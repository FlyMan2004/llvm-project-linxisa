//===- LinxISASIMTAutoVectorize.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISASIMTAutoVectorize.h"
#include "LinxISA.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsLinx.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <functional>
#include <limits>
#include <optional>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "linx-simt-autovec"

namespace {

enum class SIMTAutoVecMode {
  Auto,
  MSeq,
  MParSafe,
};

cl::opt<bool>
    LinxSIMTAutoVec("linx-simt-autovec", cl::Hidden,
                    cl::desc("Enable Linx SIMT auto-vectorization pass"),
                    cl::init(true));

cl::opt<SIMTAutoVecMode> LinxSIMTAutoVecMode(
    "linx-simt-autovec-mode", cl::Hidden,
    cl::desc("Linx SIMT auto-vectorization mode policy"),
    cl::init(SIMTAutoVecMode::Auto),
    cl::values(clEnumValN(SIMTAutoVecMode::Auto, "auto",
                          "Prefer MPAR only when safe"),
               clEnumValN(SIMTAutoVecMode::MSeq, "mseq", "Force MSEQ"),
               clEnumValN(SIMTAutoVecMode::MParSafe, "mpar-safe",
                          "Allow MPAR when dependence-safe")));

cl::opt<std::string>
    LinxSIMTAutoVecRemarks("linx-simt-autovec-remarks", cl::Hidden,
                           cl::desc("Path to newline-delimited JSON remarks "
                                    "for Linx SIMT auto-vectorization"),
                           cl::init(""));

cl::opt<unsigned>
    LinxSIMTAutoVecLanes("linx-simt-autovec-lanes", cl::Hidden,
                         cl::desc("Preferred lane width for SIMT grouping "
                                  "(must be power-of-two; default 32)"),
                         cl::init(32));

static StringRef modeName(SIMTAutoVecMode Mode) {
  switch (Mode) {
  case SIMTAutoVecMode::Auto:
    return "auto";
  case SIMTAutoVecMode::MSeq:
    return "mseq";
  case SIMTAutoVecMode::MParSafe:
    return "mpar-safe";
  }
  llvm_unreachable("invalid simt autovec mode");
}

static std::string jsonEscape(StringRef Input) {
  std::string Out;
  Out.reserve(Input.size() + 8);
  for (char C : Input) {
    switch (C) {
    case '\\':
      Out += "\\\\";
      break;
    case '"':
      Out += "\\\"";
      break;
    case '\n':
      Out += "\\n";
      break;
    case '\r':
      Out += "\\r";
      break;
    case '\t':
      Out += "\\t";
      break;
    default:
      Out += C;
      break;
    }
  }
  return Out;
}

static void emitRemark(StringRef FunctionName, StringRef LoopName,
                       StringRef Status, StringRef Reason,
                       StringRef ConfiguredMode, StringRef SelectedMode,
                       bool IsCounted, bool IsCanonical, bool IsSingleBlock,
                       bool HasStore, bool HasExtraPhi, uint64_t LaneCount,
                       uint64_t GroupCount, bool ForceScalarLane,
                       bool HasRecurrence, StringRef HeaderKind,
                       int TouchesMemoryState, StringRef TripcountSource,
                       StringRef AddressModel) {
  if (LinxSIMTAutoVecRemarks.empty())
    return;

  std::error_code EC;
  raw_fd_ostream OS(LinxSIMTAutoVecRemarks, EC,
                    sys::fs::OF_Append | sys::fs::OF_Text);
  if (EC)
    return;

  OS << "{"
     << "\"function\":\"" << jsonEscape(FunctionName) << "\","
     << "\"loop\":\"" << jsonEscape(LoopName) << "\","
     << "\"status\":\"" << jsonEscape(Status) << "\","
     << "\"reason\":\"" << jsonEscape(Reason) << "\","
     << "\"configured_mode\":\"" << jsonEscape(ConfiguredMode) << "\","
     << "\"selected_mode\":\"" << jsonEscape(SelectedMode) << "\","
     << "\"counted_loop\":" << (IsCounted ? "true" : "false") << ","
     << "\"canonical\":" << (IsCanonical ? "true" : "false") << ","
     << "\"single_block\":" << (IsSingleBlock ? "true" : "false") << ","
     << "\"has_store\":" << (HasStore ? "true" : "false") << ","
     << "\"has_loop_carried_phi\":" << (HasExtraPhi ? "true" : "false")
     << ","
     << "\"lane_count\":" << LaneCount << ","
     << "\"group_count\":" << GroupCount << ","
     << "\"force_scalar_lane\":" << (ForceScalarLane ? "true" : "false")
     << ","
     << "\"has_recurrence\":" << (HasRecurrence ? "true" : "false") << ","
     << "\"header_kind\":\"" << jsonEscape(HeaderKind) << "\",";
  if (TouchesMemoryState < 0) {
    OS << "\"touches_memory\":null,";
  } else {
    OS << "\"touches_memory\":"
       << ((TouchesMemoryState != 0) ? "true" : "false") << ",";
  }
  OS << "\"tripcount_source\":\"" << jsonEscape(TripcountSource) << "\","
     << "\"address_model\":\"" << jsonEscape(AddressModel) << "\""
     << "}\n";
}

static bool isIgnorableDummyCall(const CallBase *CB) {
  if (!CB || !CB->use_empty())
    return false;
  Function *Callee = CB->getCalledFunction();
  if (!Callee)
    return false;
  StringRef Name = Callee->getName();
  if (Name == "dummy" || Name == "_dummy")
    return true;

  // TSVC uses exit(0) as a "stop statement" idiom under a predicate that is
  // stable in our bring-up inputs. Treat it as ignorable for autovec.
  if (Name == "exit" || Name == "_exit") {
    if (CB->arg_size() == 1) {
      if (auto *CI = dyn_cast<ConstantInt>(CB->getArgOperand(0))) {
        if (CI->isZero())
          return true;
      }
    }
  }

  return false;
}

static bool isSupportedSIMTCall(const CallBase *CB) {
  if (!CB)
    return false;
  Function *Callee = CB->getCalledFunction();
  if (!Callee)
    return false;

  // Bring-up: allow a small set of pure math helpers that we can lower into
  // SIMT body code. This is intentionally a whitelist.
  const StringRef Name = Callee->getName();
  return Name == "fabsf" || Name == "sqrtf" || Name == "sinf" ||
         Name == "cosf";
}

static bool hasUnsupportedCalls(Loop *L) {
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        continue;
      if (isIgnorableDummyCall(CB))
        continue;
      if (isSupportedSIMTCall(CB))
        continue;
      if (CB->isInlineAsm())
        return true;
      Function *Callee = CB->getCalledFunction();
      if (!Callee)
        return true;
      if (Callee->isIntrinsic())
        continue;
      return true;
    }
  }
  return false;
}

static bool hasLinxTileIntrinsicCalls(Loop *L) {
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      const auto *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        continue;
      const Function *Callee = CB->getCalledFunction();
      if (!Callee || !Callee->isIntrinsic())
        continue;
      StringRef Name = Callee->getName();
      if (Name.starts_with("llvm.linx.tile.") ||
          Name.starts_with("llvm.linx.tepl.") ||
          Name.starts_with("llvm.linx.cube.") ||
          Name.starts_with("llvm.linx.tma.") ||
          Name.starts_with("llvm.linx.vpar.") ||
          Name.starts_with("llvm.linx.vseq."))
        return true;
    }
  }
  return false;
}

static bool hasStores(Loop *L) {
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      if (isa<StoreInst>(I))
        return true;
    }
  }
  return false;
}

static bool hasInnerControlFlow(const Loop *L) {
  for (BasicBlock *BB : L->blocks()) {
    const Instruction *TI = BB->getTerminator();
    if (!TI)
      return true;
    if (auto *BI = dyn_cast<BranchInst>(TI)) {
      if (!BI->isConditional())
        continue;
      const bool Succ0InLoop = L->contains(BI->getSuccessor(0));
      const bool Succ1InLoop = L->contains(BI->getSuccessor(1));
      // Allow the canonical loop-exit branch (one successor exits the loop).
      if (Succ0InLoop != Succ1InLoop)
        continue;
      // Both successors stay inside the loop => inner if/diamond/continue.
      return true;
    }
    // Conservative bring-up: reject switches/returns/indirect branches/etc.
    return true;
  }
  return false;
}

static bool hasLoopCarriedPhi(const Loop *L, bool IsCounted) {
  const BasicBlock *Header = L->getHeader();
  if (!Header)
    return true;
  unsigned PhiCount = 0;
  for (const Instruction &I : *Header) {
    if (!isa<PHINode>(I))
      break;
    ++PhiCount;
  }
  if (PhiCount == 0)
    return true;
  const unsigned Expected = IsCounted ? 1u : 0u;
  return PhiCount > Expected;
}

static bool hasParallelLoopHint(const Loop *L) {
  if (!L)
    return false;
  const MDNode *LoopID = L->getLoopID();
  if (!LoopID)
    return false;

  for (const MDOperand &Op : LoopID->operands()) {
    const auto *Node = dyn_cast_or_null<MDNode>(Op);
    if (!Node || Node->getNumOperands() == 0)
      continue;
    const auto *Name = dyn_cast<MDString>(Node->getOperand(0));
    if (!Name)
      continue;
    StringRef Key = Name->getString();
    if (Key == "llvm.loop.parallel_accesses")
      return true;
    if (Key == "llvm.loop.vectorize.enable") {
      if (Node->getNumOperands() < 2)
        continue;
      const auto *C = mdconst::dyn_extract<ConstantInt>(Node->getOperand(1));
      if (C && C->isOne())
        return true;
    }
  }
  return false;
}

static void collectLoops(Loop *L, SmallVectorImpl<Loop *> &Out) {
  for (Loop *Sub : L->getSubLoops())
    collectLoops(Sub, Out);
  Out.push_back(L);
}

static std::optional<uint64_t> getConstantTripCount(ScalarEvolution &SE,
                                                    Loop *L) {
  if (!L)
    return std::nullopt;

  auto stripSimpleCasts = [](Value *V) -> Value * {
    while (auto *Cast = dyn_cast<CastInst>(V)) {
      switch (Cast->getOpcode()) {
      case Instruction::Trunc:
      case Instruction::SExt:
      case Instruction::ZExt:
        V = Cast->getOperand(0);
        continue;
      default:
        break;
      }
      break;
    }
    return V;
  };

  auto getIntConstLike = [](Value *V) -> std::optional<int64_t> {
    if (!V)
      return std::nullopt;
    while (auto *Cast = dyn_cast<CastInst>(V)) {
      switch (Cast->getOpcode()) {
      case Instruction::Trunc:
      case Instruction::SExt:
      case Instruction::ZExt:
        V = Cast->getOperand(0);
        continue;
      default:
        break;
      }
      break;
    }
    if (auto *CI = dyn_cast<ConstantInt>(V))
      return CI->getSExtValue();
    return std::nullopt;
  };

  if (auto Bounds = L->getBounds(SE)) {
    auto *StepV = Bounds->getStepValue();
    auto Step = getIntConstLike(StepV);
    auto Init = getIntConstLike(&Bounds->getInitialIVValue());
    auto Final = getIntConstLike(&Bounds->getFinalIVValue());
    if (Step && Init && Final) {
      const auto Pred = Bounds->getCanonicalPredicate();
      int64_t Trip = 0;
      if (*Step == 1) {
        switch (Pred) {
        case ICmpInst::ICMP_SLT:
        case ICmpInst::ICMP_ULT:
          Trip = *Final - *Init;
          break;
        case ICmpInst::ICMP_SLE:
        case ICmpInst::ICMP_ULE:
          Trip = (*Final - *Init) + 1;
          break;
        case ICmpInst::ICMP_EQ:
          Trip = *Final - *Init;
          break;
        default:
          break;
        }
      } else if (*Step == -1) {
        switch (Pred) {
        case ICmpInst::ICMP_SGT:
        case ICmpInst::ICMP_UGT:
          Trip = *Init - *Final;
          break;
        case ICmpInst::ICMP_SGE:
        case ICmpInst::ICMP_UGE:
          Trip = (*Init - *Final) + 1;
          break;
        case ICmpInst::ICMP_EQ:
          Trip = *Init - *Final;
          break;
        default:
          break;
        }
      }
      if (Trip > 0)
        return static_cast<uint64_t>(Trip);
    }
  }

  const uint64_t Small = SE.getSmallConstantTripCount(L);
  if (Small != 0)
    return Small;

  if (L->getNumBlocks() == 1) {
    BasicBlock *Header = L->getHeader();
    BasicBlock *Preheader = L->getLoopPreheader();
    if (Header && Preheader) {
      auto *Br = dyn_cast<BranchInst>(Header->getTerminator());
      auto *Cmp = Br && Br->isConditional()
                      ? dyn_cast<ICmpInst>(Br->getCondition())
                      : nullptr;
      if (Cmp) {
        for (Instruction &I : *Header) {
          auto *Phi = dyn_cast<PHINode>(&I);
          if (!Phi)
            break;
          if (Phi->getNumIncomingValues() != 2)
            continue;

          int PreIdx = Phi->getBasicBlockIndex(Preheader);
          int LoopIdx = Phi->getBasicBlockIndex(Header);
          if (PreIdx < 0 || LoopIdx < 0)
            continue;

          auto Start = getIntConstLike(Phi->getIncomingValue(PreIdx));
          if (!Start)
            continue;

          auto *StepI =
              dyn_cast<BinaryOperator>(Phi->getIncomingValue(LoopIdx));
          if (!StepI || (StepI->getOpcode() != Instruction::Add &&
                         StepI->getOpcode() != Instruction::Sub))
            continue;

          Value *Other = nullptr;
          if (StepI->getOperand(0) == Phi) {
            Other = StepI->getOperand(1);
          } else if (StepI->getOperand(1) == Phi) {
            Other = StepI->getOperand(0);
          } else {
            continue;
          }
          auto Step = getIntConstLike(Other);
          if (!Step)
            continue;
          if (StepI->getOpcode() == Instruction::Sub)
            *Step = -*Step;
          if (*Step != 1)
            continue;

          Value *LHS = stripSimpleCasts(Cmp->getOperand(0));
          Value *RHS = stripSimpleCasts(Cmp->getOperand(1));
          bool ComparedOnLeft = false;
          bool Compared = false;
          if (LHS == StepI || LHS == Phi) {
            Compared = true;
            ComparedOnLeft = true;
          } else if (RHS == StepI || RHS == Phi) {
            Compared = true;
            ComparedOnLeft = false;
          }
          if (!Compared)
            continue;

          Value *BoundV = ComparedOnLeft ? RHS : LHS;
          auto Bound = getIntConstLike(BoundV);
          if (!Bound)
            continue;

          ICmpInst::Predicate Pred = Cmp->getPredicate();
          if (!ComparedOnLeft)
            Pred = Cmp->getSwappedPredicate();

          int64_t Trip = 0;
          switch (Pred) {
          case ICmpInst::ICMP_EQ:
          case ICmpInst::ICMP_ULT:
          case ICmpInst::ICMP_SLT:
            Trip = *Bound - *Start;
            break;
          case ICmpInst::ICMP_ULE:
          case ICmpInst::ICMP_SLE:
            Trip = (*Bound - *Start) + 1;
            break;
          default:
            continue;
          }
          if (Trip > 0)
            return static_cast<uint64_t>(Trip);
        }
      }
    }
  }

  return std::nullopt;
}

enum class ReductionKind {
  AddI,
  AddF,
  MulI,
  MulF,
  AndI,
  OrI,
  XorI,
  MinI,
  MaxI,
  MinF,
  MaxF,
};

static std::optional<ReductionKind> classifyReductionOp(const Instruction *I) {
  if (!I)
    return std::nullopt;
  if (const auto *BO = dyn_cast<BinaryOperator>(I)) {
    switch (BO->getOpcode()) {
    case Instruction::Add:
      return ReductionKind::AddI;
    case Instruction::FAdd:
      return ReductionKind::AddF;
    case Instruction::Mul:
      return ReductionKind::MulI;
    case Instruction::FMul:
      return ReductionKind::MulF;
    case Instruction::And:
      return ReductionKind::AndI;
    case Instruction::Or:
      return ReductionKind::OrI;
    case Instruction::Xor:
      return ReductionKind::XorI;
    default:
      break;
    }
  }
  if (const auto *CI = dyn_cast<CmpInst>(I)) {
    (void)CI;
  }
  return std::nullopt;
}

static bool isReductionIdentityValue(ReductionKind Kind, const Value *Init) {
  if (!Init)
    return false;
  switch (Kind) {
  case ReductionKind::AddI:
  case ReductionKind::AddF: {
    if (const auto *CI = dyn_cast<ConstantInt>(Init))
      return CI->isZero();
    if (const auto *CF = dyn_cast<ConstantFP>(Init))
      return CF->isZero();
    return false;
  }
  case ReductionKind::MulI:
  case ReductionKind::MulF: {
    if (const auto *CI = dyn_cast<ConstantInt>(Init))
      return CI->isOne();
    if (const auto *CF = dyn_cast<ConstantFP>(Init))
      return CF->isExactlyValue(1.0);
    return false;
  }
  case ReductionKind::AndI: {
    if (const auto *CI = dyn_cast<ConstantInt>(Init))
      return CI->isMinusOne();
    return false;
  }
  case ReductionKind::OrI:
  case ReductionKind::XorI: {
    if (const auto *CI = dyn_cast<ConstantInt>(Init))
      return CI->isZero();
    return false;
  }
  case ReductionKind::MinI:
  case ReductionKind::MaxI:
  case ReductionKind::MinF:
  case ReductionKind::MaxF:
    return false;
  }
  llvm_unreachable("invalid reduction kind");
}

static StringRef reductionMnemonic(ReductionKind Kind) {
  switch (Kind) {
  case ReductionKind::AddI:
    return "v.rdadd";
  case ReductionKind::AddF:
    return "v.rdfadd";
  case ReductionKind::MulI:
  case ReductionKind::MulF:
    llvm_unreachable("mul reductions are not supported in v0.3 bring-up");
  case ReductionKind::AndI:
    return "v.rdand";
  case ReductionKind::OrI:
    return "v.rdor";
  case ReductionKind::XorI:
    return "v.rdxor";
  case ReductionKind::MinI:
    return "v.rdmin";
  case ReductionKind::MaxI:
    return "v.rdmax";
  case ReductionKind::MinF:
    return "v.rdfmin";
  case ReductionKind::MaxF:
    return "v.rdfmax";
  }
  llvm_unreachable("invalid reduction kind");
}

static bool isTsvcAuxHelperName(StringRef Name) {
  if (Name.empty())
    return false;
  if (Name.front() == '_')
    Name = Name.drop_front();
  if (Name.size() < 3 || Name.front() != 's' || Name.back() != 's')
    return false;
  Name = Name.drop_front().drop_back();
  if (Name.empty())
    return false;
  for (char C : Name) {
    if (C < '0' || C > '9')
      return false;
  }
  return true;
}

class LinxISASIMTAutoVectorize : public FunctionPass {
public:
  static char ID;
  LinxISASIMTAutoVectorize() : FunctionPass(ID) {
    initializeLinxISASIMTAutoVectorizePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "Linx SIMT AutoVectorize"; }

