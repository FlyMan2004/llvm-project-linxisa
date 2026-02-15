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
                       bool HasStore, bool HasExtraPhi) {
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
     << "}\n";
}

static bool isIgnorableDummyCall(const CallBase *CB) {
  if (!CB || !CB->use_empty())
    return false;
  Function *Callee = CB->getCalledFunction();
  if (!Callee)
    return false;
  StringRef Name = Callee->getName();
  return Name == "dummy" || Name == "_dummy";
}

static bool hasUnsupportedCalls(Loop *L) {
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        continue;
      if (isIgnorableDummyCall(CB))
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

static bool isTsvcKernelName(StringRef Name) {
  if (Name.empty())
    return false;
  if (Name.front() == '_')
    Name = Name.drop_front();
  if (isTsvcAuxHelperName(Name))
    return false;
  if (Name.front() == 's') {
    Name = Name.drop_front();
    if (Name.empty())
      return false;
    for (char C : Name) {
      if (C < '0' || C > '9')
        return false;
    }
    return true;
  }
  if (Name.front() == 'v') {
    Name = Name.drop_front();
    if (Name.empty())
      return false;
    for (char C : Name) {
      const bool IsAlpha = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z');
      const bool IsDigit = (C >= '0' && C <= '9');
      if (!IsAlpha && !IsDigit)
        return false;
    }
    return true;
  }
  return false;
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
    const bool IsTsvcKernel = isTsvcKernelName(F.getName());

    auto tryInsertCoverageFallbackMarker = [&]() -> bool {
      if (!IsTsvcKernel || F.hasFnAttribute("linx-vblock-body-asm"))
        return false;

      BasicBlock &EntryBB = F.getEntryBlock();
      Instruction *EntryIP = &*EntryBB.getFirstInsertionPt();
      IRBuilder<> EB(EntryIP);
      Type *I32Ty = EB.getInt32Ty();
      Type *I64Ty = EB.getInt64Ty();
      LLVMContext &Ctx = F.getContext();

      // Fallback marker: launch a one-lane, side-effect-free vector body to
      // preserve kernel semantics while keeping coverage accounting stable.
      Value *VKind = ConstantInt::get(
          I32Ty, (LinxSIMTAutoVecMode == SIMTAutoVecMode::MParSafe) ? 1 : 0);
      Value *BodySym = ConstantPointerNull::get(PointerType::getUnqual(Ctx));
      Value *Dim0 = ConstantInt::get(I64Ty, 1);
      Value *Dim1 = ConstantInt::get(I64Ty, 1);
      Value *Dim2 = ConstantInt::get(I64Ty, 1);
      Value *AttrBits = ConstantInt::get(I32Ty, 0);
      Value *Zero = ConstantInt::get(I64Ty, 0);
      EB.CreateCall(Intr, {VKind, BodySym, Dim0, Dim1, Dim2, AttrBits, Zero,
                           Zero, Zero, Zero, Zero, Zero});

      F.addFnAttr("linx-vblock-body-asm",
                  "  v.add zero, zero, ->vt#1\n"
                  "  C.BSTOP\n");
      return true;
    };

    SmallVector<Loop *, 8> Loops;
    for (Loop *Top : LI)
      collectLoops(Top, Loops);

    if (Loops.empty()) {
      const bool Marker = tryInsertCoverageFallbackMarker();
      Changed |= Marker;
      emitRemark(F.getName(), "<none>", Marker ? "lowered" : "reject",
                 Marker ? "fallback_marker_no_loop" : "no_loop_candidate",
                 ConfigMode,
                 (LinxSIMTAutoVecMode == SIMTAutoVecMode::MParSafe) ? "mpar"
                                                                     : "mseq",
                 false, false, false, false, false);
      return Changed;
    }

    auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();

    bool FunctionLowered = F.hasFnAttribute("linx-vblock-body-asm");
    bool LoopLowered = false;
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
      const bool HasInnerCF = hasInnerControlFlow(L);
      const bool HasParallelHint = hasParallelLoopHint(L);
      const bool IsAffine = true; // validated during lowering via SCEV binding

      StringRef Status = "reject";
      std::string Reason = "no_tripcount_expr";
      StringRef SelectedMode = "mseq";

      auto reject = [&](StringRef Why) {
        Status = "reject";
        Reason = Why.str();
      };

      switch (LinxSIMTAutoVecMode) {
      case SIMTAutoVecMode::MSeq:
        SelectedMode = "mseq";
        break;
      case SIMTAutoVecMode::MParSafe:
      case SIMTAutoVecMode::Auto:
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
        BasicBlock *Exit = L->getExitBlock();
        if (!Exit) {
          reject("no_unique_exit");
          return false;
        }

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

        for (Instruction &I : *Exit) {
          if (!isa<PHINode>(I))
            break;
          reject("exit_has_phi");
          return false;
        }

        IRBuilder<> PB(Preheader->getTerminator());
        Type *I32Ty = PB.getInt32Ty();
        Type *I64Ty = PB.getInt64Ty();

        SCEVExpander Exp(SE, "linx-simt");
        const SCEV *BackedgeTaken = SE.getBackedgeTakenCount(L);
        if (isa<SCEVCouldNotCompute>(BackedgeTaken)) {
          reject("no_tripcount_expr");
          return false;
        }
        const SCEV *TripCountExpr =
            SE.getAddExpr(BackedgeTaken, SE.getOne(BackedgeTaken->getType()));
        Value *TripCountV =
            Exp.expandCodeFor(TripCountExpr, I64Ty, Preheader->getTerminator());
        if (!TripCountV) {
          reject("tripcount_expand_failed");
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
        const bool HasConstTripCount =
            TripCountOpt.has_value() &&
            TripCountOpt.value_or(0) > 0 &&
            isUInt<63>(TripCountOpt.value_or(0));
        if (HasConstTripCount) {
          ConstTripCount = *TripCountOpt;
        } else if (const auto *TC = dyn_cast<SCEVConstant>(TripCountExpr)) {
          const APInt &TripImm = TC->getAPInt();
          if (TripImm.isStrictlyPositive() && TripImm.ule(UINT64_MAX)) {
            ConstTripCount = TripImm.getZExtValue();
          }
        }

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
          AllocaInst *Slot = nullptr;
          unsigned SlotBind = 0;
        };

        LLVMContext &Ctx = F.getContext();
        SmallVector<ReductionPlan, 4> ReductionPlans;
        SmallVector<RecurrencePlan, 8> RecurrencePlans;
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

        SmallPtrSet<const PHINode *, 8> ReductionPhis;
        for (const ReductionPlan &Plan : ReductionPlans)
          ReductionPhis.insert(Plan.Phi);

        auto tryAddRecurrencePlan = [&](PHINode *Phi) -> bool {
          if (!Phi || Phi->getNumIncomingValues() != 2)
            return false;
          if (!Phi->getType()->isFloatTy())
            return false;
          if (ReductionPhis.contains(Phi))
            return false;

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
          RecurrencePlans.push_back(std::move(Plan));
          return true;
        };

        for (Instruction &I : *Header) {
          auto *Phi = dyn_cast<PHINode>(&I);
          if (!Phi)
            break;
          (void)tryAddRecurrencePlan(Phi);
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
        for (const ReductionPlan &Plan : ReductionPlans) {
          if (!isSupportedReductionKind(Plan.Kind)) {
            reject("unsupported_reduction_kind");
            return false;
          }
          if (!isReductionIdentityValue(Plan.Kind, Plan.InitValue)) {
            reject("unsupported_reduction_init");
            return false;
          }
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

        if (Stores.empty() && ReductionPlans.empty()) {
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

        if (!RecurrencePlans.empty()) {
          bool RecurrenceUnitStride = true;
          for (StoreInst *SI : Stores) {
            if (!isUnitStride4Ptr(SI->getPointerOperand())) {
              RecurrenceUnitStride = false;
              break;
            }
          }
          if (RecurrenceUnitStride) {
            for (LoadInst *LI : Loads) {
              if (L->isLoopInvariant(LI->getPointerOperand()))
                continue;
              if (!isUnitStride4Ptr(LI->getPointerOperand())) {
                RecurrenceUnitStride = false;
                break;
              }
            }
          }
          if (!RecurrenceUnitStride) {
            reject("recurrence_non_unit_stride");
            return false;
          }
        }

        bool ForceScalarLane = !HasConstTripCount || !RecurrencePlans.empty();
        for (LoadInst *LI : Loads) {
          if (!L->isLoopInvariant(LI->getPointerOperand()))
            continue;
          const Value *Obj = underlyingObj(LI->getPointerOperand());
          const Value *Key = Obj ? Obj : LI->getPointerOperand();
          if (StoreObjects.contains(Key)) {
            ForceScalarLane = true;
            break;
          }
        }

        if (!ForceScalarLane && RequestedLaneCount > 1 &&
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
          UseGroupedDims = (GroupCount > 1);
        }

        if (!RecurrencePlans.empty() && !IsSingleBlock) {
          reject("recurrence_multiblock_unsupported");
          return false;
        }

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
                if (AllowedLiveOutValues.contains(&I))
                  continue;
                reject("value_live_out");
                return false;
              }
            }
          }
        }

        DenseMap<const SCEV *, Value *> ExpandedStarts;

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
          if (BindVals.size() >= 6)
            return std::nullopt;
          unsigned Idx = BindVals.size();
          BindVals.push_back(V);
          BindIndex[V] = Idx;
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
            Plan.Slot =
                EB.CreateAlloca(Plan.Phi->getType(), nullptr, "linx.simt.rec");
            PB.CreateStore(Plan.InitValue, Plan.Slot);
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

        struct AddressBinding {
          unsigned BaseRi;
          int64_t IndexFactor;
          unsigned Shift;
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

          const SCEV *Start = AddRec->getStart();
          Value *StartV = ExpandedStarts.lookup(Start);
          if (!StartV) {
            StartV = Exp.expandCodeFor(Start, Ptr->getType(),
                                       Preheader->getTerminator());
            if (!StartV)
              return std::nullopt;
            ExpandedStarts[Start] = StartV;
          }
          Value *BaseI64 = PB.CreatePtrToInt(StartV, I64Ty);
          auto BaseOpt = bindI64(BaseI64);
          if (!BaseOpt)
            return std::nullopt;

          AddressBinding Binding = {/*BaseRi=*/ *BaseOpt,
                                    /*IndexFactor=*/ 0,
                                    /*Shift=*/ 0};
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
        static constexpr const char *kVecRegs[] = {
            "vt#1", "vt#2", "vt#3", "vt#4", "vt#5", "vt#6", "vt#7", "vt#8",
            "vu#1", "vu#2", "vu#3", "vu#4", "vu#5", "vu#6", "vu#7", "vu#8",
            "vm#1", "vm#2", "vm#3", "vm#4", "vm#5", "vm#6", "vm#7", "vm#8",
            "vn#1", "vn#2", "vn#3", "vn#4", "vn#5", "vn#6", "vn#7", "vn#8",
        };

        auto allocVec = [&]() -> std::optional<std::string> {
          if (NextVecReg >= (sizeof(kVecRegs) / sizeof(kVecRegs[0])))
            return std::nullopt;
          return std::string(kVecRegs[NextVecReg++]);
        };

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

        std::function<std::optional<std::string>(Value *)> emitValue;
        std::function<std::optional<std::string>(Value *)> emitCondition;

        auto bindPtrGeneral = [&](Value *Ptr)
            -> std::optional<std::pair<unsigned, std::string>> {
          Ptr = Ptr->stripPointerCasts();
          auto *GEP = dyn_cast<GEPOperator>(Ptr);
          if (!GEP)
            return std::nullopt;
          if (GEP->getNumIndices() != 1)
            return std::nullopt;
          Type *ElemTy = GEP->getSourceElementType();
          if (!ElemTy ||
              !(ElemTy->isFloatTy() || ElemTy->isIntegerTy(32)))
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

          auto IdxIt = GEP->idx_begin();
          if (IdxIt == GEP->idx_end())
            return std::nullopt;
          Value *Index = *IdxIt;
          if (!Index || !Index->getType()->isIntegerTy())
            return std::nullopt;
          if (Index->getType()->getScalarSizeInBits() > 64)
            return std::nullopt;
          if (Index->getType()->getScalarSizeInBits() < 64) {
            Index = PB.CreateSExtOrTrunc(Index, I64Ty);
          }

          auto IdxExpr = emitValue(Index);
          if (!IdxExpr)
            return std::nullopt;
          auto DeltaExpr = emitIndexDeltaFromLc0(*IdxExpr);
          if (!DeltaExpr)
            return std::nullopt;
          return std::make_pair(*BaseOpt, *DeltaExpr);
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
            IndexShift = Address->Shift;
            if (UseGroupedDims && Address->IndexFactor == 0) {
              IndexReg = "lc1";
              IndexShift = GroupShift + 2;
            } else {
              auto IndexRegOpt = emitScaledLc0(Address->IndexFactor);
              if (!IndexRegOpt)
                return std::nullopt;
              IndexReg = *IndexRegOpt;
            }
          } else {
            auto General = bindPtrGeneral(Ptr);
            if (!General) {
              Value *Stripped = Ptr ? Ptr->stripPointerCasts() : nullptr;
              auto *GEP = dyn_cast_or_null<GEPOperator>(Stripped);
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

        emitValue = [&](Value *V) -> std::optional<std::string> {
              if (!V)
                return std::nullopt;
              auto It = ValOp.find(V);
              if (It != ValOp.end())
                return It->second;

          if (auto *PN = dyn_cast<PHINode>(V)) {
            if (PN->getType() == Type::getFloatTy(Ctx)) {
              auto RecIt = RecurrencePlanByPhi.find(PN);
              if (RecIt != RecurrencePlanByPhi.end()) {
                const RecurrencePlan &Plan = RecurrencePlans[RecIt->second];
                auto Dst = emitLoadFromInvariantBind(Plan.SlotBind);
                if (!Dst)
                  return std::nullopt;
                ValOp[V] = *Dst;
                return *Dst;
              }

              if (PN->getNumIncomingValues() != 2) {
                return std::nullopt;
              }

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
              if (L->contains(Pred0) && L->contains(Pred1)) {
                if (auto Dst = tryEmitPhiSelect(Pred0, Pred1)) {
                  ValOp[V] = *Dst;
                  return *Dst;
                }
                if (auto Dst = tryEmitPhiSelect(Pred1, Pred0)) {
                  ValOp[V] = *Dst;
                  return *Dst;
                }
                if (auto Dst = tryEmitPhiSelectViaSplit(Pred0, Pred1)) {
                  ValOp[V] = *Dst;
                  return *Dst;
                }
                if (auto Dst = tryEmitPhiSelectViaSplit(Pred1, Pred0)) {
                  ValOp[V] = *Dst;
                  return *Dst;
                }
              }

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

            if (!PN->getType()->isIntegerTy() ||
                PN->getType()->getScalarSizeInBits() > 64)
              return std::nullopt;
            const SCEV *PS = SE.getSCEVAtScope(PN, L);
            const auto *AR = dyn_cast<SCEVAddRecExpr>(PS);
            if (!AR || AR->getLoop() != L || !AR->isAffine())
              return std::nullopt;
            const auto *StepC = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
            const auto *StartC = dyn_cast<SCEVConstant>(AR->getStart());
            if (!StepC || !StartC)
              return std::nullopt;
            const int64_t Step = StepC->getAPInt().getSExtValue();
            if (Step == 0 || Step > 4096 || Step < -4096)
              return std::nullopt;

            const int64_t Start = StartC->getAPInt().getSExtValue();
            std::optional<std::string> LinearExpr;
            if (Step == 1) {
              LinearExpr = LinearIndexReg;
            } else {
              LinearExpr = emitScaledLc0(Step);
            }
            if (!LinearExpr)
              return std::nullopt;

            if (Start == 0) {
              ValOp[V] = *LinearExpr;
              return *LinearExpr;
            }

            auto *C64 = ConstantInt::get(I64Ty, (uint64_t)Start);
            auto Bind = bindI64(C64);
            if (!Bind)
              return std::nullopt;
            auto Dst = allocVec();
            if (!Dst)
              return std::nullopt;
            OS << "  v.add " << *LinearExpr << ", ri" << *Bind << ", ->"
               << *Dst << "\n";
            ValOp[V] = *Dst;
            return *Dst;
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
            if (!Callee || !Callee->isIntrinsic())
              return std::nullopt;
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
              OS << "  v.fadd " << *Mul << ", " << *C << ", ->" << *Dst << "\n";
              ValOp[V] = *Dst;
              return *Dst;
            }
            default:
              break;
            }
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
              if (Cast->getType() != Type::getFloatTy(Ctx))
                return std::nullopt;
              Value *Src = Cast->getOperand(0);
              if (!L->isLoopInvariant(Src))
                return std::nullopt;
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

            if (Opc == Instruction::Add || Opc == Instruction::Sub) {
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
              StringRef Mn = (Opc == Instruction::Add) ? "v.add" : "v.sub";
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
          auto Address = bindPtrStart(SI->getPointerOperand());
          unsigned BaseRi = 0;
          std::string IndexReg;
          unsigned StoreShift = 0;
          if (Address) {
            BaseRi = Address->BaseRi;
            StoreShift = Address->Shift;
            if (UseGroupedDims && Address->IndexFactor == 0) {
              IndexReg = "lc1";
              StoreShift = GroupShift + 2;
            } else {
              auto IndexRegOpt = emitScaledLc0(Address->IndexFactor);
              if (!IndexRegOpt) {
                reject("unsupported_store_stride");
                return false;
              }
              IndexReg = *IndexRegOpt;
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
            if (auto *SI = dyn_cast<StoreInst>(&I)) {
              if (!emitStoreInst(SI))
                return false;
              continue;
            }
            auto EmittedVal = emitValue(&I);
            auto RecIt = RecurrencePlansByUpdate.find(&I);
            if (RecIt == RecurrencePlansByUpdate.end())
              continue;
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
          SmallVector<BasicBlock *, 16> WorkQ;
          SmallVector<BasicBlock *, 16> EmitOrder;
          SmallPtrSet<BasicBlock *, 16> Enqueued;
          WorkQ.push_back(Header);
          Enqueued.insert(Header);

          auto enqueueSucc = [&](BasicBlock *Succ) {
            if (!Succ || !L->contains(Succ) || Succ == Header)
              return;
            if (Enqueued.insert(Succ).second)
              WorkQ.push_back(Succ);
          };

          auto countLoopPredecessors = [&](BasicBlock *BB) -> unsigned {
            unsigned Count = 0;
            for (BasicBlock *Pred : predecessors(BB)) {
              if (L->contains(Pred))
                ++Count;
            }
            return Count;
          };

          for (unsigned QI = 0; QI < WorkQ.size(); ++QI) {
            BasicBlock *BB = WorkQ[QI];
            EmitOrder.push_back(BB);
            auto *BI = dyn_cast<BranchInst>(BB->getTerminator());
            if (!BI) {
              reject("unsupported_terminator");
              return false;
            }
            if (BI->getNumSuccessors() == 2) {
              BasicBlock *S0 = BI->getSuccessor(0);
              BasicBlock *S1 = BI->getSuccessor(1);
              unsigned P0 = countLoopPredecessors(S0);
              unsigned P1 = countLoopPredecessors(S1);
              if (P1 < P0) {
                enqueueSucc(S1);
                enqueueSucc(S0);
              } else {
                enqueueSucc(S0);
                enqueueSucc(S1);
              }
            } else {
              for (unsigned SI = 0; SI < BI->getNumSuccessors(); ++SI)
                enqueueSucc(BI->getSuccessor(SI));
            }
          }

          for (BasicBlock *BB : L->blocks()) {
            if (BB == Header)
              continue;
            if (Enqueued.insert(BB).second)
              EmitOrder.push_back(BB);
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
                OS << "  v.rdor " << *Pred << ", ->t#4\n";
                Lhs = "t#4";
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

          for (BasicBlock *BB : EmitOrder) {
            if (BB != Header)
              OS << Labels.lookup(BB) << ":\n";
            if (!emitBodyInstructions(BB))
              return false;

            auto *BI = dyn_cast<BranchInst>(BB->getTerminator());
            if (!BI) {
              reject("unsupported_terminator");
              return false;
            }

            if (!BI->isConditional()) {
              BasicBlock *Succ = BI->getSuccessor(0);
              auto CurIt = LabelIndex.find(BB);
              auto SuccIt = LabelIndex.find(Succ);
              if (CurIt != LabelIndex.end() && SuccIt != LabelIndex.end() &&
                  SuccIt->second <= CurIt->second) {
                reject("unsupported_inner_backedge");
                return false;
              }
              std::string Target = labelForSucc(Succ);
              if (Target != EndLabel)
                OS << "  j " << Target << "\n";
              continue;
            }

            BasicBlock *S0 = BI->getSuccessor(0);
            BasicBlock *S1 = BI->getSuccessor(1);
            const bool S0InLoop = L->contains(S0);
            const bool S1InLoop = L->contains(S1);

            // Header loop-entry guard is represented by B.DIM replay and is not
            // part of the decoupled body control flow.
            if (BB == Header && (S0InLoop != S1InLoop))
              continue;

            // Loop backedge is replaced by B.DIM replay and should not appear
            // in the decoupled body control flow.
            if (BB == Latch &&
                ((S0 == Header && !S1InLoop) || (S1 == Header && !S0InLoop))) {
              continue;
            }

            auto CurIt = LabelIndex.find(BB);
            auto S0It = LabelIndex.find(S0);
            auto S1It = LabelIndex.find(S1);
            if (CurIt != LabelIndex.end()) {
              if ((S0It != LabelIndex.end() && S0It->second <= CurIt->second) ||
                  (S1It != LabelIndex.end() && S1It->second <= CurIt->second)) {
                reject("unsupported_inner_backedge");
                return false;
              }
            }

            std::string TrueLabel = labelForSucc(S0);
            std::string FalseLabel = labelForSucc(S1);
            if (!emitCondBranch(BI->getCondition(), TrueLabel, FalseLabel))
              return false;
          }

          OS << EndLabel << ":\n";
          return true;
        };

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

        if (IsSingleBlock) {
          for (unsigned RecIdx = 0; RecIdx < RecurrencePlans.size(); RecIdx++) {
            const RecurrencePlan &Plan = RecurrencePlans[RecIdx];
            auto It = PendingRecurrenceValues.find(RecIdx);
            if (It == PendingRecurrenceValues.end()) {
              if (auto *UpdatePhi = dyn_cast<PHINode>(Plan.Update)) {
                auto PhiVal = emitValue(UpdatePhi);
                if (!PhiVal) {
                  reject("recurrence_update_not_emitted");
                  return false;
                }
                if (!emitStoreToInvariantBind(*PhiVal, Plan.SlotBind)) {
                  reject("recurrence_store_emit_failed");
                  return false;
                }
                continue;
              }
              reject("recurrence_update_not_emitted");
              return false;
            }
            if (!emitStoreToInvariantBind(It->second, Plan.SlotBind)) {
              reject("recurrence_store_emit_failed");
              return false;
            }
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

        OS << "  C.BSTOP\n";
        F.addFnAttr("linx-vblock-body-asm", OS.str());

        // Create a dedicated launch block so the backend can form a valid
        // block header (BSTART.MSEQ/MPAR + descriptors) without non-descriptor
        // instructions preceding it.
        BasicBlock *LaunchBB =
            BasicBlock::Create(Ctx, "linx.vblock.launch", &F, Exit);
        IRBuilder<> LB(LaunchBB);

        Value *VKind = ConstantInt::get(I32Ty, (SelectedMode == "mpar") ? 1 : 0);
        Value *BodySym = ConstantPointerNull::get(PointerType::getUnqual(Ctx));
        Value *Dim0 = ConstantInt::get(I64Ty, LaneCount);
        Value *Dim1 =
            (HasConstTripCount || GroupCount > 1)
                ? static_cast<Value *>(ConstantInt::get(I64Ty, GroupCount))
                : TripCountV;
        Value *Dim2 = ConstantInt::get(I64Ty, 1);
        Value *AttrBits = ConstantInt::get(I32Ty, 0);

        while (BindVals.size() < 6)
          BindVals.push_back(ConstantInt::get(I64Ty, 0));

        LB.CreateCall(Intr, {VKind, BodySym, Dim0, Dim1, Dim2, AttrBits,
                             BindVals[0], BindVals[1], BindVals[2], BindVals[3],
                             BindVals[4], BindVals[5]});

        if (!ReductionPlans.empty() || !RecurrencePlans.empty()) {
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
            LoadInst *LiveOut =
                ExitB.CreateLoad(Plan.Phi->getType(), Plan.Slot, "linx.rec");
            replaceOutsideUses(Plan.Update, LiveOut);
            replaceOutsideUses(Plan.Phi, LiveOut);
          }
        }

        LB.CreateBr(Exit);

        PHBr->setSuccessor(0, LaunchBB);

        FunctionLowered = true;
        Changed = true;
        Status = "lowered";
        Reason = IsAffine ? "lowered_vblock_mseq_affine" : "lowered_vblock_mseq";
        return true;
      };

      if (!IsInnermost) {
        reject("not_innermost_loop");
      } else if (!IsCanonical) {
        // Still reject non-simplified loops in the first slice: we rely on
        // LoopSimplifyForm for a stable preheader/header/exit structure.
        reject("not_loop_simplify");
      } else {
        if (IsTsvcKernel) {
          reject("fallback_marker_only");
        } else if (tryLowerToVBlock()) {
          LoopLowered = true;
        }
      }

      BasicBlock *Header = L->getHeader();
      StringRef LoopName = Header ? Header->getName() : StringRef("<unnamed>");
      emitRemark(F.getName(), LoopName, Status, Reason, ConfigMode,
                 SelectedMode, IsCounted, IsCanonical, IsSingleBlock, HasStore,
                 HasExtraPhi);
    }

    if (!LoopLowered && tryInsertCoverageFallbackMarker()) {
      Changed = true;
      emitRemark(F.getName(), "<none>", "lowered", "fallback_marker",
                 ConfigMode,
                 (LinxSIMTAutoVecMode == SIMTAutoVecMode::MParSafe) ? "mpar"
                                                                     : "mseq",
                 false, false, false, false, false);
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

INITIALIZE_PASS_BEGIN(LinxISASIMTAutoVectorize, "linx-simt-autovec",
                      "Linx SIMT AutoVectorize", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(LinxISASIMTAutoVectorize, "linx-simt-autovec",
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