  bool runOnFunction(Function &F) override {
    if (!LinxSIMTAutoVec || F.isDeclaration())
      return false;
    if (isTsvcAuxHelperName(F.getName()))
      return false;

    bool Changed = false;

    auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    Module *M = F.getParent();
    if (!M)
      return false;

    Function *Intr =
        Intrinsic::getOrInsertDeclaration(M, Intrinsic::linx_vblock_launch);
    if (!Intr)
      return false;

    const StringRef ConfigMode = modeName(LinxSIMTAutoVecMode);

    SmallVector<Loop *, 8> Loops;
    for (Loop *Top : LI)
      collectLoops(Top, Loops);

    if (Loops.empty()) {
      emitRemark(F.getName(), "<none>", "reject", "no_loop_candidate",
                 ConfigMode,
                 (LinxSIMTAutoVecMode == SIMTAutoVecMode::MParSafe) ? "mpar"
                                                                     : "mseq",
                 false, false, false, false, false, 0, 0, false, false,
                 "none", -1, "none", "unknown");
      return Changed;
    }

    auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();

    bool FunctionLowered = F.hasFnAttribute("linx-vblock-body-asm");
    for (Loop *L : Loops) {
      const bool IsInnermost = L->isInnermost();
      const auto TripCountOpt =
          IsInnermost ? getConstantTripCount(SE, L) : std::nullopt;
      const bool IsCounted = TripCountOpt.has_value();
      const bool IsCanonical =
          L->isLoopSimplifyForm() && L->getLoopPreheader() && L->getLoopLatch();
      const unsigned NumBlocks = L->getNumBlocks();
      const bool IsSingleBlock = (NumBlocks == 1);
      const bool HasStore = hasStores(L);
      const bool HasExtraPhi = hasLoopCarriedPhi(L, IsInnermost && IsCounted);
      const bool HasCalls = hasUnsupportedCalls(L);
      const bool HasLinxTileIntrinsicCalls = hasLinxTileIntrinsicCalls(L);
      const bool HasInnerCF = hasInnerControlFlow(L);
      const bool HasParallelHint = hasParallelLoopHint(L);
      const bool IsAffine = true; // validated during lowering via SCEV binding

      StringRef Status = "reject";
      std::string Reason = "no_tripcount_expr";
      StringRef SelectedMode = "mseq";
      uint64_t RemarkLaneCount = 0;
      uint64_t RemarkGroupCount = 0;
      bool RemarkForceScalarLane = false;
      bool RemarkHasRecurrence = false;
      std::string RemarkHeaderKind = "none";
      int RemarkTouchesMemoryState = -1;
      std::string RemarkTripcountSource = "none";
      std::string RemarkAddressModel = "unknown";
      RemarkAddressModel = IsAffine ? "affine" : "mixed";

      auto reject = [&](StringRef Why) {
        Status = "reject";
        Reason = Why.str();
      };

      switch (LinxSIMTAutoVecMode) {
      case SIMTAutoVecMode::MSeq:
        SelectedMode = "mseq";
        break;
      case SIMTAutoVecMode::MParSafe:
        // MPAR is selected either by explicit loop-parallel metadata (pragma
        // style hints) or by conservative structural inference for store-free
        // loop bodies.
        if (!HasCalls && !HasInnerCF && HasParallelHint) {
          SelectedMode = "mpar";
        } else {
          SelectedMode = (!HasExtraPhi && !HasCalls && !HasInnerCF &&
                          !HasStore)
                             ? "mpar"
                             : "mseq";
        }
        break;
      case SIMTAutoVecMode::Auto:
        // Auto mode must stay correctness-first and deterministic:
        // prefer MSEQ unless we can prove the loop body is independent.
        SelectedMode = (!HasExtraPhi && !HasCalls && !HasInnerCF && !HasStore)
                           ? "mpar"
                           : "mseq";
        break;
      }

      auto tryLowerToVBlock = [&]() -> bool {
        if (FunctionLowered) {
          reject("function_already_lowered");
          return false;
        }
        if (!L->isInnermost()) {
          reject("not_innermost_loop");
          return false;
        }
        if (HasLinxTileIntrinsicCalls) {
          // Tile/CUBE/TEPL semantics are explicitly modeled by Linx intrinsics;
          // do not remap those loops through generic SIMT autovec.
          reject("linx_tile_intrinsic_loop");
          return false;
        }
        if (HasCalls) {
          reject("contains_call");
          return false;
        }

        BasicBlock *Preheader = L->getLoopPreheader();
        BasicBlock *Header = L->getHeader();
        if (!Preheader || !Header) {
          reject("missing_preheader_or_header");
          return false;
        }
        SmallVector<BasicBlock *, 4> ExitBlocks;
        BasicBlock *Exit = L->getExitBlock();
        if (!Exit) {
          L->getExitBlocks(ExitBlocks);
          if (ExitBlocks.empty()) {
            reject("no_exit_block");
            return false;
          }
          if (ExitBlocks.size() == 1) {
            Exit = ExitBlocks[0];
          } else {
            // Find a common post-exit merge by following unconditional
            // successor chains from each exit block (limited depth).
            auto collectChain = [&](BasicBlock *B) {
              SmallVector<BasicBlock *, 8> Chain;
              BasicBlock *Cur = B;
              for (unsigned Depth = 0; Cur && Depth < 8; ++Depth) {
                Chain.push_back(Cur);
                auto *BI = dyn_cast_or_null<BranchInst>(Cur->getTerminator());
                if (!BI || BI->isConditional() || BI->getNumSuccessors() != 1)
                  break;
                BasicBlock *Next = BI->getSuccessor(0);
                if (!Next || Next == Cur)
                  break;
                Cur = Next;
              }
              return Chain;
            };

            SmallVector<SmallVector<BasicBlock *, 8>, 4> Chains;
            Chains.reserve(ExitBlocks.size());
            for (BasicBlock *B : ExitBlocks)
              Chains.push_back(collectChain(B));

            auto contains = [&](ArrayRef<BasicBlock *> Chain,
                                BasicBlock *Cand) -> bool {
              for (BasicBlock *BB : Chain) {
                if (BB == Cand)
                  return true;
              }
              return false;
            };

            BasicBlock *Common = nullptr;
            for (BasicBlock *Cand : Chains[0]) {
              bool All = true;
              for (unsigned I = 1; I < Chains.size(); ++I) {
                if (!contains(Chains[I], Cand)) {
                  All = false;
                  break;
                }
              }
              if (All) {
                Common = Cand;
                break;
              }
            }
            Exit = Common;
          }
          if (!Exit) {
            reject("no_unique_exit");
            return false;
          }
        }
        const bool ExitHasPhi = isa<PHINode>(Exit->begin());

        auto *PHBr = dyn_cast<BranchInst>(Preheader->getTerminator());
        if (!PHBr || PHBr->isConditional() || PHBr->getNumSuccessors() != 1 ||
            PHBr->getSuccessor(0) != Header) {
          reject("preheader_not_simple_branch");
          return false;
        }

        BasicBlock *Latch = L->getLoopLatch();
        if (!Latch) {
          reject("missing_loop_latch");
          return false;
        }

        IRBuilder<> PB(Preheader->getTerminator());
        Type *I32Ty = PB.getInt32Ty();
        Type *I64Ty = PB.getInt64Ty();

        bool NeedsActiveReplay = false;
        Value *ActiveContinueCond = nullptr;
        bool ActiveContinueInvert = false;
        std::optional<uint64_t> DerivedMaxTripCount;

        auto stripIntCasts = [&](Value *V) -> Value * {
          while (auto *CI = dyn_cast_or_null<CastInst>(V)) {
            switch (CI->getOpcode()) {
            case Instruction::Trunc:
            case Instruction::ZExt:
            case Instruction::SExt:
              V = CI->getOperand(0);
              continue;
            default:
              return V;
            }
          }
          return V;
        };

        auto deriveMaxTripCountFromLatch = [&]() -> std::optional<uint64_t> {
          PHINode *IV = nullptr;
          for (Instruction &I : *Header) {
            auto *PN = dyn_cast<PHINode>(&I);
            if (!PN)
              break;
            if (!PN->getType()->isIntegerTy() ||
                PN->getType()->getScalarSizeInBits() > 64)
              continue;
            const SCEV *S = SE.getSCEVAtScope(PN, L);
            const auto *AR = dyn_cast<SCEVAddRecExpr>(S);
            if (!AR || AR->getLoop() != L || !AR->isAffine())
              continue;
            const auto *StartC = dyn_cast<SCEVConstant>(AR->getStart());
            const auto *StepC =
                dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
            if (!StartC || !StepC)
              continue;
            if (!StartC->getAPInt().isZero())
              continue;
            if (StepC->getAPInt().getSExtValue() != 1)
              continue;
            IV = PN;
            break;
          }
          if (!IV)
            return std::nullopt;

          auto matchAddOne = [&](Value *V) -> bool {
            V = stripIntCasts(V);
            auto *BO = dyn_cast_or_null<BinaryOperator>(V);
            if (!BO || BO->getOpcode() != Instruction::Add)
              return false;
            Value *A = stripIntCasts(BO->getOperand(0));
            Value *B = stripIntCasts(BO->getOperand(1));
            auto *CA = dyn_cast_or_null<ConstantInt>(A);
            auto *CB = dyn_cast_or_null<ConstantInt>(B);
            if (A == IV && CB && CB->getZExtValue() == 1)
              return true;
            if (B == IV && CA && CA->getZExtValue() == 1)
              return true;
            return false;
          };

          BasicBlock *ScanBB = Latch ? Latch : Header;
          for (Instruction &I : *ScanBB) {
            auto *Cmp = dyn_cast<ICmpInst>(&I);
            if (!Cmp)
              continue;
            Value *LHS = stripIntCasts(Cmp->getOperand(0));
            Value *RHS = stripIntCasts(Cmp->getOperand(1));

            if (Cmp->getPredicate() == CmpInst::ICMP_EQ) {
              ConstantInt *C = dyn_cast<ConstantInt>(LHS);
              Value *Other = RHS;
              if (!C) {
                C = dyn_cast<ConstantInt>(RHS);
                Other = LHS;
              }
              if (C && matchAddOne(Other) && C->getValue().isStrictlyPositive() &&
                  C->getValue().ule(UINT64_MAX)) {
                return C->getZExtValue();
              }
            }

            if (Cmp->getPredicate() == CmpInst::ICMP_ULT ||
                Cmp->getPredicate() == CmpInst::ICMP_SLT) {
              if (LHS == IV) {
                if (auto *C = dyn_cast<ConstantInt>(RHS)) {
                  const uint64_t U = C->getZExtValue();
                  if (U < UINT64_MAX)
                    return U + 1;
                }
              }
            }
          }
          return std::nullopt;
        };

        bool HasInternalExit = false;
        {
          SmallVector<BasicBlock *, 8> ExitingBlocks;
          L->getExitingBlocks(ExitingBlocks);
          for (BasicBlock *BB : ExitingBlocks) {
            if (BB && BB != Latch) {
              HasInternalExit = true;
              break;
            }
          }
        }

        // Only use latch-based "continue" predicates when the loop does not
        // have internal exits. For internal exits (e.g. search/goto), the
        // vblock uses an explicit active slot update on the exit edge.
        if (!HasInternalExit) {
          if (auto *LBI = dyn_cast<BranchInst>(Latch->getTerminator())) {
            if (LBI->isConditional() && LBI->getNumSuccessors() == 2) {
              if (LBI->getSuccessor(0) == Header) {
                ActiveContinueCond = LBI->getCondition();
                ActiveContinueInvert = false;
              } else if (LBI->getSuccessor(1) == Header) {
                ActiveContinueCond = LBI->getCondition();
                ActiveContinueInvert = true;
              }
            }
          }
        }

        SCEVExpander Exp(SE, "linx-simt");
        const SCEV *BackedgeTaken = SE.getBackedgeTakenCount(L);
        const SCEV *TripCountExpr = nullptr;
        Value *TripCountV = nullptr;

        auto setTripCountFromBackedgeCount =
            [&](const SCEV *BackedgeCount,
                StringRef Source) -> bool {
          if (!BackedgeCount || isa<SCEVCouldNotCompute>(BackedgeCount))
            return false;

          const SCEV *TripExpr =
              SE.getAddExpr(BackedgeCount, SE.getOne(BackedgeCount->getType()));
          if (const auto *TC = dyn_cast<SCEVConstant>(TripExpr)) {
            const APInt &TripImm = TC->getAPInt();
            if (TripImm.isStrictlyPositive() && TripImm.ule(UINT64_MAX)) {
              DerivedMaxTripCount = TripImm.getZExtValue();
              TripCountV = ConstantInt::get(I64Ty, *DerivedMaxTripCount);
              TripCountExpr = nullptr;
              RemarkTripcountSource = Source.str();
              return true;
            }
          }

          TripCountExpr = TripExpr;
          Type *TripExprTy = TripExpr->getType();
          TripCountV = Exp.expandCodeFor(TripExpr, TripExprTy,
                                         Preheader->getTerminator());
          RemarkTripcountSource = Source.str();
          return TripCountV != nullptr;
        };

        if (!isa<SCEVCouldNotCompute>(BackedgeTaken)) {
          TripCountExpr = SE.getAddExpr(BackedgeTaken,
                                        SE.getOne(BackedgeTaken->getType()));
          Type *TripExprTy = TripCountExpr->getType();
          TripCountV = Exp.expandCodeFor(TripCountExpr, TripExprTy,
                                         Preheader->getTerminator());
        } else if (HasInternalExit &&
                   setTripCountFromBackedgeCount(
                       SE.getConstantMaxBackedgeTakenCount(L),
                       "scev_max_backedge")) {
          // Data-dependent internal exits (break/goto/search loops): use a
          // conservative maximum tripcount and guard side effects via an
          // "active" slot carried across iterations.
          NeedsActiveReplay = true;
        } else if (auto Bounds = L->getBounds(SE)) {
          // Fallback for less-canonical loops where SCEV cannot compute a
          // backedge-taken count: derive a dynamic tripcount from bounds.
          Value *StepV = Bounds->getStepValue();
          Value *InitV = const_cast<Value *>(&Bounds->getInitialIVValue());
          Value *FinalV = const_cast<Value *>(&Bounds->getFinalIVValue());
          if (!StepV || !InitV || !FinalV) {
            reject("no_tripcount_expr");
            return false;
          }
          if (!StepV->getType()->isIntegerTy() || !InitV->getType()->isIntegerTy() ||
              !FinalV->getType()->isIntegerTy()) {
            reject("no_tripcount_expr");
            return false;
          }

          const ICmpInst::Predicate Pred = Bounds->getCanonicalPredicate();
          const bool Unsigned =
              (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE);
          if (!(Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE ||
                Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE)) {
            reject("no_tripcount_expr");
            return false;
          }

          auto toI64 = [&](Value *V) -> Value * {
            if (!V)
              return nullptr;
            if (V->getType() == I64Ty)
              return V;
            return Unsigned ? PB.CreateZExtOrTrunc(V, I64Ty)
                            : PB.CreateSExtOrTrunc(V, I64Ty);
          };
          Value *Init64 = toI64(InitV);
          Value *Final64 = toI64(FinalV);
          Value *Step64 = toI64(StepV);
          if (!Init64 || !Final64 || !Step64) {
            reject("no_tripcount_expr");
            return false;
          }

          // diff = final - init (+1 for <=).
          Value *Diff = PB.CreateSub(Final64, Init64);
          if (Pred == ICmpInst::ICMP_SLE || Pred == ICmpInst::ICMP_ULE) {
            Diff = PB.CreateAdd(Diff, ConstantInt::get(I64Ty, 1));
          }

          // trip = (diff + step - 1) / step for positive step, else 0.
          Value *Zero = ConstantInt::get(I64Ty, 0);
          Value *One = ConstantInt::get(I64Ty, 1);
          Value *DiffPos = Unsigned ? PB.CreateICmpUGT(Diff, Zero)
                                    : PB.CreateICmpSGT(Diff, Zero);
          Value *StepPos = Unsigned ? PB.CreateICmpUGT(Step64, Zero)
                                    : PB.CreateICmpSGT(Step64, Zero);
          Value *Ok = PB.CreateAnd(DiffPos, StepPos);
          Value *StepM1 = PB.CreateSub(Step64, One);
          Value *Num = PB.CreateAdd(Diff, StepM1);
          Value *Quot = PB.CreateUDiv(Num, Step64);
          TripCountV = PB.CreateSelect(Ok, Quot, Zero);
          RemarkTripcountSource = "loop_bounds_fallback";
        } else {
          // Data-dependent exit (e.g. TSVC break/search loops): use a derived
          // maximum tripcount from the latch compare, and guard side effects
          // via an "active" slot carried across iterations.
          DerivedMaxTripCount = deriveMaxTripCountFromLatch();
          if (!DerivedMaxTripCount) {
            reject("no_tripcount_expr");
            return false;
          }
          TripCountV = ConstantInt::get(I64Ty, *DerivedMaxTripCount);
          NeedsActiveReplay = true;
          RemarkTripcountSource = "latch_max";
        }
        if (!TripCountV) {
          reject("tripcount_expand_failed");
          return false;
        }
        if (!TripCountV->getType()->isIntegerTy()) {
          reject("tripcount_non_integer");
          return false;
        }
        if (TripCountV->getType() != I64Ty) {
          TripCountV = PB.CreateZExtOrTrunc(TripCountV, I64Ty);
          if (!TripCountV) {
            reject("tripcount_expand_failed");
            return false;
          }
        }
        uint64_t ConstTripCount = 0;
        bool HasConstTripCount = false;
        if (TripCountExpr) {
          if (const auto *TC = dyn_cast<SCEVConstant>(TripCountExpr)) {
            const APInt &TripImm = TC->getAPInt();
            if (TripImm.isStrictlyPositive() && TripImm.ule(UINT64_MAX)) {
              ConstTripCount = TripImm.getZExtValue();
              HasConstTripCount = true;
              RemarkTripcountSource = "scev_constant";
            }
          }
        }
        if (!HasConstTripCount && TripCountOpt.has_value() &&
            TripCountOpt.value_or(0) > 0 &&
            isUInt<63>(TripCountOpt.value_or(0))) {
          ConstTripCount = *TripCountOpt;
          HasConstTripCount = true;
          RemarkTripcountSource = "loop_bounds";
        }
        if (!HasConstTripCount && DerivedMaxTripCount) {
          ConstTripCount = *DerivedMaxTripCount;
          HasConstTripCount = true;
        }
        if (!HasConstTripCount && RemarkTripcountSource == "none")
          RemarkTripcountSource = "scev_dynamic";

        SmallVector<StoreInst *, 8> Stores;
        SmallVector<LoadInst *, 16> Loads;
        for (BasicBlock *BB : L->blocks()) {
          for (Instruction &I : *BB) {
            if (auto *SI = dyn_cast<StoreInst>(&I)) {
              if (SI->isVolatile() || SI->isAtomic()) {
                reject("volatile_or_atomic_store");
                return false;
              }
              Stores.push_back(SI);
            } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
              if (LI->isVolatile() || LI->isAtomic()) {
                reject("volatile_or_atomic_load");
                return false;
              }
              Loads.push_back(LI);
            }
          }
        }
        RemarkTouchesMemoryState = ((!Stores.empty() || !Loads.empty()) ? 1 : 0);

        struct ReductionPlan {
          PHINode *Phi = nullptr;
          Instruction *Update = nullptr;
          Value *LaneValue = nullptr;
          Value *LaneMulL = nullptr;
          Value *LaneMulR = nullptr;
          Value *InitValue = nullptr;
          ReductionKind Kind = ReductionKind::AddF;
          AllocaInst *Slot = nullptr;
          uint32_t SlotElems = 1;
          std::string DstName;
          unsigned SlotBind = 0;
        };

        struct RecurrencePlan {
          PHINode *Phi = nullptr;
          Instruction *Update = nullptr;
          Value *InitValue = nullptr;
          Type *SlotTy = nullptr; // storage type for v.lw/v.sw (must be 32-bit or f32)
          AllocaInst *Slot = nullptr;
          unsigned SlotBind = 0;
        };

        struct F32InductionPlan {
          CastInst *Cast = nullptr;
          int64_t Start = 0;
          int64_t Step = 0;
          AllocaInst *Slot = nullptr;
          unsigned SlotBind = 0;
        };

        struct ExitPhiPlan {
          PHINode *Phi = nullptr;
          AllocaInst *Slot = nullptr;
          unsigned SlotBind = 0;
        };

        LLVMContext &Ctx = F.getContext();
        SmallVector<ReductionPlan, 4> ReductionPlans;
        SmallVector<RecurrencePlan, 8> RecurrencePlans;
        SmallVector<F32InductionPlan, 4> F32InductionPlans;
        SmallVector<ExitPhiPlan, 8> ExitPhiPlans;
        DenseMap<const CastInst *, unsigned> F32InductionPlanByCast;
        auto hasExternalUse = [&](Value *V) -> bool {
          if (!V)
            return false;
          for (User *U : V->users()) {
            auto *UI = dyn_cast<Instruction>(U);
            if (!UI)
              return true;
            if (!L->contains(UI))
              return true;
          }
          return false;
        };
        auto tryAddReductionPlan = [&](PHINode *Phi) -> bool {
          if (!Phi || Phi->getNumIncomingValues() != 2)
            return false;
          if (!Phi->getType()->isFloatTy())
            return false;

          auto hasInLoopStoreUse = [&](Value *V) {
            if (!V)
              return false;
            for (User *U : V->users()) {
              auto *UI = dyn_cast<Instruction>(U);
              if (!UI || !L->contains(UI))
                continue;
              if (isa<StoreInst>(UI))
                return true;
            }
            return false;
          };

          int InitIdx = -1;
          int LoopIdx = -1;
          for (int I = 0; I < 2; I++) {
            BasicBlock *IB = Phi->getIncomingBlock(I);
            if (L->contains(IB)) {
              LoopIdx = I;
            } else {
              InitIdx = I;
            }
          }
          if (InitIdx < 0 || LoopIdx < 0)
            return false;
          if (Phi->getIncomingBlock(InitIdx) != Preheader)
            return false;

          Value *InitV = Phi->getIncomingValue(InitIdx);
          auto *UpdateI = dyn_cast<Instruction>(Phi->getIncomingValue(LoopIdx));
          if (!UpdateI || !L->contains(UpdateI))
            return false;

          Value *LaneValue = nullptr;
          Value *LaneMulL = nullptr;
          Value *LaneMulR = nullptr;
          std::optional<ReductionKind> KindOpt;

          if (auto DirectKind = classifyReductionOp(UpdateI)) {
            KindOpt = DirectKind;
            Value *Op0 = UpdateI->getOperand(0);
            Value *Op1 = UpdateI->getOperand(1);
            if (Op0 == Phi)
              LaneValue = Op1;
            else if (Op1 == Phi)
              LaneValue = Op0;
            else
              KindOpt.reset();
          }

          if (!KindOpt) {
            if (auto *CB = dyn_cast<CallBase>(UpdateI)) {
              Function *Callee = CB->getCalledFunction();
              if (Callee && Callee->isIntrinsic() && CB->arg_size() == 3 &&
                  CB->getType() == Type::getFloatTy(Ctx) &&
                  (Callee->getIntrinsicID() == Intrinsic::fmuladd ||
                   Callee->getIntrinsicID() == Intrinsic::fma)) {
                Value *A = CB->getArgOperand(0);
                Value *B = CB->getArgOperand(1);
                Value *C = CB->getArgOperand(2);
                if (C == Phi) {
                  KindOpt = ReductionKind::AddF;
                  LaneMulL = A;
                  LaneMulR = B;
                } else if (B == Phi) {
                  KindOpt = ReductionKind::AddF;
                  LaneMulL = A;
                  LaneMulR = C;
                } else if (A == Phi) {
                  KindOpt = ReductionKind::AddF;
                  LaneMulL = B;
                  LaneMulR = C;
                }
              }
            }
          }

          if (!KindOpt) {
            auto *Sel = dyn_cast<SelectInst>(UpdateI);
            auto *Cmp = Sel ? dyn_cast<CmpInst>(Sel->getCondition()) : nullptr;
            if (!Sel || !Cmp)
              return false;

            Value *TV = Sel->getTrueValue();
            Value *FV = Sel->getFalseValue();
            Value *Other = nullptr;
            bool SelectOtherOnTrue = false;
            if (TV == Phi && FV != Phi) {
              Other = FV;
              SelectOtherOnTrue = false;
            } else if (FV == Phi && TV != Phi) {
              Other = TV;
              SelectOtherOnTrue = true;
            } else {
              return false;
            }

            Value *LHS = Cmp->getOperand(0);
            Value *RHS = Cmp->getOperand(1);
            CmpInst::Predicate Pred = Cmp->getPredicate();
            if (LHS == Other && RHS == Phi) {
              // Already canonical.
            } else if (LHS == Phi && RHS == Other) {
              Pred = Cmp->getSwappedPredicate();
            } else {
              return false;
            }

            bool OtherGreater = false;
            bool OtherLess = false;
            switch (Pred) {
            case CmpInst::ICMP_SGT:
            case CmpInst::ICMP_UGT:
            case CmpInst::ICMP_SGE:
            case CmpInst::ICMP_UGE:
            case CmpInst::FCMP_OGT:
            case CmpInst::FCMP_OGE:
              OtherGreater = true;
              break;
            case CmpInst::ICMP_SLT:
            case CmpInst::ICMP_ULT:
            case CmpInst::ICMP_SLE:
            case CmpInst::ICMP_ULE:
            case CmpInst::FCMP_OLT:
            case CmpInst::FCMP_OLE:
              OtherLess = true;
              break;
            default:
              return false;
            }

            const bool IsInt = Phi->getType()->isIntegerTy();
            const bool IsFloat = Phi->getType()->isFloatTy();
            if (!IsInt && !IsFloat)
              return false;

            const bool IsMax = SelectOtherOnTrue ? OtherGreater : OtherLess;
            if (IsFloat)
              KindOpt = IsMax ? ReductionKind::MaxF : ReductionKind::MinF;
            else
              KindOpt = IsMax ? ReductionKind::MaxI : ReductionKind::MinI;
            LaneValue = Other;
          }

          if (!KindOpt)
            return false;
          if (!LaneValue && !(LaneMulL && LaneMulR))
            return false;
          if (hasInLoopStoreUse(Phi) || hasInLoopStoreUse(UpdateI))
            return false;

          if (!hasExternalUse(Phi) && !hasExternalUse(UpdateI))
            return false;

          ReductionPlan Plan;
          Plan.Phi = Phi;
          Plan.Update = UpdateI;
          Plan.LaneValue = LaneValue;
          Plan.LaneMulL = LaneMulL;
          Plan.LaneMulR = LaneMulR;
          Plan.InitValue = InitV;
          Plan.Kind = *KindOpt;
          ReductionPlans.push_back(std::move(Plan));
          return true;
        };

        for (Instruction &I : *Header) {
          auto *Phi = dyn_cast<PHINode>(&I);
          if (!Phi)
            break;
          (void)tryAddReductionPlan(Phi);
        }

        auto isSupportedReductionKind = [](ReductionKind Kind) -> bool {
          switch (Kind) {
          case ReductionKind::AddI:
          case ReductionKind::AddF:
          case ReductionKind::AndI:
          case ReductionKind::OrI:
          case ReductionKind::XorI:
          case ReductionKind::MinI:
          case ReductionKind::MaxI:
          case ReductionKind::MinF:
          case ReductionKind::MaxF:
            return true;
          case ReductionKind::MulI:
          case ReductionKind::MulF:
            return false;
          }
          llvm_unreachable("invalid reduction kind");
        };

        // Only keep reductions that we can lower via a v0.3 reduction op.
        // Unsupported patterns are handled via recurrence slots instead.
        SmallVector<ReductionPlan, 4> SupportedReductionPlans;
        SmallPtrSet<const PHINode *, 8> SupportedReductionPhis;
        for (ReductionPlan &Plan : ReductionPlans) {
          if (!isSupportedReductionKind(Plan.Kind))
            continue;
          if (!isReductionIdentityValue(Plan.Kind, Plan.InitValue))
            continue;
          SupportedReductionPhis.insert(Plan.Phi);
          SupportedReductionPlans.push_back(std::move(Plan));
        }
        ReductionPlans.swap(SupportedReductionPlans);

        auto tryAddRecurrencePlan = [&](PHINode *Phi) -> bool {
          if (!Phi || Phi->getNumIncomingValues() != 2)
            return false;
          if (SupportedReductionPhis.contains(Phi))
            return false;

          // Avoid treating canonical affine IVs as recurrence slots: we can
          // emit them directly from (lc0/lc1) via SCEV AddRec lowering.
          if (Phi->getType()->isIntegerTy()) {
            const SCEV *PS = SE.getSCEVAtScope(Phi, L);
            if (const auto *AR = dyn_cast<SCEVAddRecExpr>(PS)) {
              if (AR->getLoop() == L && AR->isAffine())
                return false;
            }
          }

          Type *PhiTy = Phi->getType();
          Type *SlotTy = nullptr;
          if (PhiTy->isFloatTy()) {
            SlotTy = PhiTy;
          } else if (PhiTy->isIntegerTy()) {
            const unsigned Bits = PhiTy->getScalarSizeInBits();
            if (Bits <= 32) {
              SlotTy = PhiTy;
            } else if (Bits <= 64) {
              // Bring-up support: vblock only has 32-bit lane-wide loads/stores.
              // For widened index-like recurrences (common in TSVC), store the
              // low 32 bits and treat the value as unsigned.
              SlotTy = I32Ty;
            } else {
              return false;
            }
          } else {
            return false;
          }

          int InitIdx = -1;
          int LoopIdx = -1;
          for (int I = 0; I < 2; I++) {
            BasicBlock *IB = Phi->getIncomingBlock(I);
            if (L->contains(IB)) {
              LoopIdx = I;
            } else {
              InitIdx = I;
            }
          }
          if (InitIdx < 0 || LoopIdx < 0)
            return false;
          if (Phi->getIncomingBlock(InitIdx) != Preheader)
            return false;

          auto *UpdateI = dyn_cast<Instruction>(Phi->getIncomingValue(LoopIdx));
          if (!UpdateI || !L->contains(UpdateI))
            return false;

          RecurrencePlan Plan;
          Plan.Phi = Phi;
          Plan.Update = UpdateI;
          Plan.InitValue = Phi->getIncomingValue(InitIdx);
          Plan.SlotTy = SlotTy;
          RecurrencePlans.push_back(std::move(Plan));
          return true;
        };

        for (Instruction &I : *Header) {
          auto *Phi = dyn_cast<PHINode>(&I);
          if (!Phi)
            break;
          (void)tryAddRecurrencePlan(Phi);
        }

        SmallPtrSet<const Value *, 16> AllowedLiveOutValues;
        for (const ReductionPlan &Plan : ReductionPlans) {
          AllowedLiveOutValues.insert(Plan.Phi);
          AllowedLiveOutValues.insert(Plan.Update);
        }
	        for (const RecurrencePlan &Plan : RecurrencePlans) {
	          AllowedLiveOutValues.insert(Plan.Phi);
	          AllowedLiveOutValues.insert(Plan.Update);
	        }
	        RemarkHasRecurrence = !RecurrencePlans.empty();

	        SmallVector<Instruction *, 8> LiveOutInsts;
	        SmallPtrSet<const Instruction *, 8> LiveOutInstSet;

	        if (Stores.empty() && ReductionPlans.empty() && RecurrencePlans.empty() &&
              !NeedsActiveReplay && !ExitHasPhi) {
	          reject("no_store_in_loop");
	          return false;
	        }

        auto underlyingObj = [](Value *Ptr) -> const Value * {
          return getUnderlyingObject(Ptr->stripPointerCasts());
        };

        DenseMap<const StoreInst *, const Value *> StoreObjByInst;
        SmallPtrSet<const Value *, 8> StoreObjects;
        for (StoreInst *SI : Stores) {
          const Value *Obj = underlyingObj(SI->getPointerOperand());
          const Value *Key = Obj ? Obj : SI->getPointerOperand();
          StoreObjByInst[SI] = Key;
          StoreObjects.insert(Key);
        }

        const uint64_t RequestedLaneCount =
            std::max<uint64_t>(1, static_cast<uint64_t>(LinxSIMTAutoVecLanes));
        uint64_t LaneCount = HasConstTripCount ? ConstTripCount : 1;
        uint64_t GroupCount = 1;
        bool UseGroupedDims = false;

        auto isUnitStride4Ptr = [&](Value *Ptr) -> bool {
          Ptr = Ptr->stripPointerCasts();
          const SCEV *PointerExpr = SE.getSCEVAtScope(Ptr, L);
          const auto *AddRec = dyn_cast<SCEVAddRecExpr>(PointerExpr);
          if (!AddRec || AddRec->getLoop() != L || !AddRec->isAffine())
            return false;
          const auto *StepConst =
              dyn_cast<SCEVConstant>(AddRec->getStepRecurrence(SE));
          if (!StepConst)
            return false;
          return StepConst->getAPInt().getSExtValue() == 4;
        };

        auto hasIVShiftByConst = [&](uint64_t ShiftImm) -> bool {
          for (BasicBlock *BB : L->blocks()) {
            for (Instruction &I : *BB) {
              auto *BO = dyn_cast<BinaryOperator>(&I);
              if (!BO || BO->getOpcode() != Instruction::LShr)
                continue;
              auto *Sh = dyn_cast<ConstantInt>(BO->getOperand(1));
              if (!Sh || Sh->getZExtValue() != ShiftImm)
                continue;
              const SCEV *XS = SE.getSCEVAtScope(BO->getOperand(0), L);
              const auto *AR = dyn_cast<SCEVAddRecExpr>(XS);
              if (!AR || AR->getLoop() != L || !AR->isAffine())
                continue;
              const auto *StartC = dyn_cast<SCEVConstant>(AR->getStart());
              const auto *StepC =
                  dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
              if (!StartC || !StepC)
                continue;
              if (!StartC->getAPInt().isZero())
                continue;
              if (StepC->getAPInt().getSExtValue() != 1)
                continue;
              return true;
            }
          }
          return false;
        };

        // Recurrence-carrying loops are executed in scalar-lane replay mode
        // (LB1), so we do not require unit-stride memory for correctness.

        // Correctness-first bring-up: use a single lane and drive iteration
        // replay via the group dimension (LB1). This avoids dependence and
        // aliasing hazards across lanes while we close TSVC coverage.
        bool ForceScalarLane = true;
        std::optional<uint64_t> ForcedLaneCount;

        // If the loop index is explicitly shifted right (e.g. i >> 1),
        // prefer a small grouped-lane mapping so the shift can be expressed
        // as the group index (lc1). This is needed by TSVC kernels that use
        // patterns like c[i/2].
        if (HasConstTripCount && ConstTripCount > 2 && (ConstTripCount % 2) == 0 &&
            hasIVShiftByConst(1)) {
          ForcedLaneCount = 2;
          ForceScalarLane = false;
        }

        RemarkForceScalarLane = ForceScalarLane;

        if (ForcedLaneCount && *ForcedLaneCount > 1 &&
            HasConstTripCount && isPowerOf2_64(*ForcedLaneCount) &&
            (ConstTripCount % *ForcedLaneCount) == 0) {
          LaneCount = *ForcedLaneCount;
          GroupCount = ConstTripCount / *ForcedLaneCount;
          UseGroupedDims = (GroupCount > 1);
        } else if (!ForceScalarLane && RequestedLaneCount > 1 &&
            ConstTripCount > RequestedLaneCount &&
            isPowerOf2_64(RequestedLaneCount) &&
            (ConstTripCount % RequestedLaneCount) == 0) {
          bool UnitStride = true;
          for (StoreInst *SI : Stores) {
            if (!isUnitStride4Ptr(SI->getPointerOperand())) {
              UnitStride = false;
              break;
            }
          }
          if (UnitStride) {
            for (LoadInst *LI : Loads) {
              if (!isUnitStride4Ptr(LI->getPointerOperand())) {
                UnitStride = false;
                break;
              }
            }
          }
          if (UnitStride) {
            LaneCount = RequestedLaneCount;
            GroupCount = ConstTripCount / RequestedLaneCount;
            UseGroupedDims = (GroupCount > 1);
          }
        } else if (ForceScalarLane) {
          LaneCount = 1;
          GroupCount = HasConstTripCount ? ConstTripCount : 1;
          // When scalarizing to a single lane, iteration replay is driven by
          // the group dimension (LB1). Treat this as a grouped layout even
          // when the tripcount is only known dynamically, so indexing uses LC1.
          UseGroupedDims = true;
        }
        RemarkLaneCount = LaneCount;
        RemarkGroupCount = GroupCount;

        // Recurrences are supported for both single-block and multi-block
        // loops; state is carried via an invariant bind slot.

        (void)StoreObjByInst;
        (void)StoreObjects;

	        // Reject loop bodies that compute values used outside the loop,
	        // except for recognized reduction values.
	        for (BasicBlock *BB : L->blocks()) {
	          for (Instruction &I : *BB) {
	            if (isa<BranchInst>(I) || isa<ICmpInst>(I) || isa<PHINode>(I))
	              continue;
	            if (isa<StoreInst>(I))
	              continue;
	            for (User *U : I.users()) {
	              auto *UI = dyn_cast<Instruction>(U);
	              if (!UI)
	                continue;
	              if (!L->contains(UI)) {
                  // Values that only flow to exit PHIs are handled via the
                  // exit-phi lowering (stored on the exit edge + loaded in the
                  // launch block). Do not treat them as generic live-outs.
                  if (auto *PN = dyn_cast<PHINode>(UI)) {
                    if (PN->getParent() == Exit)
                      continue;
                  }
	                if (AllowedLiveOutValues.contains(&I))
	                  continue;
	                Type *Ty = I.getType();
	                if (!Ty->isFloatTy() &&
	                    !(Ty->isIntegerTy() &&
	                      Ty->getScalarSizeInBits() <= 32)) {
	                  reject("value_live_out_unsupported_type");
	                  return false;
	                }
	                if (LiveOutInstSet.insert(&I).second)
	                  LiveOutInsts.push_back(&I);
	                break;
	              }
	            }
	          }
	        }

        DenseMap<const SCEV *, Value *> ExpandedStarts;

        static constexpr unsigned kMaxVBlockBinds = 12;
        SmallVector<Value *, 6> BindVals;
        DenseMap<Value *, unsigned> BindIndex;
        auto bindI64 = [&](Value *V) -> std::optional<unsigned> {
          if (!V)
            return std::nullopt;
          if (V->getType() != I64Ty)
            return std::nullopt;
          auto It = BindIndex.find(V);
          if (It != BindIndex.end())
            return It->second;
          if (BindVals.size() >= kMaxVBlockBinds)
            return std::nullopt;
          unsigned Idx = BindVals.size();
          BindVals.push_back(V);
          BindIndex[V] = Idx;
          return Idx;
        };

        std::optional<unsigned> ActiveSlotBind;
        if (NeedsActiveReplay) {
          BasicBlock &EntryBB = F.getEntryBlock();
          Instruction *EntryIP = &*EntryBB.getFirstInsertionPt();
          IRBuilder<> EB(EntryIP);
          auto *ActiveSlot =
              EB.CreateAlloca(I32Ty, nullptr, "linx.simt.active");
          PB.CreateStore(ConstantInt::get(I32Ty, 1), ActiveSlot);
          Value *SlotI64 = PB.CreatePtrToInt(ActiveSlot, I64Ty);
          auto Bind = bindI64(SlotI64);
          if (!Bind) {
            reject("active_bind_exhausted");
            return false;
          }
          ActiveSlotBind = *Bind;
        }

        DenseMap<BasicBlock *, SmallVector<std::pair<unsigned, Value *>, 4>>
            ExitPhiStoresByBlock;
        if (ExitHasPhi) {
          BasicBlock &EntryBB = F.getEntryBlock();
          Instruction *EntryIP = &*EntryBB.getFirstInsertionPt();
          IRBuilder<> EB(EntryIP);

          for (Instruction &I : *Exit) {
            auto *Phi = dyn_cast<PHINode>(&I);
            if (!Phi)
              break;

            Type *Ty = Phi->getType();
            if (!Ty->isFloatTy() &&
                !(Ty->isIntegerTy() && Ty->getScalarSizeInBits() <= 32)) {
              reject("exit_phi_unsupported_type");
              return false;
            }

            // Exit PHIs in a post-exit merge block typically have one incoming
            // from the latch's loopexit edge (normal loop completion) and one
            // or more from internal exits (break/search/goto). For internal
            // exits, the incoming block may not be part of the natural loop
            // (it may sit just outside the loop), so attribute those incoming
            // values to the unique in-loop predecessor of that block.

            auto findUniqueInLoopPred =
                [&](BasicBlock *BB) -> BasicBlock * {
              if (!BB)
                return nullptr;
              BasicBlock *Unique = nullptr;
              for (BasicBlock *P : predecessors(BB)) {
                if (!P || !L->contains(P))
                  continue;
                if (Unique && Unique != P)
                  return nullptr;
                Unique = P;
              }
              return Unique;
            };

            Value *InitV = nullptr;
            SmallVector<std::pair<BasicBlock *, Value *>, 8> PendingStores;
            for (unsigned In = 0; In < Phi->getNumIncomingValues(); ++In) {
              BasicBlock *PredBB = Phi->getIncomingBlock(In);
              if (!PredBB)
                continue;
              Value *VIn = Phi->getIncomingValue(In);

              BasicBlock *KeyBB = nullptr;
              if (L->contains(PredBB)) {
                KeyBB = PredBB;
              } else {
                KeyBB = findUniqueInLoopPred(PredBB);
              }

              if (!KeyBB)
                continue;

              if (KeyBB == Latch && !InitV) {
                InitV = VIn;
                continue;
              }

              if (KeyBB != Latch) {
                PendingStores.push_back(std::make_pair(KeyBB, VIn));
              }
            }

            if (!InitV) {
              // Fallback: use any non-loop incoming as the initial value.
              for (unsigned In = 0; In < Phi->getNumIncomingValues(); ++In) {
                BasicBlock *PredBB = Phi->getIncomingBlock(In);
                if (!PredBB || L->contains(PredBB))
                  continue;
                InitV = Phi->getIncomingValue(In);
                break;
              }
            }
            if (!InitV) {
              reject("exit_phi_no_init_incoming");
              return false;
            }

            if (!isa<Constant>(InitV)) {
              auto *II = dyn_cast<Instruction>(InitV);
              if (!II || (II->getParent() != Preheader &&
                          II->getParent() != &F.getEntryBlock())) {
                reject("exit_phi_init_not_dominating");
                return false;
              }
            }
            auto *Slot = EB.CreateAlloca(Ty, nullptr, "linx.simt.exitphi");
            PB.CreateStore(InitV, Slot);

            Value *SlotI64 = PB.CreatePtrToInt(Slot, I64Ty);
            auto Bind = bindI64(SlotI64);
            if (!Bind) {
              reject("exit_phi_bind_exhausted");
              return false;
            }

            for (auto &Pair : PendingStores) {
              BasicBlock *KeyBB = Pair.first;
              Value *VIn = Pair.second;
              if (!KeyBB || KeyBB == Latch)
                continue;
              ExitPhiStoresByBlock[KeyBB].push_back(
                  std::make_pair(*Bind, VIn));
            }

            ExitPhiPlan Plan;
            Plan.Phi = Phi;
            Plan.Slot = Slot;
            Plan.SlotBind = *Bind;
            ExitPhiPlans.push_back(std::move(Plan));

            // Note: incoming values for blocks keyed above cover all
            // non-latch internal exits. We intentionally do not attempt to
            // emit latch-completion stores (those values are represented by
            // InitV above).
          }
        }

        auto getOrCreateF32InductionPlan =
            [&](CastInst *Cast) -> std::optional<unsigned> {
          if (!Cast)
            return std::nullopt;
          auto It = F32InductionPlanByCast.find(Cast);
          if (It != F32InductionPlanByCast.end())
            return It->second;

          // Scalar-lane replay only: the plan carries a single scalar value
          // across "iterations" (groups). In grouped-lane mode, per-lane
          // values would diverge (we would need an int->float conversion op).
          if (LaneCount != 1)
            return std::nullopt;

          if (Cast->getType() != Type::getFloatTy(Ctx))
            return std::nullopt;
          const unsigned Opc = Cast->getOpcode();
          if (Opc != Instruction::SIToFP && Opc != Instruction::UIToFP)
            return std::nullopt;

          Value *Src = Cast->getOperand(0);
          while (auto *CI = dyn_cast_or_null<CastInst>(Src)) {
            switch (CI->getOpcode()) {
            case Instruction::Trunc:
            case Instruction::ZExt:
            case Instruction::SExt:
              Src = CI->getOperand(0);
              continue;
            default:
              break;
            }
            break;
          }
          if (!Src || !Src->getType()->isIntegerTy())
            return std::nullopt;

          const SCEV *S = SE.getSCEVAtScope(Src, L);
          const auto *AR = dyn_cast<SCEVAddRecExpr>(S);
          if (!AR || AR->getLoop() != L || !AR->isAffine())
            return std::nullopt;
          const auto *StartC = dyn_cast<SCEVConstant>(AR->getStart());
          const auto *StepC =
              dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
          if (!StartC || !StepC)
            return std::nullopt;
          const int64_t StartI = StartC->getAPInt().getSExtValue();
          const int64_t StepI = StepC->getAPInt().getSExtValue();
          if (StepI == 0)
            return std::nullopt;

          BasicBlock &EntryBB = F.getEntryBlock();
          Instruction *EntryIP = &*EntryBB.getFirstInsertionPt();
          IRBuilder<> EB(EntryIP);

          F32InductionPlan Plan;
          Plan.Cast = Cast;
          Plan.Start = StartI;
          Plan.Step = StepI;
          Plan.Slot = EB.CreateAlloca(Type::getFloatTy(Ctx), nullptr,
                                      "linx.simt.fiv");
          PB.CreateStore(ConstantFP::get(Type::getFloatTy(Ctx), (double)StartI),
                         Plan.Slot);
          Value *SlotI64 = PB.CreatePtrToInt(Plan.Slot, I64Ty);
          auto Bind = bindI64(SlotI64);
          if (!Bind)
            return std::nullopt;
          Plan.SlotBind = *Bind;

          const unsigned Idx = F32InductionPlans.size();
          F32InductionPlans.push_back(std::move(Plan));
          F32InductionPlanByCast[Cast] = Idx;
          return Idx;
        };

	        DenseMap<const PHINode *, unsigned> RecurrencePlanByPhi;
	        DenseMap<const Instruction *, SmallVector<unsigned, 2>>
	            RecurrencePlansByUpdate;
	        if (!RecurrencePlans.empty()) {
	          BasicBlock &EntryBB = F.getEntryBlock();
	          Instruction *EntryIP = &*EntryBB.getFirstInsertionPt();
	          IRBuilder<> EB(EntryIP);
	          for (unsigned RI = 0; RI < RecurrencePlans.size(); RI++) {
	            RecurrencePlan &Plan = RecurrencePlans[RI];
              if (!Plan.SlotTy) {
                reject("invalid_recurrence_slot_type");
                return false;
              }
	            Plan.Slot =
	                EB.CreateAlloca(Plan.SlotTy, nullptr, "linx.simt.rec");
              Value *InitStored = Plan.InitValue;
              if (!InitStored) {
                reject("invalid_recurrence_init");
                return false;
              }
              if (InitStored->getType() != Plan.SlotTy) {
                if (InitStored->getType()->isIntegerTy() &&
                    Plan.SlotTy->isIntegerTy()) {
                  InitStored = PB.CreateZExtOrTrunc(InitStored, Plan.SlotTy);
                } else if (InitStored->getType()->isFloatTy() &&
                           Plan.SlotTy->isFloatTy()) {
                  InitStored = PB.CreateFPCast(InitStored, Plan.SlotTy);
                } else {
                  reject("invalid_recurrence_init_cast");
                  return false;
                }
              }
	            PB.CreateStore(InitStored, Plan.Slot);
	            Value *SlotI64 = PB.CreatePtrToInt(Plan.Slot, I64Ty);
	            auto Bind = bindI64(SlotI64);
	            if (!Bind) {
	              reject("recurrence_bind_exhausted");
	              return false;
	            }
	            Plan.SlotBind = *Bind;
	            RecurrencePlanByPhi[Plan.Phi] = RI;
	            RecurrencePlansByUpdate[Plan.Update].push_back(RI);
	          }
	        }

	        struct LiveOutPlan {
	          Instruction *Inst = nullptr;
	          AllocaInst *Slot = nullptr;
	          unsigned SlotBind = 0;
	        };
	        SmallVector<LiveOutPlan, 8> LiveOutPlans;
	        if (!LiveOutInsts.empty()) {
	          BasicBlock &EntryBB = F.getEntryBlock();
	          Instruction *EntryIP = &*EntryBB.getFirstInsertionPt();
	          IRBuilder<> EB(EntryIP);
	          for (Instruction *I : LiveOutInsts) {
	            if (!I)
	              continue;
	            LiveOutPlan Plan;
	            Plan.Inst = I;
	            Plan.Slot = EB.CreateAlloca(I->getType(), nullptr,
	                                       "linx.simt.liveout");
	            Value *SlotI64 = PB.CreatePtrToInt(Plan.Slot, I64Ty);
	            auto Bind = bindI64(SlotI64);
	            if (!Bind) {
	              reject("liveout_bind_exhausted");
	              return false;
	            }
	            Plan.SlotBind = *Bind;
	            LiveOutPlans.push_back(std::move(Plan));
	          }
	        }

	        struct AddressBinding {
	          unsigned BaseRi;
	          int64_t IndexFactor;
	          unsigned Shift;
          int64_t StepElems;
        };

        auto bindPtrStart = [&](Value *Ptr) -> std::optional<AddressBinding> {
          Ptr = Ptr->stripPointerCasts();
          const SCEV *PointerExpr = SE.getSCEVAtScope(Ptr, L);
          const auto *AddRec = dyn_cast<SCEVAddRecExpr>(PointerExpr);
          if (!AddRec || AddRec->getLoop() != L || !AddRec->isAffine())
            return std::nullopt;
          const auto *StepConst =
              dyn_cast<SCEVConstant>(AddRec->getStepRecurrence(SE));
          if (!StepConst)
            return std::nullopt;
          int64_t StepBytes = StepConst->getAPInt().getSExtValue();
          if ((StepBytes % 4) != 0 || StepBytes == 0)
            return std::nullopt;
          const int64_t StepElems = StepBytes / 4;

          const SCEV *Start = AddRec->getStart();
          Value *StartV = ExpandedStarts.lookup(Start);
          if (!StartV) {
            StartV = Exp.expandCodeFor(Start, Start->getType(),
                                       Preheader->getTerminator());
            if (!StartV)
              return std::nullopt;
            ExpandedStarts[Start] = StartV;
          }
          Value *BaseI64 = nullptr;
          if (StartV->getType()->isPointerTy()) {
            BaseI64 = PB.CreatePtrToInt(StartV, I64Ty);
          } else if (StartV->getType()->isIntegerTy()) {
            BaseI64 = PB.CreateZExtOrTrunc(StartV, I64Ty);
          } else {
            return std::nullopt;
          }
          auto BaseOpt = bindI64(BaseI64);
          if (!BaseOpt)
            return std::nullopt;

          AddressBinding Binding = {/*BaseRi=*/ *BaseOpt,
                                    /*IndexFactor=*/ 0,
                                    /*Shift=*/ 0,
                                    /*StepElems=*/ StepElems};
          const int64_t Delta = StepBytes - 4;
          if (Delta == 0)
            return Binding;

          const uint64_t AbsDelta =
              Delta < 0 ? static_cast<uint64_t>(-Delta)
                        : static_cast<uint64_t>(Delta);
          const unsigned Shift = countr_zero(AbsDelta);
          if (Shift > 31)
            return std::nullopt;

          const int64_t Factor = Delta >> Shift;
          if (Factor == 0 || Factor > 4096 || Factor < -4096)
            return std::nullopt;
          Binding.IndexFactor = Factor;
          Binding.Shift = Shift;
          return Binding;
        };

	        unsigned NextVecReg = 0;
	        auto allocVec = [&]() -> std::optional<std::string> {
	          static constexpr unsigned kMaxIndex = 31;
	          static constexpr const char *kClassPrefix[] = {"vt#", "vu#", "vm#",
	                                                         "vn#"};
	          static constexpr unsigned kNumClasses =
	              sizeof(kClassPrefix) / sizeof(kClassPrefix[0]);
	          if (NextVecReg >= (kMaxIndex * kNumClasses))
	            return std::nullopt;
	          const unsigned Class = NextVecReg / kMaxIndex;
	          const unsigned Index = (NextVecReg % kMaxIndex) + 1u;
	          ++NextVecReg;
	          return std::string(kClassPrefix[Class]) + std::to_string(Index);
	        };

	        unsigned NextAsmLabel = 0;
	        auto freshAsmLabel = [&](StringRef Prefix) -> std::string {
	          std::string S;
	          raw_string_ostream SS(S);
	          SS << Prefix << NextAsmLabel++;
	          return SS.str();
	        };

	        struct PtrPhiPlan {
	          std::string SelReg; // Small integer selector in a vector register.
	          SmallVector<unsigned, 4> BaseRis; // sel_id -> base RI bind
	          DenseMap<const BasicBlock *, unsigned> SelByPred; // pred -> sel_id
	        };
	        DenseMap<const PHINode *, PtrPhiPlan> PtrPhiPlans;

        DenseMap<Value *, std::string> ValOp;
        SmallString<512> Body;
        raw_svector_ostream OS(Body);

        std::string LinearIndexReg = "lc0";
        const unsigned GroupShift =
            UseGroupedDims ? static_cast<unsigned>(Log2_64(LaneCount)) : 0u;
        if (UseGroupedDims) {
          auto Lin = allocVec();
          if (!Lin) {
            reject("vector_reg_exhausted");
            return false;
          }
          OS << "  v.add lc0, lc1<<" << GroupShift << ", ->" << *Lin << "\n";
          LinearIndexReg = *Lin;
        }

        DenseMap<int64_t, std::string> IndexRegByFactor;
        IndexRegByFactor[0] = "zero";
        IndexRegByFactor[1] = LinearIndexReg;
        std::optional<std::string> NegLc0Reg;
        DenseMap<int64_t, std::string> GroupedIndexRegByStepElems;

        std::function<std::optional<std::string>(int64_t)> emitScaledLc0 =
            [&](int64_t Factor) -> std::optional<std::string> {
          auto Cached = IndexRegByFactor.find(Factor);
          if (Cached != IndexRegByFactor.end())
            return Cached->second;

          if (Factor == -1) {
            auto NegReg = allocVec();
            if (!NegReg)
              return std::nullopt;
            OS << "  v.sub zero, " << LinearIndexReg << ", ->" << *NegReg
               << "\n";
            IndexRegByFactor[Factor] = *NegReg;
            return *NegReg;
          }

          const bool IsNegative = Factor < 0;
          const uint64_t AbsFactor =
              IsNegative ? static_cast<uint64_t>(-Factor)
                         : static_cast<uint64_t>(Factor);
          if (AbsFactor == 0)
            return std::string("zero");
          if (AbsFactor > 4096)
            return std::nullopt;

          DenseMap<unsigned, std::string> Pow2Regs;
          Pow2Regs[0] = LinearIndexReg;
          const unsigned HighestBit = Log2_64(AbsFactor);
          for (unsigned Bit = 1; Bit <= HighestBit; ++Bit) {
            auto Prev = Pow2Regs.find(Bit - 1);
            if (Prev == Pow2Regs.end())
              return std::nullopt;
            auto Next = allocVec();
            if (!Next)
              return std::nullopt;
            OS << "  v.add " << Prev->second << ", " << Prev->second << ", ->"
               << *Next << "\n";
            Pow2Regs[Bit] = *Next;
          }

          std::optional<std::string> AccumReg;
          for (unsigned Bit = 0; Bit <= HighestBit; ++Bit) {
            if (((AbsFactor >> Bit) & 1u) == 0)
              continue;
            auto Part = Pow2Regs.find(Bit);
            if (Part == Pow2Regs.end())
              return std::nullopt;
            if (!AccumReg) {
              AccumReg = Part->second;
              continue;
            }
            auto Sum = allocVec();
            if (!Sum)
              return std::nullopt;
            OS << "  v.add " << *AccumReg << ", " << Part->second << ", ->"
               << *Sum << "\n";
            AccumReg = *Sum;
          }

          if (!AccumReg)
            return std::nullopt;

          if (IsNegative) {
            auto NegReg = allocVec();
            if (!NegReg)
              return std::nullopt;
            OS << "  v.sub zero, " << *AccumReg << ", ->" << *NegReg << "\n";
            AccumReg = *NegReg;
          }

          IndexRegByFactor[Factor] = *AccumReg;
          return *AccumReg;
        };

        auto emitGroupedIndexReg =
            [&](int64_t StepElems) -> std::optional<std::string> {
          auto Cached = GroupedIndexRegByStepElems.find(StepElems);
          if (Cached != GroupedIndexRegByStepElems.end())
            return Cached->second;

          auto StepScaled = emitScaledLc0(StepElems);
          if (!StepScaled)
            return std::nullopt;
          auto Idx = allocVec();
          if (!Idx)
            return std::nullopt;
          OS << "  v.sub " << *StepScaled << ", lc0, ->" << *Idx << "\n";
          GroupedIndexRegByStepElems[StepElems] = *Idx;
          return *Idx;
        };

        auto emitNegLc0 = [&]() -> std::optional<std::string> {
          if (NegLc0Reg)
            return NegLc0Reg;
          auto Neg = allocVec();
          if (!Neg)
            return std::nullopt;
          OS << "  v.sub zero, lc0, ->" << *Neg << "\n";
          NegLc0Reg = *Neg;
          return NegLc0Reg;
        };

	        auto emitIndexDeltaFromLc0 =
	            [&](StringRef IndexExpr) -> std::optional<std::string> {
	          StringRef Expr = IndexExpr.trim();
	          if (Expr == "lc0")
	            return std::string("zero");
	          if (Expr == "zero")
	            return emitNegLc0();
	          auto Delta = allocVec();
	          if (!Delta)
	            return std::nullopt;
	          OS << "  v.sub " << Expr << ", lc0, ->" << *Delta << "\n";
	          return *Delta;
	        };

	        // Convert a byte-based induction/index expression (e.g. i8 GEP index)
	        // into a word index suitable for v.lw/v.sw (which operate on 32-bit
	        // elements and use lc0<<2 addressing).
	        auto emitWordIndexFromByteIndex =
	            [&](Value *ByteIndex) -> std::optional<std::string> {
	          if (!ByteIndex)
	            return std::nullopt;
	          if (!ByteIndex->getType()->isIntegerTy() ||
	              ByteIndex->getType()->getScalarSizeInBits() > 64) {
	            return std::nullopt;
	          }

	          const SCEV *IS = SE.getSCEVAtScope(ByteIndex, L);
	          const auto *AR = dyn_cast<SCEVAddRecExpr>(IS);
	          if (!AR || AR->getLoop() != L || !AR->isAffine())
	            return std::nullopt;
	          const auto *StartC = dyn_cast<SCEVConstant>(AR->getStart());
	          const auto *StepC =
	              dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
	          if (!StartC || !StepC)
	            return std::nullopt;

	          const int64_t StartB = StartC->getAPInt().getSExtValue();
	          const int64_t StepB = StepC->getAPInt().getSExtValue();
	          if ((StartB % 4) != 0 || (StepB % 4) != 0)
	            return std::nullopt;

	          const int64_t StartW = StartB / 4;
	          const int64_t StepW = StepB / 4;
	          if (StepW == 0)
	            return std::nullopt;
	          if (StepW > 4096 || StepW < -4096)
	            return std::nullopt;

	          std::optional<std::string> ScaledIndex;
	          if (StepW == 1) {
	            ScaledIndex = LinearIndexReg;
	          } else {
	            ScaledIndex = emitScaledLc0(StepW);
	          }
	          if (!ScaledIndex)
	            return std::nullopt;

	          if (StartW == 0)
	            return *ScaledIndex;

	          auto *C64 = ConstantInt::get(I64Ty, (uint64_t)StartW);
	          auto Bind = bindI64(C64);
	          if (!Bind)
	            return std::nullopt;
	          std::string StartTok = "ri" + std::to_string(*Bind);

	          auto Dst = allocVec();
	          if (!Dst)
	            return std::nullopt;
	          OS << "  v.add " << *ScaledIndex << ", " << StartTok << ", ->"
	             << *Dst << "\n";
	          return *Dst;
	        };

	        std::function<std::optional<std::string>(Value *)> emitValue;
	        std::function<std::optional<std::string>(Value *)> emitCondition;
	        std::function<std::optional<std::string>(Value *)> emitF32;

	        auto emitIntegerAffineAddRecValue =
	            [&](Value *IV, bool EdgeFresh) -> std::optional<std::string> {
	              if (!IV || !IV->getType()->isIntegerTy() ||
	                  IV->getType()->getScalarSizeInBits() > 64) {
	                return std::nullopt;
	              }

	              if (!EdgeFresh) {
	                auto It = ValOp.find(IV);
	                if (It != ValOp.end())
	                  return It->second;
	              }

	              const SCEV *PS = SE.getSCEVAtScope(IV, L);
	              const auto *AR = dyn_cast<SCEVAddRecExpr>(PS);
	              if (!AR || AR->getLoop() != L || !AR->isAffine()) {
	                return std::nullopt;
	              }
	              const SCEV *StartS = AR->getStart();
	              const SCEV *StepS = AR->getStepRecurrence(SE);
	              if (!StartS || !StepS)
	                return std::nullopt;

	              // Prefer constant-step lowering when available; fall back to a
	              // vector multiply for dynamic step values.
	              std::optional<int64_t> StepConst;
	              if (auto *StepC = dyn_cast<SCEVConstant>(StepS)) {
	                StepConst = StepC->getAPInt().getSExtValue();
	                if (*StepConst == 0)
	                  return std::nullopt;
	                if (*StepConst > 4096 || *StepConst < -4096)
	                  StepConst.reset();
	              }

	              std::optional<std::string> ScaledIndex;
	              if (StepConst) {
	                if (*StepConst == 1) {
	                  ScaledIndex = LinearIndexReg;
	                } else {
	                  ScaledIndex = emitScaledLc0(*StepConst);
	                }
	              } else {
	                Value *StepV = Exp.expandCodeFor(StepS, StepS->getType(),
	                                                 Preheader->getTerminator());
	                if (!StepV)
	                  return std::nullopt;
	                if (!StepV->getType()->isIntegerTy())
	                  return std::nullopt;
	                if (StepV->getType()->getScalarSizeInBits() > 64)
	                  return std::nullopt;
	                if (StepV->getType() != I64Ty)
	                  StepV = PB.CreateSExtOrTrunc(StepV, I64Ty);
	                auto StepTok = emitValue(StepV);
	                if (!StepTok)
	                  return std::nullopt;
	                auto Mul = allocVec();
	                if (!Mul)
	                  return std::nullopt;
	                OS << "  v.mul " << LinearIndexReg << ", " << *StepTok
	                   << ", ->" << *Mul << "\n";
	                ScaledIndex = *Mul;
	              }
	              if (!ScaledIndex)
	                return std::nullopt;

	              std::optional<int64_t> StartConst;
	              if (auto *StartC = dyn_cast<SCEVConstant>(StartS))
	                StartConst = StartC->getAPInt().getSExtValue();

	              if (StartConst && *StartConst == 0) {
	                if (!EdgeFresh)
	                  ValOp[IV] = *ScaledIndex;
	                return *ScaledIndex;
	              }

	              std::optional<std::string> StartTok;
	              if (StartConst) {
	                auto *C64 = ConstantInt::get(I64Ty, (uint64_t)*StartConst);
	                auto Bind = bindI64(C64);
	                if (!Bind)
	                  return std::nullopt;
	                StartTok = "ri" + std::to_string(*Bind);
	              } else {
	                Value *StartV = Exp.expandCodeFor(StartS, StartS->getType(),
	                                                  Preheader->getTerminator());
	                if (!StartV)
	                  return std::nullopt;
	                if (!StartV->getType()->isIntegerTy())
	                  return std::nullopt;
	                if (StartV->getType()->getScalarSizeInBits() > 64)
	                  return std::nullopt;
	                if (StartV->getType() != I64Ty)
	                  StartV = PB.CreateSExtOrTrunc(StartV, I64Ty);
	                StartTok = emitValue(StartV);
	              }
	              if (!StartTok)
	                return std::nullopt;

	              auto Dst = allocVec();
	              if (!Dst)
	                return std::nullopt;
	              OS << "  v.add " << *ScaledIndex << ", " << *StartTok << ", ->"
	                 << *Dst << "\n";
	              if (!EdgeFresh)
	                ValOp[IV] = *Dst;
	              return *Dst;
	            };

		        auto bindPtrGeneral = [&](Value *Ptr)
		            -> std::optional<std::pair<unsigned, std::string>> {
		          Ptr = Ptr->stripPointerCasts();
		          if (auto *GEP = dyn_cast<GEPOperator>(Ptr)) {
		            auto Try = [&]() -> std::optional<std::pair<unsigned, std::string>> {
		              // Accept both the canonical pointer GEP form:
		              //   gep <elt>, <ptr>, <idx>
		              // and the common global-array form:
		              //   gep [N x <elt>], <ptr>, 0, <idx>
		              // TSVC frequently uses the latter for global arrays.
		              Value *Index = nullptr;
		              const unsigned NumIdx = GEP->getNumIndices();
		              if (NumIdx == 1) {
		                Index = GEP->getOperand(1);
		              } else if (NumIdx == 2) {
		                auto *Z = dyn_cast<ConstantInt>(GEP->getOperand(1));
		                if (!Z || !Z->isZero())
		                  return std::nullopt;
		                Index = GEP->getOperand(2);
		              } else {
		                return std::nullopt;
		              }

		              Type *ElemTy = GEP->getResultElementType();
		              if (!ElemTy || !(ElemTy->isFloatTy() || ElemTy->isIntegerTy(32)))
		                return std::nullopt;
		              const DataLayout &DL = F.getParent()->getDataLayout();
		              if (DL.getTypeStoreSize(ElemTy) != 4)
		                return std::nullopt;

		              Value *BasePtr = GEP->getPointerOperand()->stripPointerCasts();
		              if (!L->isLoopInvariant(BasePtr))
		                return std::nullopt;
		              Value *BaseI64 = PB.CreatePtrToInt(BasePtr, I64Ty);
		              auto BaseOpt = bindI64(BaseI64);
		              if (!BaseOpt)
		                return std::nullopt;

		              if (!Index || !Index->getType()->isIntegerTy())
		                return std::nullopt;
		              if (Index->getType()->getScalarSizeInBits() > 64)
		                return std::nullopt;

		              // Keep loop-variant casts inside the body emission rather than
		              // inserting them in the IR preheader (which may not dominate the
		              // value definition).
		              auto IdxExpr = emitValue(Index);
		              if (!IdxExpr)
		                return std::nullopt;
		              auto DeltaExpr = emitIndexDeltaFromLc0(*IdxExpr);
		              if (!DeltaExpr)
		                return std::nullopt;
		              return std::make_pair(*BaseOpt, *DeltaExpr);
		            };

		            if (auto Res = Try())
		              return Res;
		          }

		          // Pointer induction variable: accept affine AddRec pointers even
		          // when the step is dynamic (e.g. i += inc).
	          const SCEV *PtrS = SE.getSCEVAtScope(Ptr, L);
	          const auto *AR = dyn_cast<SCEVAddRecExpr>(PtrS);
	          if (!AR || AR->getLoop() != L || !AR->isAffine())
	            return std::nullopt;

	          const SCEV *Start = AR->getStart();
	          Value *StartV = ExpandedStarts.lookup(Start);
	          if (!StartV) {
	            StartV = Exp.expandCodeFor(Start, Start->getType(),
	                                       Preheader->getTerminator());
	            if (!StartV)
	              return std::nullopt;
	            ExpandedStarts[Start] = StartV;
	          }
	          Value *BaseI64 = nullptr;
	          if (StartV->getType()->isPointerTy()) {
	            BaseI64 = PB.CreatePtrToInt(StartV, I64Ty);
	          } else if (StartV->getType()->isIntegerTy()) {
	            BaseI64 = PB.CreateZExtOrTrunc(StartV, I64Ty);
	          } else {
	            return std::nullopt;
	          }
	          auto BaseOpt = bindI64(BaseI64);
	          if (!BaseOpt)
	            return std::nullopt;

	          const SCEV *StepS = AR->getStepRecurrence(SE);
	          if (!StepS)
	            return std::nullopt;
	          Value *StepBytesV =
	              Exp.expandCodeFor(StepS, StepS->getType(),
	                                Preheader->getTerminator());
	          if (!StepBytesV || !StepBytesV->getType()->isIntegerTy())
	            return std::nullopt;
	          if (StepBytesV->getType() != I64Ty)
	            StepBytesV = PB.CreateSExtOrTrunc(StepBytesV, I64Ty);
	          Value *StepElemsV =
	              PB.CreateAShr(StepBytesV, ConstantInt::get(I64Ty, 2));
	          auto StepTok = emitValue(StepElemsV);
	          if (!StepTok)
	            return std::nullopt;
	          auto Mul = allocVec();
	          auto Idx = allocVec();
	          if (!Mul || !Idx)
	            return std::nullopt;
	          OS << "  v.mul " << LinearIndexReg << ", " << *StepTok << ", ->"
	             << *Mul << "\n";
	          OS << "  v.sub " << *Mul << ", lc0, ->" << *Idx << "\n";
	          return std::make_pair(*BaseOpt, *Idx);
	        };

        auto unsupportedValueReason = [&](Value *V) -> std::string {
          if (auto *I = dyn_cast<Instruction>(V)) {
            if (auto *CB = dyn_cast<CallBase>(I)) {
              if (Function *Callee = CB->getCalledFunction()) {
                if (Callee->isIntrinsic())
                  return ("unsupported_value_expr:intrinsic:" +
                          Callee->getName())
                      .str();
                return ("unsupported_value_expr:call:" + Callee->getName())
                    .str();
              }
              return "unsupported_value_expr:indirect_call";
            }
            return ("unsupported_value_expr:" + StringRef(I->getOpcodeName()))
                .str();
          }
          if (isa<Argument>(V))
            return "unsupported_value_expr:arg";
          return "unsupported_value_expr:unknown";
        };

        std::function<std::optional<std::string>(Value *)> emitLoadFromPtr;
        emitLoadFromPtr = [&](Value *Ptr) -> std::optional<std::string> {
          auto Address = bindPtrStart(Ptr);
          unsigned BaseRi = 0;
          std::string IndexReg;
          unsigned IndexShift = 0;
          if (Address) {
            BaseRi = Address->BaseRi;
            if (UseGroupedDims) {
              // In grouped mode, compute the full step in elements:
              //   addr = base + (lc0<<2) + ((stepElems*linearIndex - lc0)<<2)
              // This preserves the (lane + group*LaneCount) addressing for
              // both unit and non-unit stride patterns.
              auto Idx = emitGroupedIndexReg(Address->StepElems);
              if (!Idx)
                return std::nullopt;
              IndexReg = *Idx;
              IndexShift = 2;
            } else {
              // Avoid negative-stride encoding patterns that rely on mixed
              // shifts (e.g. lc0<<2 + (-lc0)<<3). Use the stepElems form
              // instead to keep the scale uniform at <<2.
              if (Address->StepElems < 0) {
                auto Idx = emitGroupedIndexReg(Address->StepElems);
                if (!Idx)
                  return std::nullopt;
                IndexReg = *Idx;
                IndexShift = 2;
              } else {
                IndexShift = Address->Shift;
                auto IndexRegOpt = emitScaledLc0(Address->IndexFactor);
                if (!IndexRegOpt)
                  return std::nullopt;
                IndexReg = *IndexRegOpt;
              }
            }
          } else {
	            auto General = bindPtrGeneral(Ptr);
	            if (!General) {
	              Value *Stripped = Ptr ? Ptr->stripPointerCasts() : nullptr;
	              auto *GEP = dyn_cast_or_null<GEPOperator>(Stripped);
	              if (GEP) {
	                Value *Base = GEP->getPointerOperand()->stripPointerCasts();
	                if (auto *Phi = dyn_cast<PHINode>(Base)) {
		                  auto PlanIt = PtrPhiPlans.find(Phi);
		                  if (PlanIt != PtrPhiPlans.end()) {
		                    // Pointer sink PHI: dispatch load based on the selector
		                    // written on the incoming edge.
		                    Value *Index = nullptr;
		                    const unsigned NumIdx = GEP->getNumIndices();
		                    if (NumIdx == 1) {
		                      Index = GEP->getOperand(1);
	                    } else if (NumIdx == 2) {
	                      auto *Z = dyn_cast<ConstantInt>(GEP->getOperand(1));
	                      if (!Z || !Z->isZero())
	                        return std::nullopt;
	                      Index = GEP->getOperand(2);
	                    } else {
	                      return std::nullopt;
	                    }
		                    if (!Index || !Index->getType()->isIntegerTy() ||
		                        Index->getType()->getScalarSizeInBits() > 64) {
		                      return std::nullopt;
		                    }

		                    const DataLayout &DL =
		                        F.getParent()->getDataLayout();
		                    Type *ElemTy = GEP->getResultElementType();
		                    const uint64_t ElemBytes =
		                        ElemTy ? DL.getTypeStoreSize(ElemTy) : 0;

		                    std::optional<std::string> IdxExpr;
		                    if (ElemBytes == 4) {
		                      IdxExpr = emitValue(Index);
		                    } else if (ElemBytes == 1) {
		                      IdxExpr = emitWordIndexFromByteIndex(Index);
		                    } else {
		                      return std::nullopt;
		                    }
		                    if (!IdxExpr)
		                      return std::nullopt;

		                    auto DeltaExpr = emitIndexDeltaFromLc0(*IdxExpr);
		                    if (!DeltaExpr)
		                      return std::nullopt;
		                    auto Dst = allocVec();
	                    if (!Dst)
	                      return std::nullopt;

	                    PtrPhiPlan &Plan = PlanIt->second;
	                    if (Plan.BaseRis.empty())
	                      return std::nullopt;

	                    const std::string EndLbl = freshAsmLabel("L_ptrphi_end");
	                    SmallVector<std::pair<std::string, unsigned>, 4> CaseLabels;
	                    CaseLabels.reserve(Plan.BaseRis.size());
	                    for (unsigned SelId = 0; SelId < Plan.BaseRis.size();
	                         ++SelId) {
	                      std::string CaseLbl = freshAsmLabel("L_ptrphi_case");
	                      CaseLabels.push_back(std::make_pair(CaseLbl, SelId));

	                      std::string SelTok = "zero";
	                      if (SelId != 0) {
	                        auto Tok =
	                            emitValue(ConstantInt::get(I64Ty, SelId));
	                        if (!Tok)
	                          return std::nullopt;
	                        SelTok = *Tok;
	                      }

		                      auto Pred = allocVec();
		                      if (!Pred)
		                        return std::nullopt;
			                      OS << "  v.cmp.eq " << Plan.SelReg << ", " << SelTok
			                         << ", ->" << *Pred << "\n";
			                      // Reduce ops accumulate into the destination register; seed
			                      // our scratch reduce destination before each use.
			                      // NOTE: C.MOVR cannot write to a specific t#k entry; it can
			                      // only push to `t`/`u` or write a global GPR.
			                      OS << "  c.movr zero, ->t\n";
			                      OS << "  v.rdor " << *Pred << ", ->t#1\n";
			                      OS << "  b.ne t#1, zero, " << CaseLbl << "\n";
			                    }

	                    OS << "  j " << CaseLabels.front().first << "\n";
	                    for (auto &C : CaseLabels) {
	                      const unsigned SelId = C.second;
	                      if (SelId >= Plan.BaseRis.size())
	                        return std::nullopt;
	                      const unsigned Ri = Plan.BaseRis[SelId];
	                      OS << C.first << ":\n";
	                      OS << "  v.lw.brg [ri" << Ri << ", lc0<<2, "
	                         << *DeltaExpr << "<<2], ->" << *Dst << "\n";
	                      OS << "  j " << EndLbl << "\n";
	                    }
	                    OS << EndLbl << ":\n";
	                    return *Dst;
	                  }
	                }
	              }
	              if (GEP && GEP->getNumIndices() == 1) {
	                Value *Base = GEP->getPointerOperand()->stripPointerCasts();
	                auto *Sel = dyn_cast<SelectInst>(Base);
	                if (Sel && Sel->getType()->isPointerTy()) {
                  auto IdxIt = GEP->idx_begin();
                  Value *Index =
                      (IdxIt == GEP->idx_end()) ? nullptr : IdxIt->get();
                  if (Index) {
                    Value *TruePtr = PB.CreateGEP(
                        GEP->getSourceElementType(), Sel->getTrueValue(), Index);
                    Value *FalsePtr = PB.CreateGEP(
                        GEP->getSourceElementType(), Sel->getFalseValue(), Index);
                    auto Pred = emitCondition(Sel->getCondition());
                    auto TV = emitLoadFromPtr(TruePtr);
                    auto FV = emitLoadFromPtr(FalsePtr);
                    if (Pred && TV && FV) {
                      auto Dst = allocVec();
                      if (!Dst)
                        return std::nullopt;
                      OS << "  v.csel " << *Pred << ", " << *TV << ", " << *FV
                         << ", ->" << *Dst << "\n";
                      return *Dst;
                    }
                  }
                }
              }
              return std::nullopt;
            }
            BaseRi = General->first;
            IndexReg = General->second;
            IndexShift = 2;
          }
          auto Dst = allocVec();
          if (!Dst)
            return std::nullopt;
          OS << "  v.lw.brg [ri" << BaseRi << ", lc0<<2, " << IndexReg;
          if (IndexShift)
            OS << "<<" << IndexShift;
          OS << "], ->" << *Dst << "\n";
          return *Dst;
        };

        auto emitLoadFromInvariantPtr =
            [&](Value *Ptr) -> std::optional<std::string> {
          if (!Ptr)
            return std::nullopt;
          Value *PtrI64 = PB.CreatePtrToInt(Ptr->stripPointerCasts(), I64Ty);
          auto Base = bindI64(PtrI64);
          if (!Base)
            return std::nullopt;
          auto Neg = emitNegLc0();
          if (!Neg)
            return std::nullopt;
          auto Dst = allocVec();
          if (!Dst)
            return std::nullopt;
          OS << "  v.lw.brg [ri" << *Base << ", lc0<<2, " << *Neg
             << "<<2], ->" << *Dst << "\n";
          return *Dst;
        };

        auto emitLoadFromInvariantBind =
            [&](unsigned BaseRi) -> std::optional<std::string> {
          auto Neg = emitNegLc0();
          if (!Neg)
            return std::nullopt;
          auto Dst = allocVec();
          if (!Dst)
            return std::nullopt;
          OS << "  v.lw.brg [ri" << BaseRi << ", lc0<<2, " << *Neg
             << "<<2], ->" << *Dst << "\n";
          return *Dst;
        };

        auto emitStoreToInvariantBind = [&](StringRef Src, unsigned BaseRi) {
          auto Neg = emitNegLc0();
          if (!Neg)
            return false;
          OS << "  v.sw.brg " << Src << ", [ri" << BaseRi << ", lc0<<2, "
             << *Neg << "<<2]\n";
          return true;
        };

        auto canScalarizeInvariantLoad = [&](const LoadInst *LI) -> bool {
          if (!L->isLoopInvariant(LI->getPointerOperand())) {
            return false;
          }
          const SCEV *LoadS =
              SE.getSCEVAtScope(SE.getSCEV(const_cast<Value *>(LI->getPointerOperand())), L);
          for (StoreInst *SI : Stores) {
            const SCEV *StoreS = SE.getSCEVAtScope(SI->getPointerOperand(), L);
            if (!SE.isKnownPredicate(CmpInst::ICMP_NE, LoadS, StoreS)) {
              return false;
            }
          }
          return true;
        };

	        emitCondition = [&](Value *Cond) -> std::optional<std::string> {
          if (auto *CI = dyn_cast<ConstantInt>(Cond)) {
            if (CI->isZero()) {
              ValOp[Cond] = "zero";
              return "zero";
            }
            auto *C64 = ConstantInt::get(I64Ty, CI->getZExtValue());
            auto Bind = bindI64(C64);
            if (!Bind)
              return std::nullopt;
            std::string Name = "ri" + std::to_string(*Bind);
            ValOp[Cond] = Name;
            return Name;
          }

          auto It = ValOp.find(Cond);
          if (It != ValOp.end())
            return It->second;

          if (auto *SI = dyn_cast<SelectInst>(Cond)) {
            if (!SI->getType()->isIntegerTy(1))
              return std::nullopt;
            auto Pred = emitCondition(SI->getCondition());
            auto TV = emitCondition(SI->getTrueValue());
            auto FV = emitCondition(SI->getFalseValue());
            if (!Pred || !TV || !FV)
              return std::nullopt;
            auto Dst = allocVec();
            if (!Dst)
              return std::nullopt;
            OS << "  v.csel " << *Pred << ", " << *TV << ", " << *FV << ", ->"
               << *Dst << "\n";
            ValOp[Cond] = *Dst;
            return *Dst;
          }

          if (auto *Cmp = dyn_cast<ICmpInst>(Cond)) {
            auto Lhs = emitValue(Cmp->getOperand(0));
            auto Rhs = emitValue(Cmp->getOperand(1));
            if (!Lhs || !Rhs)
              return std::nullopt;

            auto Dst = allocVec();
            if (!Dst)
              return std::nullopt;

            StringRef Mn;
            std::string A = *Lhs;
            std::string B = *Rhs;
            switch (Cmp->getPredicate()) {
            case CmpInst::ICMP_EQ:
              Mn = "v.cmp.eq";
              break;
            case CmpInst::ICMP_NE:
              Mn = "v.cmp.ne";
              break;
            case CmpInst::ICMP_SLT:
              Mn = "v.cmp.lt";
              break;
            case CmpInst::ICMP_SLE:
              Mn = "v.cmp.ge";
              std::swap(A, B);
              break;
            case CmpInst::ICMP_SGT:
              Mn = "v.cmp.lt";
              std::swap(A, B);
              break;
            case CmpInst::ICMP_SGE:
              Mn = "v.cmp.ge";
              break;
            case CmpInst::ICMP_ULT:
              Mn = "v.cmp.ltu";
              break;
            case CmpInst::ICMP_ULE:
              Mn = "v.cmp.geu";
              std::swap(A, B);
              break;
            case CmpInst::ICMP_UGT:
              Mn = "v.cmp.ltu";
              std::swap(A, B);
              break;
            case CmpInst::ICMP_UGE:
              Mn = "v.cmp.geu";
              break;
            default:
              return std::nullopt;
            }

            OS << "  " << Mn << " " << A << ", " << B << ", ->" << *Dst
               << "\n";
            ValOp[Cond] = *Dst;
            return *Dst;
          }

          auto *FCmp = dyn_cast<FCmpInst>(Cond);
          if (!FCmp)
            return std::nullopt;

          auto Lhs = emitValue(FCmp->getOperand(0));
          auto Rhs = emitValue(FCmp->getOperand(1));
          if (!Lhs || !Rhs)
            return std::nullopt;

          auto Dst = allocVec();
          if (!Dst)
            return std::nullopt;

          StringRef Mn;
          std::string A = *Lhs;
          std::string B = *Rhs;
          switch (FCmp->getPredicate()) {
          case CmpInst::FCMP_OEQ:
          case CmpInst::FCMP_UEQ:
            Mn = "v.feq";
            break;
          case CmpInst::FCMP_ONE:
          case CmpInst::FCMP_UNE:
            Mn = "v.fne";
            break;
          case CmpInst::FCMP_OLT:
          case CmpInst::FCMP_ULT:
            Mn = "v.flt";
            break;
          case CmpInst::FCMP_OLE:
          case CmpInst::FCMP_ULE:
            Mn = "v.fge";
            std::swap(A, B);
            break;
          case CmpInst::FCMP_OGT:
          case CmpInst::FCMP_UGT:
            Mn = "v.flt";
            std::swap(A, B);
            break;
          case CmpInst::FCMP_OGE:
          case CmpInst::FCMP_UGE:
            Mn = "v.fge";
            break;
          default:
            return std::nullopt;
          }

          OS << "  " << Mn << " " << A << ", " << B << ", ->" << *Dst << "\n";
          ValOp[Cond] = *Dst;
          return *Dst;
	        };

	        emitF32 = [&](Value *V) -> std::optional<std::string> {
	          if (!V)
	            return std::nullopt;
	          if (V->getType() == Type::getFloatTy(Ctx))
	            return emitValue(V);
	          if (!V->getType()->isDoubleTy())
	            return std::nullopt;

	          auto It = ValOp.find(V);
	          if (It != ValOp.end())
	            return It->second;

	          if (auto *Cast = dyn_cast<CastInst>(V)) {
	            if (Cast->getOpcode() == Instruction::FPExt &&
	                Cast->getOperand(0)->getType() == Type::getFloatTy(Ctx)) {
	              auto Tok = emitValue(Cast->getOperand(0));
	              if (Tok)
	                ValOp[V] = *Tok;
	              return Tok;
	            }
	          }

	          if (auto *CF = dyn_cast<ConstantFP>(V)) {
	            APFloat F = CF->getValueAPF();
	            bool LosesInfo = false;
	            F.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven,
	                      &LosesInfo);
	            APInt Bits = F.bitcastToAPInt();
	            if (Bits.getBitWidth() != 32)
	              return std::nullopt;
	            const uint64_t U = Bits.getZExtValue();
	            if (U == 0) {
	              ValOp[V] = "zero";
	              return "zero";
	            }
	            auto *CI = ConstantInt::get(I64Ty, U);
	            auto Bind = bindI64(CI);
	            if (!Bind)
	              return std::nullopt;
	            std::string Name = "ri" + std::to_string(*Bind);
	            ValOp[V] = Name;
	            return Name;
	          }

	          if (auto *BO = dyn_cast<BinaryOperator>(V)) {
	            unsigned Opc = BO->getOpcode();
	            if (Opc == Instruction::FAdd || Opc == Instruction::FSub ||
	                Opc == Instruction::FMul || Opc == Instruction::FDiv) {
	              auto Lhs = emitF32(BO->getOperand(0));
	              auto Rhs = emitF32(BO->getOperand(1));
	              if (!Lhs || !Rhs)
	                return std::nullopt;
	              auto Dst = allocVec();
	              if (!Dst)
	                return std::nullopt;
	              StringRef Mn = (Opc == Instruction::FAdd)   ? "v.fadd"
	                            : (Opc == Instruction::FSub) ? "v.fsub"
	                            : (Opc == Instruction::FMul) ? "v.fmul"
	                                                         : "v.fdiv";
	              OS << "  " << Mn << " " << *Lhs << ", " << *Rhs << ", ->" << *Dst
	                 << "\n";
	              ValOp[V] = *Dst;
	              return *Dst;
	            }
	          }

	          return std::nullopt;
	        };

	        emitValue = [&](Value *V) -> std::optional<std::string> {
	              if (!V)
	                return std::nullopt;
	              auto It = ValOp.find(V);
              if (It != ValOp.end())
                return It->second;

	          if (auto *PN = dyn_cast<PHINode>(V)) {
	            auto RecIt = RecurrencePlanByPhi.find(PN);
	            if (RecIt != RecurrencePlanByPhi.end()) {
	              const RecurrencePlan &Plan = RecurrencePlans[RecIt->second];
	              auto Dst = emitLoadFromInvariantBind(Plan.SlotBind);
	              if (!Dst)
	                return std::nullopt;
	              ValOp[V] = *Dst;
	              return *Dst;
	            }

	            auto tryEmitPhiSelectCsel = [&]() -> std::optional<std::string> {
	              if (PN->getNumIncomingValues() != 2)
	                return std::nullopt;

	              auto tryEmitPhiSelect = [&](BasicBlock *BranchBB,
	                                          BasicBlock *OtherBB)
	                  -> std::optional<std::string> {
	                if (!BranchBB || !OtherBB || BranchBB == OtherBB)
	                  return std::nullopt;
	                auto *BI = dyn_cast<BranchInst>(BranchBB->getTerminator());
	                auto *OBI = dyn_cast<BranchInst>(OtherBB->getTerminator());
	                if (!BI || !BI->isConditional() || BI->getNumSuccessors() != 2)
	                  return std::nullopt;
	                if (!OBI || OBI->isConditional() || OBI->getNumSuccessors() != 1)
	                  return std::nullopt;
	                if (OBI->getSuccessor(0) != PN->getParent())
	                  return std::nullopt;

	                const bool TrueToMerge = (BI->getSuccessor(0) == PN->getParent());
	                const bool FalseToMerge = (BI->getSuccessor(1) == PN->getParent());
	                if (!(TrueToMerge || FalseToMerge))
	                  return std::nullopt;

	                if (TrueToMerge) {
	                  if (BI->getSuccessor(1) != OtherBB)
	                    return std::nullopt;
	                } else {
	                  if (BI->getSuccessor(0) != OtherBB)
	                    return std::nullopt;
	                }

	                Value *VTrue = PN->getIncomingValueForBlock(
	                    TrueToMerge ? BranchBB : OtherBB);
	                Value *VFalse = PN->getIncomingValueForBlock(
	                    TrueToMerge ? OtherBB : BranchBB);
	                if (!VTrue || !VFalse || VTrue == PN || VFalse == PN)
	                  return std::nullopt;

	                auto Pred = emitCondition(BI->getCondition());
	                auto TV = emitValue(VTrue);
	                auto FV = emitValue(VFalse);
	                if (!Pred || !TV || !FV)
	                  return std::nullopt;

	                auto Dst = allocVec();
	                if (!Dst)
	                  return std::nullopt;
	                OS << "  v.csel " << *Pred << ", " << *TV << ", " << *FV
	                   << ", ->" << *Dst << "\n";
	                return *Dst;
	              };

	              auto tryEmitPhiSelectViaSplit = [&](BasicBlock *TruePred,
	                                                  BasicBlock *FalsePred)
	                  -> std::optional<std::string> {
	                if (!TruePred || !FalsePred || TruePred == FalsePred)
	                  return std::nullopt;
	                auto *TPredBI = dyn_cast<BranchInst>(TruePred->getTerminator());
	                auto *FPredBI = dyn_cast<BranchInst>(FalsePred->getTerminator());
	                if (!TPredBI || !FPredBI)
	                  return std::nullopt;
	                if (TPredBI->isConditional() || FPredBI->isConditional())
	                  return std::nullopt;
	                if (TPredBI->getNumSuccessors() != 1 ||
	                    FPredBI->getNumSuccessors() != 1)
	                  return std::nullopt;
	                if (TPredBI->getSuccessor(0) != PN->getParent() ||
	                    FPredBI->getSuccessor(0) != PN->getParent())
	                  return std::nullopt;

	                BasicBlock *BranchBB = TruePred->getSinglePredecessor();
	                if (!BranchBB || BranchBB != FalsePred->getSinglePredecessor())
	                  return std::nullopt;
	                auto *BI = dyn_cast<BranchInst>(BranchBB->getTerminator());
	                if (!BI || !BI->isConditional() || BI->getNumSuccessors() != 2)
	                  return std::nullopt;
	                if (BI->getSuccessor(0) != TruePred ||
	                    BI->getSuccessor(1) != FalsePred)
	                  return std::nullopt;

	                Value *VTrue = PN->getIncomingValueForBlock(TruePred);
	                Value *VFalse = PN->getIncomingValueForBlock(FalsePred);
	                if (!VTrue || !VFalse || VTrue == PN || VFalse == PN)
	                  return std::nullopt;

	                auto Pred = emitCondition(BI->getCondition());
	                auto TV = emitValue(VTrue);
	                auto FV = emitValue(VFalse);
	                if (!Pred || !TV || !FV)
	                  return std::nullopt;

	                auto Dst = allocVec();
	                if (!Dst)
	                  return std::nullopt;
	                OS << "  v.csel " << *Pred << ", " << *TV << ", " << *FV
	                   << ", ->" << *Dst << "\n";
	                return *Dst;
	              };

	              BasicBlock *Pred0 = PN->getIncomingBlock(0);
	              BasicBlock *Pred1 = PN->getIncomingBlock(1);
	              if (!L->contains(Pred0) || !L->contains(Pred1))
	                return std::nullopt;

	              if (auto Dst = tryEmitPhiSelect(Pred0, Pred1))
	                return Dst;
	              if (auto Dst = tryEmitPhiSelect(Pred1, Pred0))
	                return Dst;
	              if (auto Dst = tryEmitPhiSelectViaSplit(Pred0, Pred1))
	                return Dst;
	              if (auto Dst = tryEmitPhiSelectViaSplit(Pred1, Pred0))
	                return Dst;

	              return std::nullopt;
	            };

	            if (PN->getType() == Type::getFloatTy(Ctx) ||
	                (PN->getType()->isIntegerTy() &&
	                 PN->getType()->getScalarSizeInBits() <= 64)) {
	              if (auto Dst = tryEmitPhiSelectCsel()) {
	                ValOp[V] = *Dst;
	                return *Dst;
	              }
	            }

	            if (PN->getType() == Type::getFloatTy(Ctx)) {
	              Value *LoopIncoming = nullptr;
	              Value *PreIncoming = nullptr;
	              for (unsigned I = 0; I < 2; I++) {
	                BasicBlock *IncomingBB = PN->getIncomingBlock(I);
	                if (L->contains(IncomingBB)) {
                  if (LoopIncoming)
                    return std::nullopt;
                  LoopIncoming = PN->getIncomingValue(I);
                } else {
                  if (PreIncoming)
                    return std::nullopt;
                  PreIncoming = PN->getIncomingValue(I);
                }
              }
              if (!LoopIncoming || !PreIncoming) {
                return std::nullopt;
              }

              auto *LoopLI = dyn_cast_or_null<LoadInst>(LoopIncoming);
              auto *PreLI = dyn_cast_or_null<LoadInst>(PreIncoming);
              if (!LoopLI || !PreLI || LoopLI->isVolatile() || LoopLI->isAtomic() ||
                  PreLI->isVolatile() || PreLI->isAtomic()) {
                return std::nullopt;
              }

              const SCEV *PS = SE.getSCEVAtScope(LoopLI->getPointerOperand(), L);
              const auto *AR = dyn_cast<SCEVAddRecExpr>(PS);
              if (!AR || AR->getLoop() != L || !AR->isAffine()) {
                return std::nullopt;
              }
              const auto *StepC = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
              if (!StepC) {
                return std::nullopt;
              }
              const int64_t StepBytes = StepC->getAPInt().getSExtValue();
              if ((StepBytes % 4) != 0 || StepBytes <= 0) {
                return std::nullopt;
              }
              const SCEV *ExpectedPre =
                  SE.getMinusSCEV(AR->getStart(), AR->getStepRecurrence(SE));
              const SCEV *PreS = SE.getSCEVAtScope(PreLI->getPointerOperand(), L);
              if (!SE.isKnownPredicate(CmpInst::ICMP_EQ, PreS, ExpectedPre)) {
                return std::nullopt;
              }
              Value *AdjPtr = PB.CreateGEP(
                  PB.getInt8Ty(), LoopLI->getPointerOperand(),
                  ConstantInt::get(I64Ty, -StepBytes));
              auto Dst = emitLoadFromPtr(AdjPtr);
              if (!Dst) {
                return std::nullopt;
              }
              ValOp[V] = *Dst;
              return *Dst;

            }

	            if (PN->getType()->isIntegerTy() &&
	                PN->getType()->getScalarSizeInBits() <= 64) {
	              if (auto Dst = emitIntegerAffineAddRecValue(PN, /*EdgeFresh=*/false))
	                return Dst;
	            }

	            // Loop-invariant PHIs can appear in nested loops (outer IVs). Treat
	            // them like any other loop-invariant value and bind them.
	            if (L->isLoopInvariant(PN)) {
	              if (PN->getType()->isPointerTy()) {
	                Value *I64V = PB.CreatePtrToInt(const_cast<Value *>(V), I64Ty);
	                auto Bind = bindI64(I64V);
	                if (!Bind)
	                  return std::nullopt;
	                std::string Name = "ri" + std::to_string(*Bind);
	                ValOp[V] = Name;
	                return Name;
	              }
	              if (PN->getType()->isIntegerTy() &&
	                  PN->getType()->getScalarSizeInBits() <= 64) {
	                Value *I64V = PB.CreateZExtOrTrunc(const_cast<Value *>(V), I64Ty);
	                auto Bind = bindI64(I64V);
	                if (!Bind)
	                  return std::nullopt;
	                std::string Name = "ri" + std::to_string(*Bind);
	                ValOp[V] = Name;
	                return Name;
	              }
	              if (PN->getType() == Type::getFloatTy(Ctx)) {
	                auto *Bits32 = PB.CreateBitCast(const_cast<Value *>(V), I32Ty);
	                auto *I64V = PB.CreateZExt(Bits32, I64Ty);
	                auto Bind = bindI64(I64V);
	                if (!Bind)
	                  return std::nullopt;
	                std::string Name = "ri" + std::to_string(*Bind);
	                ValOp[V] = Name;
	                return Name;
	              }
	            }

	            return std::nullopt;
	          }

          if (auto *CF = dyn_cast<ConstantFP>(V)) {
            APInt Bits = CF->getValueAPF().bitcastToAPInt();
            if (Bits.getBitWidth() != 32)
              return std::nullopt;
            const uint64_t U = Bits.getZExtValue();
            if (U == 0) {
              ValOp[V] = "zero";
              return "zero";
            }
            auto *CI = ConstantInt::get(I64Ty, U);
            auto Bind = bindI64(CI);
            if (!Bind)
              return std::nullopt;
            std::string Name = "ri" + std::to_string(*Bind);
            ValOp[V] = Name;
            return Name;
          }

          if (auto *CI = dyn_cast<ConstantInt>(V)) {
            if (CI->isZero()) {
              ValOp[V] = "zero";
              return "zero";
            }
            auto *C64 = ConstantInt::get(I64Ty, CI->getZExtValue());
            auto Bind = bindI64(C64);
            if (!Bind)
              return std::nullopt;
            std::string Name = "ri" + std::to_string(*Bind);
            ValOp[V] = Name;
            return Name;
          }

          if (auto *CB = dyn_cast<CallBase>(V)) {
            Function *Callee = CB->getCalledFunction();
            if (!Callee)
              return std::nullopt;

            if (Callee->isIntrinsic()) {
              if (CB->getType() != Type::getFloatTy(Ctx))
                return std::nullopt;

              switch (Callee->getIntrinsicID()) {
              case Intrinsic::fmuladd:
              case Intrinsic::fma: {
                auto A = emitValue(CB->getArgOperand(0));
                auto B = emitValue(CB->getArgOperand(1));
                auto C = emitValue(CB->getArgOperand(2));
                if (!A || !B || !C)
                  return std::nullopt;
                auto Mul = allocVec();
                auto Dst = allocVec();
                if (!Mul || !Dst)
                  return std::nullopt;
                OS << "  v.fmul " << *A << ", " << *B << ", ->" << *Mul << "\n";
                OS << "  v.fadd " << *Mul << ", " << *C << ", ->" << *Dst
                   << "\n";
                ValOp[V] = *Dst;
                return *Dst;
              }
              default:
                break;
              }
              return std::nullopt;
            }

            // Leaf helper calls supported in bring-up mode.
            if (CB->getType() != Type::getFloatTy(Ctx))
              return std::nullopt;
            StringRef Name = Callee->getName();
            if (Name == "fabsf" && CB->arg_size() == 1) {
              auto Src = emitValue(CB->getArgOperand(0));
              if (!Src)
                return std::nullopt;
              auto Dst = allocVec();
              if (!Dst)
                return std::nullopt;
              OS << "  v.fabs " << *Src << ", ->" << *Dst << "\n";
              ValOp[V] = *Dst;
              return *Dst;
            }
            if (Name == "sqrtf" && CB->arg_size() == 1) {
              auto Src = emitValue(CB->getArgOperand(0));
              if (!Src)
                return std::nullopt;
              auto Dst = allocVec();
              if (!Dst)
                return std::nullopt;
              OS << "  v.fsqrt " << *Src << ", ->" << *Dst << "\n";
              ValOp[V] = *Dst;
              return *Dst;
            }
            if ((Name == "sinf" || Name == "cosf") && CB->arg_size() == 1) {
              // The freestanding bring-up runtime implements sin/cos as
              // conservative stubs (returns 0.0). Keep SIMT lowering aligned.
              ValOp[V] = "zero";
              return "zero";
            }

            return std::nullopt;
          }

          if (auto *LI = dyn_cast<LoadInst>(V)) {
            if (LI->isVolatile() || LI->isAtomic()) {
              return std::nullopt;
            }

            if (L->isLoopInvariant(LI->getPointerOperand())) {
              if (!LI->getType()->isFloatTy() &&
                  !(LI->getType()->isIntegerTy() &&
                    LI->getType()->getScalarSizeInBits() <= 32)) {
                return std::nullopt;
              }
              if (canScalarizeInvariantLoad(LI)) {
                Value *ScalarLoad = PB.CreateLoad(LI->getType(), LI->getPointerOperand());
                Value *I64V = nullptr;
                if (LI->getType()->isFloatTy()) {
                  auto *Bits32 = PB.CreateBitCast(ScalarLoad, I32Ty);
                  I64V = PB.CreateZExt(Bits32, I64Ty);
                } else {
                  I64V = PB.CreateZExtOrTrunc(ScalarLoad, I64Ty);
                }
                auto Slot = bindI64(I64V);
                if (!Slot) {
                  return std::nullopt;
                }
                std::string Name = "ri" + std::to_string(*Slot);
                ValOp[V] = Name;
                return Name;
              }
              auto Dst = emitLoadFromInvariantPtr(LI->getPointerOperand());
              if (!Dst) {
                return std::nullopt;
              }
              ValOp[V] = *Dst;
              return *Dst;
            }

            if (!LI->getType()->isFloatTy() &&
                !(LI->getType()->isIntegerTy() &&
                  LI->getType()->getScalarSizeInBits() <= 32))
              return std::nullopt;
            if (auto *SelPtr = dyn_cast<SelectInst>(LI->getPointerOperand())) {
              auto Pred = emitCondition(SelPtr->getCondition());
              auto TV = emitLoadFromPtr(SelPtr->getTrueValue());
              auto FV = emitLoadFromPtr(SelPtr->getFalseValue());
              if (!Pred || !TV || !FV)
                return std::nullopt;
              auto Dst = allocVec();
              if (!Dst)
                return std::nullopt;
              OS << "  v.csel " << *Pred << ", " << *TV << ", " << *FV
                 << ", ->" << *Dst << "\n";
              ValOp[V] = *Dst;
              return *Dst;
            }

            auto Dst = emitLoadFromPtr(LI->getPointerOperand());
            if (!Dst)
              return std::nullopt;
            ValOp[V] = *Dst;
            return *Dst;
          }

          if (auto *SI = dyn_cast<SelectInst>(V)) {
            auto Pred = emitCondition(SI->getCondition());
            auto TV = emitValue(SI->getTrueValue());
            auto FV = emitValue(SI->getFalseValue());
            if (!Pred || !TV || !FV)
              return std::nullopt;
            auto Dst = allocVec();
            if (!Dst)
              return std::nullopt;
            OS << "  v.csel " << *Pred << ", " << *TV << ", " << *FV
               << ", ->" << *Dst << "\n";
            ValOp[V] = *Dst;
            return *Dst;
          }

	          if (auto *Cast = dyn_cast<CastInst>(V)) {
	            switch (Cast->getOpcode()) {
	            case Instruction::Trunc:
	            case Instruction::ZExt:
	            case Instruction::SExt:
	              return emitValue(Cast->getOperand(0));
	            case Instruction::SIToFP:
	            case Instruction::UIToFP:
	            case Instruction::FPExt:
	            case Instruction::FPTrunc: {
	              if (Cast->getOpcode() == Instruction::FPExt &&
	                  Cast->getOperand(0)->getType() == Type::getFloatTy(Ctx) &&
	                  Cast->getType()->isDoubleTy()) {
	                // TSVC frequently promotes floats to double due to literal
	                // constants (e.g. "/1.9") and truncates back to float.
	                // We model the computation in float32 and treat these casts
	                // as no-ops in bring-up mode.
	                return emitValue(Cast->getOperand(0));
	              }
	              if (Cast->getOpcode() == Instruction::FPTrunc &&
	                  Cast->getType() == Type::getFloatTy(Ctx) &&
	                  Cast->getOperand(0)->getType()->isDoubleTy()) {
	                auto Tok = emitF32(Cast->getOperand(0));
	                if (Tok)
	                  ValOp[V] = *Tok;
	                return Tok;
	              }

	              if (Cast->getType() != Type::getFloatTy(Ctx))
	                return std::nullopt;
	              Value *Src = Cast->getOperand(0);
	              if (!L->isLoopInvariant(Src)) {
	                // Support affine int induction to float in scalar-lane replay
	                // mode by synthesizing a float induction slot.
	                auto PlanIdx = getOrCreateF32InductionPlan(Cast);
	                if (!PlanIdx)
	                  return std::nullopt;
	                const F32InductionPlan &Plan = F32InductionPlans[*PlanIdx];
	                auto Tok = emitLoadFromInvariantBind(Plan.SlotBind);
	                if (Tok)
	                  ValOp[V] = *Tok;
	                return Tok;
	              }
              Value *Scalar = PB.CreateCast(Cast->getOpcode(), Src, Cast->getType());
              auto *Bits32 = PB.CreateBitCast(Scalar, I32Ty);
              auto *I64V = PB.CreateZExt(Bits32, I64Ty);
              auto Bind = bindI64(I64V);
              if (!Bind)
                return std::nullopt;
              std::string Name = "ri" + std::to_string(*Bind);
              ValOp[V] = Name;
              return Name;
            }
            default:
              break;
            }
          }

          if (auto *UO = dyn_cast<UnaryOperator>(V)) {
            if (UO->getOpcode() == Instruction::FNeg) {
              auto Src = emitValue(UO->getOperand(0));
              if (!Src)
                return std::nullopt;
              auto Dst = allocVec();
              if (!Dst)
                return std::nullopt;
              OS << "  v.fsub zero, " << *Src << ", ->" << *Dst << "\n";
              ValOp[V] = *Dst;
              return *Dst;
            }
          }

          if (auto *BO = dyn_cast<BinaryOperator>(V)) {
            unsigned Opc = BO->getOpcode();
            if (Opc == Instruction::FAdd || Opc == Instruction::FSub ||
                Opc == Instruction::FMul || Opc == Instruction::FDiv) {
              if (BO->getType() != Type::getFloatTy(Ctx))
                return std::nullopt;
              auto Lhs = emitValue(BO->getOperand(0));
              auto Rhs = emitValue(BO->getOperand(1));
              if (!Lhs || !Rhs)
                return std::nullopt;
              auto Dst = allocVec();
              if (!Dst)
                return std::nullopt;
              StringRef Mn = (Opc == Instruction::FAdd)   ? "v.fadd"
                            : (Opc == Instruction::FSub) ? "v.fsub"
                            : (Opc == Instruction::FMul) ? "v.fmul"
                                                         : "v.fdiv";
              OS << "  " << Mn << " " << *Lhs << ", " << *Rhs << ", ->" << *Dst
                 << "\n";
              ValOp[V] = *Dst;
              return *Dst;
            }

            if (Opc == Instruction::And) {
              if (!BO->getType()->isIntegerTy() ||
                  BO->getType()->getScalarSizeInBits() > 64)
                return std::nullopt;

              // TSVC frequently masks indices with 0xffffffff/0x7fffffff
              // after phi widening to i64. Treat those masks as no-ops in
              // bring-up mode (they only discard high bits that are known to
              // be zero for in-bounds loop indices).
              ConstantInt *MaskC = dyn_cast<ConstantInt>(BO->getOperand(0));
              Value *Other = BO->getOperand(1);
              if (!MaskC) {
                MaskC = dyn_cast<ConstantInt>(BO->getOperand(1));
                Other = BO->getOperand(0);
              }
              if (!MaskC)
                return std::nullopt;

              const uint64_t Mask = MaskC->getZExtValue();
              if (Mask == 0xffffffffffffffffULL || Mask == 0xffffffffULL ||
                  Mask == 0x7fffffffULL) {
                auto Tok = emitValue(Other);
                if (Tok)
                  ValOp[V] = *Tok;
                return Tok;
              }
              return std::nullopt;
            }

            if (Opc == Instruction::LShr) {
              if (!BO->getType()->isIntegerTy() ||
                  BO->getType()->getScalarSizeInBits() > 64)
                return std::nullopt;
              auto *Sh = dyn_cast<ConstantInt>(BO->getOperand(1));
              if (!Sh)
                return std::nullopt;
              const uint64_t ShiftImm = Sh->getZExtValue();

              // In grouped-lane mode, shifting the canonical IV right by the
              // group shift yields the group index (lc1). This allows us to
              // represent patterns like c[i/2] without needing a vblock shift
              // op.
              if (UseGroupedDims && LaneCount > 1 &&
                  isPowerOf2_64(static_cast<uint64_t>(LaneCount)) &&
                  ShiftImm == GroupShift) {
                const SCEV *XS = SE.getSCEVAtScope(BO->getOperand(0), L);
                const auto *AR = dyn_cast<SCEVAddRecExpr>(XS);
                if (AR && AR->getLoop() == L && AR->isAffine()) {
                  const auto *StartC = dyn_cast<SCEVConstant>(AR->getStart());
                  const auto *StepC =
                      dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
                  if (StartC && StepC && StartC->getAPInt().isZero() &&
                      StepC->getAPInt().getSExtValue() == 1) {
                    ValOp[V] = "lc1";
                    return std::string("lc1");
                  }
                }
              }
              return std::nullopt;
            }

            if (Opc == Instruction::Add || Opc == Instruction::Sub ||
                Opc == Instruction::Mul) {
              if (!BO->getType()->isIntegerTy() ||
                  BO->getType()->getScalarSizeInBits() > 64)
                return std::nullopt;
              auto Lhs = emitValue(BO->getOperand(0));
              auto Rhs = emitValue(BO->getOperand(1));
              if (!Lhs || !Rhs)
                return std::nullopt;
              auto Dst = allocVec();
              if (!Dst)
                return std::nullopt;
              StringRef Mn = (Opc == Instruction::Add)   ? "v.add"
                            : (Opc == Instruction::Sub) ? "v.sub"
                                                        : "v.mul";
              OS << "  " << Mn << " " << *Lhs << ", " << *Rhs << ", ->" << *Dst
                 << "\n";
              ValOp[V] = *Dst;
              return *Dst;
            }
          }

          if (L->isLoopInvariant(V)) {
            if (V->getType()->isPointerTy()) {
              Value *I64V = PB.CreatePtrToInt(const_cast<Value *>(V), I64Ty);
              auto Bind = bindI64(I64V);
              if (!Bind)
                return std::nullopt;
              std::string Name = "ri" + std::to_string(*Bind);
              ValOp[V] = Name;
              return Name;
            }
            if (V->getType()->isIntegerTy() &&
                V->getType()->getScalarSizeInBits() <= 64) {
              Value *I64V = PB.CreateZExtOrTrunc(const_cast<Value *>(V), I64Ty);
              auto Bind = bindI64(I64V);
              if (!Bind)
                return std::nullopt;
              std::string Name = "ri" + std::to_string(*Bind);
              ValOp[V] = Name;
              return Name;
            }
            if (V->getType() == Type::getFloatTy(Ctx)) {
              auto *Bits32 = PB.CreateBitCast(const_cast<Value *>(V), I32Ty);
              auto *I64V = PB.CreateZExt(Bits32, I64Ty);
              auto Bind = bindI64(I64V);
              if (!Bind)
                return std::nullopt;
              std::string Name = "ri" + std::to_string(*Bind);
              ValOp[V] = Name;
              return Name;
            }
          }

          return std::nullopt;
        };

	        auto emitStoreInst = [&](StoreInst *SI) -> bool {
	          if (SI->getValueOperand()->getType() != Type::getFloatTy(Ctx)) {
	            reject("non_float_store_value");
	            return false;
	          }

	          // Pointer sink PHI store: dispatch by selector written on the incoming
	          // edge (classic if/switch sinks in TSVC).
	          if (auto *GEP = dyn_cast_or_null<GEPOperator>(
	                  SI->getPointerOperand()->stripPointerCasts())) {
	            Value *Base = GEP->getPointerOperand()->stripPointerCasts();
		            if (auto *Phi = dyn_cast<PHINode>(Base)) {
		              auto PlanIt = PtrPhiPlans.find(Phi);
		              if (PlanIt != PtrPhiPlans.end()) {
		                Value *Index = nullptr;
		                const unsigned NumIdx = GEP->getNumIndices();
		                if (NumIdx == 1) {
		                  Index = GEP->getOperand(1);
	                } else if (NumIdx == 2) {
	                  auto *Z = dyn_cast<ConstantInt>(GEP->getOperand(1));
	                  if (!Z || !Z->isZero()) {
	                    reject("unsupported_ptr_phi_store_gep");
	                    return false;
	                  }
	                  Index = GEP->getOperand(2);
	                } else {
	                  reject("unsupported_ptr_phi_store_gep");
	                  return false;
	                }
		                if (!Index || !Index->getType()->isIntegerTy() ||
		                    Index->getType()->getScalarSizeInBits() > 64) {
		                  reject("unsupported_ptr_phi_store_gep");
		                  return false;
		                }

		                const DataLayout &DL =
		                    F.getParent()->getDataLayout();
		                Type *ElemTy = GEP->getResultElementType();
		                const uint64_t ElemBytes =
		                    ElemTy ? DL.getTypeStoreSize(ElemTy) : 0;
		                std::optional<std::string> IdxExpr;
		                if (ElemBytes == 4) {
		                  IdxExpr = emitValue(Index);
		                } else if (ElemBytes == 1) {
		                  IdxExpr = emitWordIndexFromByteIndex(Index);
		                } else {
		                  reject("unsupported_ptr_phi_store_gep");
		                  return false;
		                }

		                if (!IdxExpr) {
		                  reject("unsupported_ptr_phi_store_index");
		                  return false;
		                }
		                auto DeltaExpr = emitIndexDeltaFromLc0(*IdxExpr);
	                if (!DeltaExpr) {
	                  reject("unsupported_ptr_phi_store_index");
	                  return false;
	                }

	                auto Val = emitValue(SI->getValueOperand());
	                if (!Val) {
	                  reject(unsupportedValueReason(SI->getValueOperand()));
	                  return false;
	                }

	                PtrPhiPlan &Plan = PlanIt->second;
	                if (Plan.BaseRis.empty()) {
	                  reject("invalid_ptr_phi_plan");
	                  return false;
	                }

	                const std::string EndLbl =
	                    freshAsmLabel("L_ptrphi_st_end");
	                SmallVector<std::pair<std::string, unsigned>, 4> CaseLabels;
	                CaseLabels.reserve(Plan.BaseRis.size());
	                for (unsigned SelId = 0; SelId < Plan.BaseRis.size();
	                     ++SelId) {
	                  std::string CaseLbl =
	                      freshAsmLabel("L_ptrphi_st_case");
	                  CaseLabels.push_back(std::make_pair(CaseLbl, SelId));

	                  std::string SelTok = "zero";
	                  if (SelId != 0) {
	                    auto Tok = emitValue(ConstantInt::get(I64Ty, SelId));
	                    if (!Tok) {
	                      reject("ptr_phi_sel_emit_failed");
	                      return false;
	                    }
	                    SelTok = *Tok;
	                  }

		                  auto Pred = allocVec();
		                  if (!Pred) {
		                    reject("vector_reg_exhausted");
		                    return false;
		                  }
			                  OS << "  v.cmp.eq " << Plan.SelReg << ", " << SelTok
			                     << ", ->" << *Pred << "\n";
			                  // Reduce ops accumulate into the destination register; seed our
			                  // scratch reduce destination before each use.
			                  OS << "  c.movr zero, ->t\n";
			                  OS << "  v.rdor " << *Pred << ", ->t#1\n";
			                  OS << "  b.ne t#1, zero, " << CaseLbl << "\n";
			                }

	                OS << "  j " << CaseLabels.front().first << "\n";
	                for (auto &C : CaseLabels) {
	                  const unsigned SelId = C.second;
	                  if (SelId >= Plan.BaseRis.size()) {
	                    reject("invalid_ptr_phi_plan");
	                    return false;
	                  }
	                  const unsigned Ri = Plan.BaseRis[SelId];
	                  OS << C.first << ":\n";
	                  OS << "  v.sw.brg " << *Val << ", [ri" << Ri
	                     << ", lc0<<2, " << *DeltaExpr << "<<2]\n";
	                  OS << "  j " << EndLbl << "\n";
	                }
	                OS << EndLbl << ":\n";
	                return true;
	              }
	            }
	          }

	          auto Address = bindPtrStart(SI->getPointerOperand());
	          unsigned BaseRi = 0;
	          std::string IndexReg;
	          unsigned StoreShift = 0;
          if (Address) {
            BaseRi = Address->BaseRi;
            if (UseGroupedDims) {
              auto Idx = emitGroupedIndexReg(Address->StepElems);
              if (!Idx) {
                reject("unsupported_store_stride");
                return false;
              }
              IndexReg = *Idx;
              StoreShift = 2;
            } else {
              if (Address->StepElems < 0) {
                auto Idx = emitGroupedIndexReg(Address->StepElems);
                if (!Idx) {
                  reject("unsupported_store_stride");
                  return false;
                }
                IndexReg = *Idx;
                StoreShift = 2;
              } else {
                StoreShift = Address->Shift;
                auto IndexRegOpt = emitScaledLc0(Address->IndexFactor);
                if (!IndexRegOpt) {
                  reject("unsupported_store_stride");
                  return false;
                }
                IndexReg = *IndexRegOpt;
              }
            }
          } else {
            auto General = bindPtrGeneral(SI->getPointerOperand());
            if (!General) {
              reject("non_affine_store_address");
              return false;
            }
            BaseRi = General->first;
            IndexReg = General->second;
            StoreShift = 2;
          }

          auto Val = emitValue(SI->getValueOperand());
          if (!Val) {
            reject(unsupportedValueReason(SI->getValueOperand()));
            return false;
          }

          // Preserve original per-iteration instruction order.
          // v0.3 encoding rule: v.sw uses an index shift of (2+shamt). For
          // contiguous stores we bind idx=zero, but the printed shift still
          // must be >= 2 to satisfy the assembler's legality checks.
          if (IndexReg == "zero" && StoreShift < 2)
            StoreShift = 2;
          OS << "  v.sw.brg " << *Val << ", [ri" << BaseRi
             << ", lc0<<2, " << IndexReg << "<<" << StoreShift << "]\n";
          return true;
        };

        DenseMap<unsigned, std::string> PendingRecurrenceValues;
        auto emitBodyInstructions = [&](BasicBlock *BB) -> bool {
          for (Instruction &I : *BB) {
            if (isa<PHINode>(I) || isa<BranchInst>(I) || isa<ICmpInst>(I) ||
                isa<FCmpInst>(I))
              continue;

            /*
             * Preserve per-iteration program order for values that feed later
             * stores. Our emitValue() is otherwise demand-driven (triggered by
             * stores), which can accidentally move loads across earlier stores
             * and change semantics for "read-before-write" patterns (e.g. TSVC
             * scalar/array expansion kernels like s1251).
             *
             * Emitting in-order and caching by SSA value keeps the body
             * deterministic without relying on alias analysis.
             */
            if (!isa<StoreInst>(I) && !I.use_empty()) {
              // Preserve program order for side-effecting value computations
              // that can affect memory semantics (loads and float ops feeding
              // stores). Avoid emitting pointer/i64 induction plumbing that is
              // not required for address formation and can generate illegal
              // vector+scalar ops on some loop-rotated forms (e.g. TSVC s1111).
              Type *Ty = I.getType();
              if (Ty->isFloatTy() ||
                  (Ty->isIntegerTy() && Ty->getScalarSizeInBits() <= 32)) {
                (void)emitValue(&I);
              }
            }
            if (auto *SI = dyn_cast<StoreInst>(&I)) {
              if (!emitStoreInst(SI))
                return false;
              continue;
            }
            auto RecIt = RecurrencePlansByUpdate.find(&I);
            if (RecIt == RecurrencePlansByUpdate.end())
              continue;
            auto EmittedVal = emitValue(&I);
            if (!EmittedVal) {
              reject(unsupportedValueReason(&I));
              return false;
            }
            for (unsigned RecIdx : RecIt->second) {
              if (RecIdx >= RecurrencePlans.size()) {
                reject("invalid_recurrence_plan");
                return false;
              }
              PendingRecurrenceValues[RecIdx] = *EmittedVal;
            }
          }
          return true;
        };

	        auto emitInnerControlFlowBody = [&]() -> bool {
	          // Linearize the loop's inner CFG for one "iteration" of the vblock
	          // body. The vblock launch (B.DIM replay) provides loop iteration,
	          // so edges to Header are treated as "end of iteration".
	          //
	          // Use a stable topological order over the acyclic inner CFG so we
	          // don't reject structured control flow due to an unlucky traversal
	          // order.
	          DenseMap<BasicBlock *, unsigned> FuncOrder;
	          unsigned FuncIdx = 0;
	          for (BasicBlock &BB : F)
	            FuncOrder[&BB] = FuncIdx++;

	          auto isBodyBlock = [&](BasicBlock *BB) -> bool {
	            if (!BB || !L->contains(BB))
	              return false;
	            // Header is always emitted first. Include the latch block as part
	            // of the linearized body so we don't drop iteration-tail side
	            // effects (stores/recurrence updates) that are commonly placed in
	            // the latch after if/else lowering under -O2.
	            if (BB == Header)
	              return false;
	            return true;
	          };

	          SmallVector<BasicBlock *, 16> Nodes;
	          SmallPtrSet<BasicBlock *, 16> NodeSet;
	          Nodes.push_back(Header);
	          NodeSet.insert(Header);

	          auto addNode = [&](BasicBlock *BB) {
	            if (!isBodyBlock(BB))
	              return;
	            if (NodeSet.insert(BB).second)
	              Nodes.push_back(BB);
	          };

	          // Discover all blocks reachable from Header within the "iteration"
	          // CFG (excluding edges to Header/Latch).
	          for (unsigned NI = 0; NI < Nodes.size(); ++NI) {
	            BasicBlock *BB = Nodes[NI];
	            auto *TI = BB->getTerminator();
	            if (!TI) {
	              reject("missing_terminator");
	              return false;
	            }
	            if (auto *BI = dyn_cast<BranchInst>(TI)) {
	              for (unsigned SI = 0; SI < BI->getNumSuccessors(); ++SI)
	                addNode(BI->getSuccessor(SI));
	              continue;
	            }
	            if (auto *SI = dyn_cast<SwitchInst>(TI)) {
	              addNode(SI->getDefaultDest());
	              for (auto Case : SI->cases())
	                addNode(Case.getCaseSuccessor());
	              continue;
	            }
	            reject("unsupported_terminator");
	            return false;
	          }

	          auto forEachBodySucc = [&](BasicBlock *BB,
	                                    function_ref<void(BasicBlock *)> Fn) {
	            auto *TI = BB ? BB->getTerminator() : nullptr;
	            if (!TI)
	              return;
	            if (auto *BI = dyn_cast<BranchInst>(TI)) {
	              for (unsigned SI = 0; SI < BI->getNumSuccessors(); ++SI) {
	                BasicBlock *Succ = BI->getSuccessor(SI);
	                if (NodeSet.count(Succ) && Succ != Header)
	                  Fn(Succ);
	              }
	              return;
	            }
	            if (auto *SI = dyn_cast<SwitchInst>(TI)) {
	              BasicBlock *Def = SI->getDefaultDest();
	              if (NodeSet.count(Def) && Def != Header)
	                Fn(Def);
	              for (auto Case : SI->cases()) {
	                BasicBlock *Succ = Case.getCaseSuccessor();
	                if (NodeSet.count(Succ) && Succ != Header)
	                  Fn(Succ);
	              }
	              return;
	            }
	          };

	          DenseMap<BasicBlock *, unsigned> Indegree;
	          for (BasicBlock *BB : Nodes)
	            Indegree[BB] = 0;
	          for (BasicBlock *BB : Nodes)
	            forEachBodySucc(BB, [&](BasicBlock *Succ) { ++Indegree[Succ]; });

	          SmallVector<BasicBlock *, 16> Ready;
	          for (BasicBlock *BB : Nodes) {
	            if (BB == Header)
	              continue;
	            if (Indegree.lookup(BB) == 0)
	              Ready.push_back(BB);
	          }

	          SmallVector<BasicBlock *, 16> EmitOrder;
	          EmitOrder.reserve(Nodes.size());
	          EmitOrder.push_back(Header);

	          auto pickReady = [&]() -> BasicBlock * {
	            unsigned BestI = 0;
	            unsigned BestOrder = std::numeric_limits<unsigned>::max();
	            for (unsigned I = 0; I < Ready.size(); ++I) {
	              BasicBlock *BB = Ready[I];
	              unsigned Ord = FuncOrder.lookup(BB);
	              if (Ord < BestOrder) {
	                BestOrder = Ord;
	                BestI = I;
	              }
	            }
	            BasicBlock *BB = Ready[BestI];
	            Ready.erase(Ready.begin() + BestI);
	            return BB;
	          };

	          auto process = [&](BasicBlock *BB) {
	            forEachBodySucc(BB, [&](BasicBlock *Succ) {
	              auto It = Indegree.find(Succ);
	              if (It == Indegree.end())
	                return;
	              if (It->second == 0)
	                return;
	              if (--It->second == 0 && Succ != Header)
	                Ready.push_back(Succ);
	            });
	          };

	          // Seed ready set after processing Header first.
	          process(Header);
	          while (!Ready.empty()) {
	            BasicBlock *BB = pickReady();
	            EmitOrder.push_back(BB);
	            process(BB);
	          }

	          if (EmitOrder.size() != Nodes.size()) {
	            reject("unsupported_inner_cycle");
	            return false;
	          }

	          DenseMap<BasicBlock *, std::string> Labels;
	          DenseMap<BasicBlock *, unsigned> LabelIndex;
	          for (unsigned I = 0; I < EmitOrder.size(); ++I)
            Labels[EmitOrder[I]] = ("L" + std::to_string(I));
          for (unsigned I = 0; I < EmitOrder.size(); ++I)
            LabelIndex[EmitOrder[I]] = I;
          const std::string EndLabel = "L_end";

		          auto labelForSucc = [&](BasicBlock *Succ) -> std::string {
		            if (!Succ || !L->contains(Succ) || Succ == Header)
		              return EndLabel;
		            auto It = Labels.find(Succ);
		            if (It == Labels.end())
		              return EndLabel;
		            return It->second;
	          };

	          // Plan inner-CF PHIs: allocate vector registers for scalar value PHIs
	          // and create selector plans for pointer PHIs so loads/stores can
	          // dispatch to the correct invariant base.
	          DenseMap<BasicBlock *, SmallVector<PHINode *, 4>> ValuePhisByBlock;
	          DenseMap<BasicBlock *, SmallVector<PHINode *, 2>> PtrPhisByBlock;

	          auto planInnerPhis = [&]() -> bool {
	            for (BasicBlock *BB : EmitOrder) {
	              if (!BB || BB == Header)
	                continue;

	              for (Instruction &I : *BB) {
	                auto *Phi = dyn_cast<PHINode>(&I);
	                if (!Phi)
	                  break;
	                if (Phi->use_empty())
	                  continue;

	                if (Phi->getType()->isPointerTy()) {
	                  if (!PtrPhiPlans.count(Phi)) {
	                    PtrPhiPlan Plan;
	                    auto Sel = allocVec();
	                    if (!Sel) {
	                      reject("vector_reg_exhausted");
	                      return false;
	                    }
	                    Plan.SelReg = *Sel;
	                    DenseMap<const Value *, unsigned> SelIdByPtr;

	                    for (unsigned II = 0, IE = Phi->getNumIncomingValues();
	                         II != IE; ++II) {
	                      Value *InV = Phi->getIncomingValue(II);
	                      BasicBlock *Pred = Phi->getIncomingBlock(II);
	                      Value *InBase =
	                          InV ? InV->stripPointerCasts() : nullptr;
	                      if (!InBase || !InBase->getType()->isPointerTy()) {
	                        reject("unsupported_ptr_phi_incoming");
	                        return false;
	                      }
	                      if (!L->isLoopInvariant(InBase)) {
	                        reject("unsupported_ptr_phi_variant_incoming");
	                        return false;
	                      }

	                      unsigned SelId = 0;
	                      auto Seen = SelIdByPtr.find(InBase);
	                      if (Seen != SelIdByPtr.end()) {
	                        SelId = Seen->second;
	                      } else {
	                        Value *I64V = PB.CreatePtrToInt(InBase, I64Ty);
	                        auto BaseOpt = bindI64(I64V);
	                        if (!BaseOpt) {
	                          reject("ptr_phi_bind_exhausted");
	                          return false;
	                        }
	                        SelId = Plan.BaseRis.size();
	                        Plan.BaseRis.push_back(*BaseOpt);
	                        SelIdByPtr[InBase] = SelId;
	                      }
	                      Plan.SelByPred[Pred] = SelId;
	                    }

	                    PtrPhiPlans[Phi] = std::move(Plan);
	                  }
	                  PtrPhisByBlock[BB].push_back(Phi);
	                  continue;
	                }

	                Type *Ty = Phi->getType();
	                if (Ty == Type::getFloatTy(Ctx) ||
	                    (Ty->isIntegerTy() && Ty->getScalarSizeInBits() <= 64)) {
	                  auto Dst = allocVec();
	                  if (!Dst) {
	                    reject("vector_reg_exhausted");
	                    return false;
	                  }
	                  ValOp[Phi] = *Dst;
	                  ValuePhisByBlock[BB].push_back(Phi);
	                  continue;
	                }

	                reject("unsupported_inner_phi_type");
	                return false;
	              }
	            }
	            return true;
	          };

	          if (!planInnerPhis())
	            return false;

	          SmallVector<
	              std::pair<std::string, std::pair<BasicBlock *, BasicBlock *>>,
	              16>
	              PhiEdgeLabels;

	          auto needsPhiEdge = [&](BasicBlock *SuccBB) -> bool {
	            if (!SuccBB)
	              return false;
	            auto VI = ValuePhisByBlock.find(SuccBB);
	            if (VI != ValuePhisByBlock.end() && !VI->second.empty())
	              return true;
	            auto PI = PtrPhisByBlock.find(SuccBB);
	            if (PI != PtrPhisByBlock.end() && !PI->second.empty())
	              return true;
	            return false;
	          };

	          auto getPhiEdgeLabel = [&](BasicBlock *PredBB,
	                                     BasicBlock *SuccBB) -> std::string {
	            std::string P = Labels.lookup(PredBB);
	            if (P.empty())
	              P = "L" + std::to_string(LabelIndex.lookup(PredBB));
	            std::string S = Labels.lookup(SuccBB);
	            if (S.empty())
	              S = "L" + std::to_string(LabelIndex.lookup(SuccBB));
	            std::string Lbl = P + "_to_" + S;
	            for (auto &E : PhiEdgeLabels) {
	              if (E.first == Lbl)
	                return Lbl;
	            }
	            PhiEdgeLabels.push_back(
	                std::make_pair(Lbl, std::make_pair(PredBB, SuccBB)));
	            return Lbl;
	          };

	          auto targetLabelForSucc = [&](BasicBlock *PredBB,
	                                        BasicBlock *SuccBB) -> std::string {
	            std::string Base = labelForSucc(SuccBB);
	            if (Base != EndLabel && needsPhiEdge(SuccBB))
	              return getPhiEdgeLabel(PredBB, SuccBB);
	            return Base;
	          };

	          auto isVectorToken = [](StringRef Tok) -> bool {
	            std::string Lower = Tok.trim().lower();
	            StringRef T(Lower);
	            return T.starts_with("vt#") || T.starts_with("vu#") ||
                   T.starts_with("vm#") || T.starts_with("vn#") ||
                   T.starts_with("lc") || T == "ta" || T == "tb" ||
                   T == "tc" || T == "td" || T == "to" || T == "ts" ||
                   T.starts_with("acc");
          };

	          auto emitCondBranch = [&](Value *Cond, StringRef TrueLabel,
	                                    StringRef FalseLabel) -> bool {
            std::string Mnemonic = "b.ne";
            std::string Lhs;
            std::string Rhs = "zero";

            auto emitPredicatedBranch = [&](Value *PredicateExpr) -> bool {
              auto Pred = emitCondition(PredicateExpr);
              if (!Pred) {
                reject("unsupported_branch_condition");
                return false;
              }
		              if (isVectorToken(*Pred)) {
		                // SIMT inner-CF fallback: branch on per-group "any-active-lane"
		                // predicate using vector OR-reduction to a scalar queue register.
		                // Keep this in-body so B.EQ/B.NE carry the actual CFG edges.
		                // Reduce ops accumulate into the destination register; seed our
		                // scratch reduce destination before each use.
		                OS << "  c.movr zero, ->t\n";
		                OS << "  v.rdor " << *Pred << ", ->t#1\n";
		                Lhs = "t#1";
		              } else {
		                Lhs = *Pred;
	              }
              Mnemonic = "b.ne";
              Rhs = "zero";
              return true;
            };

            if (auto *Cmp = dyn_cast<ICmpInst>(Cond)) {
              auto L = emitValue(Cmp->getOperand(0));
              auto R = emitValue(Cmp->getOperand(1));
              if (!L || !R) {
                reject("unsupported_branch_condition");
                return false;
              }

              if (Cmp->getOperand(0)->getType()->isIntegerTy(1) ||
                  Cmp->getOperand(1)->getType()->isIntegerTy(1)) {
                reject("unsupported_branch_i1_condition");
                return false;
              }

              const bool UsePredicateFallback =
                  isVectorToken(*L) || isVectorToken(*R);
              if (UsePredicateFallback) {
                if (!emitPredicatedBranch(Cond))
                  return false;
              } else {
                Lhs = *L;
                Rhs = *R;
                switch (Cmp->getPredicate()) {
                case CmpInst::ICMP_EQ:
                  Mnemonic = "b.eq";
                  break;
                case CmpInst::ICMP_NE:
                  Mnemonic = "b.ne";
                  break;
                case CmpInst::ICMP_SLT:
                  Mnemonic = "b.lt";
                  break;
                case CmpInst::ICMP_SLE:
                  Mnemonic = "b.ge";
                  std::swap(Lhs, Rhs);
                  break;
                case CmpInst::ICMP_SGT:
                  Mnemonic = "b.lt";
                  std::swap(Lhs, Rhs);
                  break;
                case CmpInst::ICMP_SGE:
                  Mnemonic = "b.ge";
                  break;
                case CmpInst::ICMP_ULT:
                  Mnemonic = "b.ltu";
                  break;
                case CmpInst::ICMP_ULE:
                  Mnemonic = "b.geu";
                  std::swap(Lhs, Rhs);
                  break;
                case CmpInst::ICMP_UGT:
                  Mnemonic = "b.ltu";
                  std::swap(Lhs, Rhs);
                  break;
                case CmpInst::ICMP_UGE:
                  Mnemonic = "b.geu";
                  break;
                default:
                  reject("unsupported_branch_predicate");
                  return false;
                }
              }
            } else if (isa<FCmpInst>(Cond)) {
              if (!emitPredicatedBranch(Cond)) {
                reject("unsupported_branch_fcmp_condition");
                return false;
              }
            } else {
              if (!emitPredicatedBranch(Cond))
                return false;
            }

            OS << "  " << Mnemonic << " " << Lhs << ", " << Rhs << ", "
               << TrueLabel << "\n";
            if (TrueLabel != FalseLabel)
              OS << "  j " << FalseLabel << "\n";
	            return true;
	          };

	          SmallVector<std::pair<std::string, BasicBlock *>, 8> ExitEdgeLabels;

	          auto emitExitEdgeStores = [&](BasicBlock *PredBB) -> bool {
	            auto It = ExitPhiStoresByBlock.find(PredBB);
	            if (It != ExitPhiStoresByBlock.end()) {
	              for (auto &Pair : It->second) {
	                unsigned BaseRi = Pair.first;
	                Value *VIn = Pair.second;
	                auto Tok = emitValue(VIn);
	                if (!Tok) {
	                  reject("exit_phi_value_emit_failed");
	                  return false;
	                }
	                if (!emitStoreToInvariantBind(*Tok, BaseRi)) {
	                  reject("exit_phi_store_emit_failed");
	                  return false;
	                }
	              }
	            }
	            if (ActiveSlotBind && NeedsActiveReplay) {
	              if (!emitStoreToInvariantBind("zero", *ActiveSlotBind)) {
	                reject("active_store_emit_failed");
	                return false;
	              }
	            }
	            return true;
	          };

	          auto getExitEdgeLabel =
	              [&](BasicBlock *PredBB, unsigned SuccIdx) -> std::string {
	            std::string Base = Labels.lookup(PredBB);
	            if (Base.empty())
	              Base = "L" + std::to_string(LabelIndex.lookup(PredBB));
	            std::string Lbl =
	                Base + "_exit" + std::to_string(SuccIdx);
	            for (auto &P : ExitEdgeLabels) {
	              if (P.first == Lbl)
	                return Lbl;
	            }
	            ExitEdgeLabels.push_back(std::make_pair(Lbl, PredBB));
	            return Lbl;
	          };

	          for (BasicBlock *BB : EmitOrder) {
	            if (BB != Header)
	              OS << Labels.lookup(BB) << ":\n";
	            if (!emitBodyInstructions(BB))
	              return false;

	            auto *TI = BB->getTerminator();
	            auto *BI = dyn_cast_or_null<BranchInst>(TI);
	            auto *SI = dyn_cast_or_null<SwitchInst>(TI);
	            if (!BI && !SI) {
	              reject("unsupported_terminator");
	              return false;
	            }

		            if (BI) {
			              if (!BI->isConditional()) {
			                BasicBlock *Succ = BI->getSuccessor(0);
			                std::string Target = targetLabelForSucc(BB, Succ);
			                if (Succ && !L->contains(Succ)) {
			                  if (!emitExitEdgeStores(BB))
			                    return false;
			                }
		                if (Target != EndLabel) {
		                  auto CurIt = LabelIndex.find(BB);
		                  auto SuccIt = LabelIndex.find(Succ);
		                  if (CurIt != LabelIndex.end() && SuccIt != LabelIndex.end() &&
	                      SuccIt->second <= CurIt->second) {
	                    reject("unsupported_inner_backedge");
	                    return false;
	                  }
	                }
	                OS << "  j " << Target << "\n";
	                continue;
	              }

	              BasicBlock *S0 = BI->getSuccessor(0);
	              BasicBlock *S1 = BI->getSuccessor(1);
		              const bool S0InLoop = L->contains(S0);
		              const bool S1InLoop = L->contains(S1);

		              // Header loop-entry guard is represented by B.DIM replay and is not
		              // part of the decoupled body control flow.
		              if (BB == Header && (S0InLoop != S1InLoop)) {
		                BasicBlock *InLoopSucc = S0InLoop ? S0 : S1;
		                if (InLoopSucc && InLoopSucc != Latch)
		                  continue;
		              }

			              std::string TrueLabel =
			                  (S0 && !S0InLoop) ? getExitEdgeLabel(BB, 0)
			                                    : targetLabelForSucc(BB, S0);
			              std::string FalseLabel =
			                  (S1 && !S1InLoop) ? getExitEdgeLabel(BB, 1)
			                                    : targetLabelForSucc(BB, S1);
		              if (TrueLabel != EndLabel) {
		                auto CurIt = LabelIndex.find(BB);
		                auto S0It = LabelIndex.find(S0);
		                if (CurIt != LabelIndex.end() && S0It != LabelIndex.end() &&
	                    S0It->second <= CurIt->second) {
	                  reject("unsupported_inner_backedge");
	                  return false;
	                }
	              }
	              if (FalseLabel != EndLabel) {
	                auto CurIt = LabelIndex.find(BB);
	                auto S1It = LabelIndex.find(S1);
	                if (CurIt != LabelIndex.end() && S1It != LabelIndex.end() &&
	                    S1It->second <= CurIt->second) {
	                  reject("unsupported_inner_backedge");
	                  return false;
	                }
	              }

	              if (TrueLabel == FalseLabel) {
	                OS << "  j " << TrueLabel << "\n";
	                continue;
	              }
	              if (!emitCondBranch(BI->getCondition(), TrueLabel, FalseLabel))
	                return false;
		              continue;
		            }

		            // SwitchInst: lower as a linear compare chain. In bring-up mode we
	            // model SIMT divergence via LaneCount=1, so a scalar branch is
	            // sufficient.
	            auto CondTok = emitValue(SI->getCondition());
	            if (!CondTok) {
	              reject("unsupported_switch_condition");
	              return false;
	            }

	            for (auto Case : SI->cases()) {
	              auto CaseTok = emitValue(Case.getCaseValue());
	              if (!CaseTok) {
	                reject("unsupported_switch_case");
	                return false;
	              }
		              BasicBlock *DestBB = Case.getCaseSuccessor();
		              std::string DestLabel = targetLabelForSucc(BB, DestBB);
	              if (DestLabel != EndLabel) {
	                auto CurIt = LabelIndex.find(BB);
	                auto DestIt = LabelIndex.find(DestBB);
	                if (CurIt != LabelIndex.end() && DestIt != LabelIndex.end() &&
	                    DestIt->second <= CurIt->second) {
	                  reject("unsupported_inner_backedge");
	                  return false;
	                }
	              }
		              auto Pred = allocVec();
		              if (!Pred) {
		                reject("vector_reg_exhausted");
		                return false;
		              }
			              OS << "  v.cmp.eq " << *CondTok << ", " << *CaseTok << ", ->"
			                 << *Pred << "\n";
			              // Reduce ops accumulate into the destination register; seed our
			              // scratch reduce destination before each use.
			              OS << "  c.movr zero, ->t\n";
			              OS << "  v.rdor " << *Pred << ", ->t#1\n";
			              OS << "  b.ne t#1, zero, " << DestLabel << "\n";
			            }
		            std::string DefaultLabel =
		                targetLabelForSucc(BB, SI->getDefaultDest());
		            OS << "  j " << DefaultLabel << "\n";
			          }

			          // Emit phi-edge labels (PHI copies + ptr-phi selector writes)
			          // before the exit-edge labels so we can branch to them from
			          // within the linearized body.
			          for (auto &P : PhiEdgeLabels) {
			            OS << P.first << ":\n";
			            BasicBlock *PredBB = P.second.first;
			            BasicBlock *SuccBB = P.second.second;
			            if (!PredBB || !SuccBB) {
			              reject("invalid_phi_edge");
			              return false;
			            }

			            auto PI = PtrPhisByBlock.find(SuccBB);
			            if (PI != PtrPhisByBlock.end()) {
			              for (PHINode *Phi : PI->second) {
			                auto PlanIt = PtrPhiPlans.find(Phi);
			                if (PlanIt == PtrPhiPlans.end()) {
			                  reject("missing_ptr_phi_plan");
			                  return false;
			                }
			                PtrPhiPlan &Plan = PlanIt->second;
			                auto SelIt = Plan.SelByPred.find(PredBB);
			                if (SelIt == Plan.SelByPred.end()) {
			                  reject("missing_ptr_phi_edge");
			                  return false;
			                }
			                const unsigned SelId = SelIt->second;
			                std::string SelTok = "zero";
			                if (SelId != 0) {
			                  auto Tok = emitValue(ConstantInt::get(I64Ty, SelId));
			                  if (!Tok) {
			                    reject("ptr_phi_sel_emit_failed");
			                    return false;
			                  }
			                  SelTok = *Tok;
			                }
			                OS << "  v.add zero, " << SelTok << ", ->" << Plan.SelReg
			                   << "\n";
			              }
			            }

			            auto VI = ValuePhisByBlock.find(SuccBB);
			            if (VI != ValuePhisByBlock.end()) {
			              for (PHINode *Phi : VI->second) {
			                int Idx = Phi->getBasicBlockIndex(PredBB);
			                if (Idx < 0) {
			                  reject("missing_phi_incoming");
			                  return false;
			                }
			                Value *InV = Phi->getIncomingValue(Idx);
			                std::optional<std::string> SrcTok;
			                bool NeedsEdgeFresh = false;
			                if (InV->getType()->isIntegerTy() &&
			                    InV->getType()->getScalarSizeInBits() <= 64) {
			                  const SCEV *InS = SE.getSCEVAtScope(InV, L);
			                  const auto *AR = dyn_cast<SCEVAddRecExpr>(InS);
			                  if (AR && AR->getLoop() == L && AR->isAffine()) {
			                    NeedsEdgeFresh = true;
			                    SrcTok = emitIntegerAffineAddRecValue(
			                        InV, /*EdgeFresh=*/true);
			                  }
			                }
			                if (NeedsEdgeFresh && !SrcTok) {
			                  reject("phi_incoming_addrec_emit_failed");
			                  return false;
			                }
			                if (!SrcTok)
			                  SrcTok = emitValue(InV);
			                if (!SrcTok) {
			                  reject("phi_incoming_emit_failed");
			                  return false;
			                }
			                auto DIt = ValOp.find(Phi);
			                if (DIt == ValOp.end()) {
			                  reject("missing_phi_reg");
			                  return false;
			                }
			                OS << "  v.add " << *SrcTok << ", zero, ->" << DIt->second
			                   << "\n";
			              }
			            }

			            OS << "  j " << labelForSucc(SuccBB) << "\n";
			          }

			          // Emit exit-edge labels (stores + active=0) before the end-of-iteration
			          // label so we can branch to them from within the linearized body.
			          for (auto &P : ExitEdgeLabels) {
			            OS << P.first << ":\n";
		            if (!emitExitEdgeStores(P.second))
		              return false;
		            OS << "  j " << EndLabel << "\n";
		          }

		          OS << EndLabel << ":\n";
		          return true;
	        };

        const std::string AfterLabel = "L_after";
        if (ActiveSlotBind) {
          auto ActiveTok = emitLoadFromInvariantBind(*ActiveSlotBind);
          if (!ActiveTok) {
            reject("active_load_failed");
            return false;
          }
	          auto Pred = allocVec();
	          if (!Pred) {
	            reject("vector_reg_exhausted");
	            return false;
	          }
		          OS << "  v.cmp.eq " << *ActiveTok << ", zero, ->" << *Pred << "\n";
		          // Reduce ops accumulate into the destination register; seed our scratch
		          // reduce destination before each use.
		          OS << "  c.movr zero, ->t\n";
		          OS << "  v.rdor " << *Pred << ", ->t#1\n";
		          OS << "  b.ne t#1, zero, " << AfterLabel << "\n";
		        }

        if (IsSingleBlock) {
          if (!emitBodyInstructions(Header))
            return false;
        } else if (HasInnerCF) {
          if (!emitInnerControlFlowBody())
            return false;
        } else {
          for (StoreInst *SI : Stores) {
            if (!emitStoreInst(SI))
              return false;
          }
        }

        for (unsigned RecIdx = 0; RecIdx < RecurrencePlans.size(); RecIdx++) {
          const RecurrencePlan &Plan = RecurrencePlans[RecIdx];
          auto It = PendingRecurrenceValues.find(RecIdx);
          if (It == PendingRecurrenceValues.end()) {
            auto UpdateVal = emitValue(Plan.Update);
            if (!UpdateVal) {
              reject("recurrence_update_not_emitted");
              return false;
            }
            if (!emitStoreToInvariantBind(*UpdateVal, Plan.SlotBind)) {
              reject("recurrence_store_emit_failed");
              return false;
            }
            continue;
          }
          if (!emitStoreToInvariantBind(It->second, Plan.SlotBind)) {
            reject("recurrence_store_emit_failed");
            return false;
          }
        }

        for (unsigned FI = 0; FI < F32InductionPlans.size(); ++FI) {
          const F32InductionPlan &Plan = F32InductionPlans[FI];
          if (!Plan.Cast) {
            reject("invalid_f32_induction_plan");
            return false;
          }
          auto Cur = emitValue(Plan.Cast);
          if (!Cur) {
            reject("f32_induction_not_emitted");
            return false;
          }
          auto StepTok = emitValue(
              ConstantFP::get(Type::getFloatTy(Ctx), (double)Plan.Step));
          if (!StepTok) {
            reject("f32_induction_step_emit_failed");
            return false;
          }
          auto Next = allocVec();
          if (!Next) {
            reject("vector_reg_exhausted");
            return false;
          }
          OS << "  v.fadd " << *Cur << ", " << *StepTok << ", ->" << *Next
             << "\n";
          if (!emitStoreToInvariantBind(*Next, Plan.SlotBind)) {
            reject("f32_induction_store_emit_failed");
            return false;
          }
        }

        if (ActiveSlotBind && NeedsActiveReplay && ActiveContinueCond) {
          auto PredTok = emitCondition(ActiveContinueCond);
          if (!PredTok) {
            reject("active_cond_emit_failed");
            return false;
          }
          std::string PredName = *PredTok;
	          if (ActiveContinueInvert) {
	            auto Inv = allocVec();
	            if (!Inv) {
	              reject("vector_reg_exhausted");
	              return false;
	            }
	            OS << "  v.cmp.eq " << PredName << ", zero, ->" << *Inv << "\n";
	            PredName = *Inv;
		          }
		          // Reduce ops accumulate into the destination register; seed our scratch
		          // reduce destination before each use.
		          OS << "  c.movr zero, ->t\n";
		          OS << "  v.rdor " << PredName << ", ->t#1\n";
		          if (!emitStoreToInvariantBind("t#1", *ActiveSlotBind)) {
		            reject("active_store_emit_failed");
		            return false;
		          }
        }

        static constexpr const char *kReductionDstRegs[] = {"a0", "a1", "a2",
                                                             "a3", "a4", "a5"};
        if (ReductionPlans.size() > (sizeof(kReductionDstRegs) /
                                     sizeof(kReductionDstRegs[0]))) {
          reject("too_many_reductions");
          return false;
        }

	        if (!ReductionPlans.empty()) {
	          BasicBlock &EntryBB = F.getEntryBlock();
	          Instruction *EntryIP = &*EntryBB.getFirstInsertionPt();
	          IRBuilder<> EB(EntryIP);

          for (unsigned RI = 0; RI < ReductionPlans.size(); RI++) {
            ReductionPlan &Plan = ReductionPlans[RI];
            Type *RedTy = Plan.Update->getType();
            const uint32_t SlotElems =
                static_cast<uint32_t>(LaneCount ? LaneCount : 1u);
            Plan.Slot =
                EB.CreateAlloca(RedTy, ConstantInt::get(I32Ty, SlotElems),
                                "linx.simt.redslot");
            Plan.SlotElems = SlotElems;
            Plan.DstName = kReductionDstRegs[RI];

            Value *SlotI64 = PB.CreatePtrToInt(Plan.Slot, I64Ty);
            auto Bind = bindI64(SlotI64);
            if (!Bind) {
              reject("reduction_bind_exhausted");
              return false;
            }
            Plan.SlotBind = *Bind;

            std::optional<std::string> Src;
            if (Plan.LaneMulL && Plan.LaneMulR) {
              auto Lhs = emitValue(Plan.LaneMulL);
              auto Rhs = emitValue(Plan.LaneMulR);
              if (!Lhs || !Rhs) {
                reject("unsupported_reduction_value");
                return false;
              }
              auto Mul = allocVec();
              if (!Mul) {
                reject("vector_reg_exhausted");
                return false;
              }
              OS << "  v.fmul " << *Lhs << ", " << *Rhs << ", ->" << *Mul
                 << "\n";
              Src = *Mul;
            } else {
              Src = emitValue(Plan.LaneValue);
            }
            if (!Src) {
              reject("unsupported_reduction_value");
              return false;
            }

            OS << "  " << reductionMnemonic(Plan.Kind) << " " << *Src << ", ->"
               << Plan.DstName << "\n";
            OS << "  v.sw.brg " << Plan.DstName << ", [ri" << Plan.SlotBind
               << ", lc0<<2, zero<<2]\n";
	          }
	        }

	        for (const LiveOutPlan &Plan : LiveOutPlans) {
	          if (!Plan.Inst)
	            continue;
	          auto Tok = emitValue(Plan.Inst);
	          if (!Tok) {
	            reject("unsupported_liveout_value");
	            return false;
	          }
	          if (!emitStoreToInvariantBind(*Tok, Plan.SlotBind)) {
	            reject("liveout_store_emit_failed");
	            return false;
	          }
	        }

        if (ActiveSlotBind)
          OS << AfterLabel << ":\n";
	        OS << "  C.BSTOP\n";
	        F.addFnAttr("linx-vblock-body-asm", OS.str());

        // Decoupled body contract:
        // - Launch block carries only BSTART.{MSEQ,MPAR} descriptors.
        // - Out-of-line body is linear and ends with C.BSTOP.
        // - Header and body are connected via B.TEXT and execute with
        //   lane/group replay state (LB0/LB1/LB2).
        //
        // Create a dedicated launch block so the backend can form a valid
        // block header (BSTART.MSEQ/MPAR + descriptors) without non-descriptor
        // instructions preceding it.
        BasicBlock *LaunchBB =
            BasicBlock::Create(Ctx, "linx.vblock.launch", &F, Exit);
        IRBuilder<> LB(LaunchBB);

        const bool TouchesMemory = !Stores.empty() || !Loads.empty();
        const bool ParallelMode = (SelectedMode == "mpar");
        unsigned VKindImm = 0;
        if (TouchesMemory) {
          VKindImm = ParallelMode ? 1u : 0u; // MPAR/MSEQ
          RemarkHeaderKind = ParallelMode ? "mpar" : "mseq";
        } else {
          VKindImm = ParallelMode ? 3u : 2u; // VPAR/VSEQ
          RemarkHeaderKind = ParallelMode ? "vpar" : "vseq";
        }
        RemarkTouchesMemoryState = TouchesMemory ? 1 : 0;
        Value *VKind = ConstantInt::get(I32Ty, VKindImm);
        Value *BodySym = ConstantPointerNull::get(PointerType::getUnqual(Ctx));
        Value *Dim0 = ConstantInt::get(I64Ty, LaneCount);
        Value *Dim1 =
            (HasConstTripCount || GroupCount > 1)
                ? static_cast<Value *>(ConstantInt::get(I64Ty, GroupCount))
                : TripCountV;
        Value *Dim2 = ConstantInt::get(I64Ty, 1);
        Value *AttrBits = ConstantInt::get(I32Ty, 0);

        while (BindVals.size() < kMaxVBlockBinds)
          BindVals.push_back(ConstantInt::get(I64Ty, 0));

	        LB.CreateCall(Intr, {VKind, BodySym, Dim0, Dim1, Dim2, AttrBits,
	                             BindVals[0], BindVals[1], BindVals[2], BindVals[3],
	                             BindVals[4], BindVals[5], BindVals[6], BindVals[7],
	                             BindVals[8], BindVals[9], BindVals[10], BindVals[11]});

	        if (!ExitPhiPlans.empty()) {
	          for (ExitPhiPlan &Plan : ExitPhiPlans) {
	            if (!Plan.Phi || !Plan.Slot) {
	              reject("invalid_exit_phi_plan");
	              return false;
	            }
	            LoadInst *LiveOut =
	                LB.CreateLoad(Plan.Phi->getType(), Plan.Slot, "linx.exitphi");
	            Plan.Phi->addIncoming(LiveOut, LaunchBB);
	          }
	        }

		        if (!ReductionPlans.empty() || !RecurrencePlans.empty() ||
		            !LiveOutPlans.empty()) {
		          Instruction *ExitIP = &*Exit->getFirstInsertionPt();
		          IRBuilder<> ExitB(ExitIP);

          auto replaceOutsideUses = [&](Value *From, Value *To) {
            if (!From || !To)
              return;
            SmallVector<Use *, 8> ToReplace;
            for (Use &U : From->uses()) {
              auto *UI = dyn_cast<Instruction>(U.getUser());
              if (!UI)
                continue;
              if (!L->contains(UI))
                ToReplace.push_back(&U);
            }
            for (Use *U : ToReplace)
              U->set(To);
          };

          for (ReductionPlan &Plan : ReductionPlans) {
            Value *LoadPtr = Plan.Slot;
            if (Plan.SlotElems > 1) {
              LoadPtr = ExitB.CreateConstInBoundsGEP1_32(
                  Plan.Update->getType(), Plan.Slot,
                  static_cast<unsigned>(Plan.SlotElems - 1u),
                  "linx.red.last.ptr");
            }
            LoadInst *LiveOut = ExitB.CreateLoad(Plan.Update->getType(), LoadPtr,
                                                 "linx.red");
            replaceOutsideUses(Plan.Update, LiveOut);
            replaceOutsideUses(Plan.Phi, LiveOut);
          }

	          for (RecurrencePlan &Plan : RecurrencePlans) {
              if (!Plan.Slot || !Plan.Phi || !Plan.SlotTy) {
                reject("invalid_recurrence_plan");
                return false;
              }

              Value *Raw = ExitB.CreateLoad(Plan.SlotTy, Plan.Slot, "linx.rec");

              Value *PhiOut = Raw;
              if (Plan.SlotTy != Plan.Phi->getType()) {
                if (!Plan.SlotTy->isIntegerTy() || !Plan.Phi->getType()->isIntegerTy()) {
                  reject("invalid_recurrence_liveout_cast");
                  return false;
                }
                PhiOut = ExitB.CreateZExtOrTrunc(Raw, Plan.Phi->getType(), "linx.rec.zext");
              }

              Value *UpdateOut = PhiOut;
              if (Plan.Update && Plan.Update->getType() != Plan.Phi->getType()) {
                if (!Plan.Update->getType()->isIntegerTy() || !Plan.Phi->getType()->isIntegerTy()) {
                  reject("invalid_recurrence_liveout_cast");
                  return false;
                }
                UpdateOut = ExitB.CreateZExtOrTrunc(PhiOut, Plan.Update->getType(),
                                                   "linx.rec.upd.zext");
              }

	            replaceOutsideUses(Plan.Update, UpdateOut);
	            replaceOutsideUses(Plan.Phi, PhiOut);
	          }

	          for (const LiveOutPlan &Plan : LiveOutPlans) {
	            if (!Plan.Inst || !Plan.Slot)
	              continue;
	            LoadInst *LiveOut =
	                ExitB.CreateLoad(Plan.Inst->getType(), Plan.Slot, "linx.liveout");
	            replaceOutsideUses(Plan.Inst, LiveOut);
	          }
	        }

        LB.CreateBr(Exit);

        PHBr->setSuccessor(0, LaunchBB);

        FunctionLowered = true;
        Changed = true;
        Status = "lowered";
        Reason = (IsAffine ? ("lowered_vblock_" + RemarkHeaderKind + "_affine")
                           : ("lowered_vblock_" + RemarkHeaderKind));
        return true;
      };

      if (!IsInnermost) {
        reject("not_innermost_loop");
      } else if (!IsCanonical) {
        // Still reject non-simplified loops in the first slice: we rely on
        // LoopSimplifyForm for a stable preheader/header/exit structure.
        reject("not_loop_simplify");
      } else {
        (void)tryLowerToVBlock();
      }

      BasicBlock *Header = L->getHeader();
      StringRef LoopName = Header ? Header->getName() : StringRef("<unnamed>");
      emitRemark(F.getName(), LoopName, Status, Reason, ConfigMode,
                 SelectedMode, IsCounted, IsCanonical, IsSingleBlock, HasStore,
                 HasExtraPhi, RemarkLaneCount, RemarkGroupCount,
                 RemarkForceScalarLane, RemarkHasRecurrence, RemarkHeaderKind,
                 RemarkTouchesMemoryState, RemarkTripcountSource,
                 RemarkAddressModel);
    }
    return Changed;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
  }
};

char LinxISASIMTAutoVectorize::ID = 0;

} // namespace

INITIALIZE_PASS_BEGIN(LinxISASIMTAutoVectorize, "linx-simt-autovec-pass",
                      "Linx SIMT AutoVectorize", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(LinxISASIMTAutoVectorize, "linx-simt-autovec-pass",
                    "Linx SIMT AutoVectorize", false, false)

bool llvm::linxSIMTAutoVectorizeEnabled() { return LinxSIMTAutoVec; }

StringRef llvm::linxSIMTAutoVectorizeMode() {
  return modeName(LinxSIMTAutoVecMode);
}

StringRef llvm::linxSIMTAutoVectorizeRemarksPath() {
  return LinxSIMTAutoVecRemarks;
}

FunctionPass *llvm::createLinxISASIMTAutoVectorizePass() {
  return new LinxISASIMTAutoVectorize();
}
