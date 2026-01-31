//===-- LinxISABlockify.cpp - Block boundary + T-hand lowering ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISA.h"
#include "LinxISAInstrInfo.h"
#include "LinxISARegisterInfo.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "linx-blockify"

namespace {

struct LocalDUInfo {
  unsigned DefCount = 0;
  unsigned UseCount = 0;
  unsigned DefIdx = 0;
  unsigned UseIdx = 0;
  MachineInstr *DefMI = nullptr;
  MachineInstr *UseMI = nullptr;
  unsigned DefOpNo = 0;
  unsigned UseOpNo = 0;
};

static bool isMarkerInstr(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case LinxISA::CBSTART_STD:
  case LinxISA::BSTART_STD_FALL:
  case LinxISA::BSTART_STD_DIRECT:
  case LinxISA::BSTART_STD_COND:
  case LinxISA::BSTART_STD_CALL:
  case LinxISA::BSTART_STD_IND:
  case LinxISA::BSTART_STD_ICALL:
  case LinxISA::BSTART_STD_RET:
  case LinxISA::BSTOP:
    return true;
  default:
    return false;
  }
}

static Register getTQueueUseReg(unsigned Index) {
  switch (Index) {
  case 1:
    return LinxISA::T1;
  case 2:
    return LinxISA::T2;
  case 3:
    return LinxISA::T3;
  case 4:
    return LinxISA::T4;
  default:
    return Register();
  }
}

static Register getUQueueUseReg(unsigned Index) {
  switch (Index) {
  case 1:
    return LinxISA::U1;
  case 2:
    return LinxISA::U2;
  case 3:
    return LinxISA::U3; // u#3
  case 4:
    return LinxISA::U4; // u#4 (encodes as t)
  default:
    return Register();
  }
}

class LinxISABlockify : public MachineFunctionPass {
public:
  static char ID;

  LinxISABlockify() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "Linx Blockify"; }

  bool runOnMachineFunction(MachineFunction &MF) override {
    const auto &TII = *MF.getSubtarget().getInstrInfo();
    const auto &TRI = *MF.getSubtarget().getRegisterInfo();

    const BitVector Reserved = TRI.getReservedRegs(MF);
    bool Changed = false;

    auto splitAfterCall = [&](MachineBasicBlock &MBB, MachineInstr &CallMI)
        -> MachineBasicBlock * {
      MachineFunction &MF = *MBB.getParent();
      auto *ContBB = MF.CreateMachineBasicBlock(MBB.getBasicBlock());
      MF.insert(std::next(MBB.getIterator()), ContBB);

      // Move everything after the call into the continuation block.
      auto SplitPt = std::next(CallMI.getIterator());
      ContBB->splice(ContBB->end(), &MBB, SplitPt, MBB.end());

      // Continuation inherits the original CFG edges; call block falls through to
      // the continuation after return.
      ContBB->transferSuccessorsAndUpdatePHIs(&MBB);
      MBB.addSuccessor(ContBB);
      return ContBB;
    };

    // Ensure PSEUDO_CALL ends a block. This matches BlockISA: the call is the
    // block's outgoing control-flow (encoded in the BSTART header), and the
    // return target is the next block (encoded via SETRET).
    SmallVector<MachineBasicBlock *, 32> CallSplitWorklist;
    CallSplitWorklist.reserve(MF.size());
    for (MachineBasicBlock &MBB : MF)
      CallSplitWorklist.push_back(&MBB);

    while (!CallSplitWorklist.empty()) {
      MachineBasicBlock *MBB = CallSplitWorklist.pop_back_val();
      for (MachineInstr &MI : *MBB) {
        if (MI.isDebugInstr())
          continue;
        if (MI.getOpcode() != LinxISA::PSEUDO_CALL)
          continue;

        auto Next = std::next(MI.getIterator());
        while (Next != MBB->end() && Next->isDebugInstr())
          ++Next;
        if (Next == MBB->end())
          break; // already ends the block

        MachineBasicBlock *ContBB = splitAfterCall(*MBB, MI);
        CallSplitWorklist.push_back(ContBB);
        Changed = true;
        break;
      }
    }

    auto findSetcInsertPt = [&](MachineBasicBlock &MBB, MachineInstr &Anchor,
                                Register LHS, Register RHS)
        -> MachineBasicBlock::iterator {
      MachineInstr *InsertAfter = nullptr;
      for (MachineInstr &MI : MBB) {
        if (&MI == &Anchor)
          break;
        if (MI.isDebugInstr() || isMarkerInstr(MI))
          continue;
        if ((LHS && MI.definesRegister(LHS, &TRI)) ||
            (RHS && MI.definesRegister(RHS, &TRI))) {
          InsertAfter = &MI;
        }
      }

      if (InsertAfter)
        return std::next(InsertAfter->getIterator());
      return Anchor.getIterator();
    };

    for (MachineBasicBlock &MBB : MF) {
      enum class ExitKind {
        Fall,
        Direct,
        Cond,
        Call,
        Ret,
        Ind,
        ICall,
      };

      ExitKind Kind = ExitKind::Fall;
      MachineBasicBlock *TargetBB = nullptr;   // DIRECT/COND
      MachineBasicBlock *ReturnBB = nullptr;   // CALL (return target)
      std::optional<MachineOperand> CallTargetOp; // CALL (callee)

      // Identify the last two non-debug, non-marker instructions.
      MachineInstr *Last = nullptr;
      MachineInstr *Prev = nullptr;
      for (auto It = MBB.rbegin(), E = MBB.rend(); It != E; ++It) {
        if (It->isDebugInstr() || isMarkerInstr(*It))
          continue;
        if (!Last) {
          Last = &*It;
          continue;
        }
        Prev = &*It;
        break;
      }

      // Recognize exit shape from the end of the block.
      if (Last) {
        switch (Last->getOpcode()) {
        case LinxISA::PSEUDO_CALL: {
          Kind = ExitKind::Call;
          CallTargetOp = Last->getOperand(0);
          if (!MBB.succ_empty())
            ReturnBB = *MBB.succ_begin();
          else
            ReturnBB = MBB.getNextNode();
          Last->eraseFromParent();
          Changed = true;
          break;
        }
        case LinxISA::PSEUDO_RET: {
          Kind = ExitKind::Ret;
          // Return target is provided via C.SETC.TGT ra.
          BuildMI(MBB, Last->getIterator(), DebugLoc(), TII.get(LinxISA::CSETC_TGT))
              .addReg(LinxISA::R10);
          Last->eraseFromParent();
          Changed = true;
          break;
        }
        case LinxISA::JR: {
          const Register Reg = Last->getOperand(0).getReg();
          Kind = (Reg == LinxISA::R10) ? ExitKind::Ret : ExitKind::Ind;
          BuildMI(MBB, Last->getIterator(), DebugLoc(), TII.get(LinxISA::CSETC_TGT))
              .addReg(Reg);
          Last->eraseFromParent();
          Changed = true;
          break;
        }
        case LinxISA::JUMP: {
          // Common lowering shape: `Bcc ...; JUMP ...` (no fallthrough).
          // In BlockISA we must pick a fallthrough block (the physically next
          // block) and encode the other successor in the BSTART header.
          if (Prev && (Prev->getOpcode() == LinxISA::BEQ ||
                       Prev->getOpcode() == LinxISA::BNE ||
                       Prev->getOpcode() == LinxISA::BLT ||
                       Prev->getOpcode() == LinxISA::BGE ||
                       Prev->getOpcode() == LinxISA::BLTU ||
                       Prev->getOpcode() == LinxISA::BGEU)) {
            MachineBasicBlock *BrTargetBB = Prev->getOperand(2).getMBB();
            MachineBasicBlock *JumpTargetBB = Last->getOperand(0).getMBB();
            MachineBasicBlock *FallthroughBB = MBB.getNextNode();
            if (!FallthroughBB)
              report_fatal_error("Linx: conditional+jump block requires fallthrough");

            unsigned SetcOpc = 0;
            Register LHSReg = Prev->getOperand(0).getReg();
            Register RHSReg = Prev->getOperand(1).getReg();
            auto pickSetc = [&](unsigned BrOpc) -> unsigned {
              switch (BrOpc) {
              case LinxISA::BEQ:
                return LinxISA::CSETC_EQ;
              case LinxISA::BNE:
                return LinxISA::CSETC_NE;
              case LinxISA::BLT:
                return LinxISA::SETC_LT;
              case LinxISA::BGE:
                return LinxISA::SETC_GE;
              case LinxISA::BLTU:
                return LinxISA::SETC_LTU;
              case LinxISA::BGEU:
                return LinxISA::SETC_GEU;
              default:
                llvm_unreachable("Unexpected branch opcode");
              }
            };
            auto invertBranch = [&](unsigned BrOpc) -> unsigned {
              switch (BrOpc) {
              case LinxISA::BEQ:
                return LinxISA::BNE;
              case LinxISA::BNE:
                return LinxISA::BEQ;
              case LinxISA::BLT:
                return LinxISA::BGE;
              case LinxISA::BGE:
                return LinxISA::BLT;
              case LinxISA::BLTU:
                return LinxISA::BGEU;
              case LinxISA::BGEU:
                return LinxISA::BLTU;
              default:
                llvm_unreachable("Unexpected branch opcode");
              }
            };

            // Prefer using the already-laid-out next block as fallthrough.
            unsigned BrOpcForSetc = Prev->getOpcode();
            if (FallthroughBB == JumpTargetBB) {
              Kind = ExitKind::Cond;
              TargetBB = BrTargetBB;
              SetcOpc = pickSetc(BrOpcForSetc);
            } else if (FallthroughBB == BrTargetBB) {
              Kind = ExitKind::Cond;
              TargetBB = JumpTargetBB;
              BrOpcForSetc = invertBranch(BrOpcForSetc);
              SetcOpc = pickSetc(BrOpcForSetc);
            } else {
              report_fatal_error("Linx: conditional+jump block without fallthrough to either successor");
            }

            auto SetcIt = findSetcInsertPt(MBB, *Prev, LHSReg, RHSReg);
            BuildMI(MBB, SetcIt, DebugLoc(), TII.get(SetcOpc))
                .addReg(LHSReg)
                .addReg(RHSReg);
            Prev->eraseFromParent();
            Last->eraseFromParent();
            Changed = true;
            break;
          }

          Kind = ExitKind::Direct;
          TargetBB = Last->getOperand(0).getMBB();
          Last->eraseFromParent();
          Changed = true;
          break;
        }
        case LinxISA::BEQ:
        case LinxISA::BNE:
        case LinxISA::BLT:
        case LinxISA::BGE:
        case LinxISA::BLTU:
        case LinxISA::BGEU: {
          Kind = ExitKind::Cond;
          TargetBB = Last->getOperand(2).getMBB();

          unsigned SetcOpc = 0;
          switch (Last->getOpcode()) {
          case LinxISA::BEQ:
            SetcOpc = LinxISA::CSETC_EQ;
            break;
          case LinxISA::BNE:
            SetcOpc = LinxISA::CSETC_NE;
            break;
          case LinxISA::BLT:
            SetcOpc = LinxISA::SETC_LT;
            break;
          case LinxISA::BGE:
            SetcOpc = LinxISA::SETC_GE;
            break;
          case LinxISA::BLTU:
            SetcOpc = LinxISA::SETC_LTU;
            break;
          case LinxISA::BGEU:
            SetcOpc = LinxISA::SETC_GEU;
            break;
          default:
            llvm_unreachable("Unexpected branch opcode");
          }

          Register LHSReg = Last->getOperand(0).getReg();
          Register RHSReg = Last->getOperand(1).getReg();
          auto SetcIt = findSetcInsertPt(MBB, *Last, LHSReg, RHSReg);
          BuildMI(MBB, SetcIt, DebugLoc(), TII.get(SetcOpc))
              .addReg(LHSReg)
              .addReg(RHSReg);
          Last->eraseFromParent();
          Changed = true;
          break;
        }
        default:
          break;
        }
      }

      // Peephole: fold `slli tmp, x, k; add dst, base, tmp` into
      // `add base, x<<k, ->dst` (uses the ISA shamt field).
      auto hasSingleNonDbgUseInMBB =
          [&](Register Reg, const MachineInstr *UserMI,
              const MachineInstr *IgnoreMI) -> bool {
        unsigned Count = 0;
        for (const MachineInstr &MI : MBB) {
          if (MI.isDebugInstr() || isMarkerInstr(MI))
            continue;
          if (&MI == IgnoreMI)
            continue;
          for (const MachineOperand &MO : MI.operands()) {
            if (!MO.isReg() || MO.isImplicit() || MO.isDef())
              continue;
            if (MO.getReg() != Reg)
              continue;
            ++Count;
            if (&MI != UserMI || Count > 1)
              return false;
          }
        }
        return Count == 1;
      };

      for (auto It = MBB.begin(); It != MBB.end();) {
        MachineInstr &ShiftMI = *It;
        if (ShiftMI.isDebugInstr() || isMarkerInstr(ShiftMI)) {
          ++It;
          continue;
        }

        const unsigned ShiftOpc = ShiftMI.getOpcode();
        const bool IsSLLI =
            (ShiftOpc == LinxISA::SLLIri || ShiftOpc == LinxISA::SLLIWri);
        if (!IsSLLI || ShiftMI.getNumOperands() < 3 || !ShiftMI.getOperand(2).isImm()) {
          ++It;
          continue;
        }

        const Register ShDst = ShiftMI.getOperand(0).getReg();
        const Register ShSrc = ShiftMI.getOperand(1).getReg();
        const int64_t ShAmt = ShiftMI.getOperand(2).getImm();
        if (ShAmt == 0) {
          ++It;
          continue;
        }

        auto NextIt = std::next(It);
        while (NextIt != MBB.end() &&
               (NextIt->isDebugInstr() || isMarkerInstr(*NextIt)))
          ++NextIt;
        if (NextIt == MBB.end()) {
          ++It;
          continue;
        }

        MachineInstr &AddMI = *NextIt;
        const unsigned AddOpc = AddMI.getOpcode();
        const bool IsMatch =
            (ShiftOpc == LinxISA::SLLIri && AddOpc == LinxISA::ADDrr) ||
            (ShiftOpc == LinxISA::SLLIWri && AddOpc == LinxISA::ADDWrr);
        if (!IsMatch || AddMI.getNumOperands() < 3) {
          ++It;
          continue;
        }

        const Register AddDst = AddMI.getOperand(0).getReg();
        Register AddOp1 = AddMI.getOperand(1).getReg();
        Register AddOp2 = AddMI.getOperand(2).getReg();

        Register Other;
        if (AddOp1 == ShDst)
          Other = AddOp2;
        else if (AddOp2 == ShDst)
          Other = AddOp1;
        else {
          ++It;
          continue;
        }

        // Ignore ShiftMI itself: register allocation may legally coalesce
        // `tmp` with `x`, yielding an in-place shift (e.g. `r3 = slli r3, k`).
        if (!hasSingleNonDbgUseInMBB(ShDst, &AddMI, &ShiftMI)) {
          ++It;
          continue;
        }

        const unsigned NewOpc =
            (ShiftOpc == LinxISA::SLLIri) ? LinxISA::ADDrr_SH : LinxISA::ADDWrr_SH;
        MachineInstr *NewMI =
            BuildMI(MBB, AddMI.getIterator(), AddMI.getDebugLoc(),
                    TII.get(NewOpc), AddDst)
                .addReg(Other)
                .addReg(ShSrc)
                .addImm(ShAmt)
                .getInstr();
        AddMI.eraseFromParent();
        ShiftMI.eraseFromParent();
        Changed = true;
        It = std::next(NewMI->getIterator());
      }

      // Insert `BSTART.STD <kind>` after PHIs.
      auto InsertBStart = MBB.begin();
      while (InsertBStart != MBB.end() && InsertBStart->isPHI())
        ++InsertBStart;

      // Remove any existing start marker (in case the pass runs twice).
      if (InsertBStart != MBB.end() &&
          (InsertBStart->getOpcode() == LinxISA::CBSTART_STD ||
           InsertBStart->getOpcode() == LinxISA::BSTART_STD_FALL ||
           InsertBStart->getOpcode() == LinxISA::BSTART_STD_DIRECT ||
           InsertBStart->getOpcode() == LinxISA::BSTART_STD_COND ||
           InsertBStart->getOpcode() == LinxISA::BSTART_STD_CALL ||
           InsertBStart->getOpcode() == LinxISA::BSTART_STD_IND ||
           InsertBStart->getOpcode() == LinxISA::BSTART_STD_ICALL ||
           InsertBStart->getOpcode() == LinxISA::BSTART_STD_RET)) {
        InsertBStart = MBB.erase(InsertBStart);
        Changed = true;
      }

      MachineInstr *BStartMI = nullptr;
      switch (Kind) {
      case ExitKind::Fall:
        BStartMI = BuildMI(MBB, InsertBStart, DebugLoc(),
                           TII.get(LinxISA::BSTART_STD_FALL))
                       .getInstr();
        break;
      case ExitKind::Direct:
        BStartMI = BuildMI(MBB, InsertBStart, DebugLoc(),
                           TII.get(LinxISA::BSTART_STD_DIRECT))
                       .addMBB(TargetBB)
                       .getInstr();
        break;
      case ExitKind::Cond:
        BStartMI = BuildMI(MBB, InsertBStart, DebugLoc(),
                           TII.get(LinxISA::BSTART_STD_COND))
                       .addMBB(TargetBB)
                       .getInstr();
        break;
      case ExitKind::Call: {
        if (!CallTargetOp)
          report_fatal_error("Linx: missing call target operand");
        BStartMI = BuildMI(MBB, InsertBStart, DebugLoc(),
                           TII.get(LinxISA::BSTART_STD_CALL))
                       .add(*CallTargetOp)
                       .getInstr();
        // Set return target for the call (ra = PC + imm20<<1).
        if (ReturnBB) {
          ReturnBB->setLabelMustBeEmitted();
          BuildMI(MBB, std::next(BStartMI->getIterator()), DebugLoc(),
                  TII.get(LinxISA::SETRET))
              .addMBB(ReturnBB);
        }
        break;
      }
      case ExitKind::Ret:
        BStartMI = BuildMI(MBB, InsertBStart, DebugLoc(),
                           TII.get(LinxISA::BSTART_STD_RET))
                       .getInstr();
        break;
      case ExitKind::Ind:
        BStartMI = BuildMI(MBB, InsertBStart, DebugLoc(),
                           TII.get(LinxISA::BSTART_STD_IND))
                       .getInstr();
        break;
      case ExitKind::ICall:
        BStartMI = BuildMI(MBB, InsertBStart, DebugLoc(),
                           TII.get(LinxISA::BSTART_STD_ICALL))
                       .getInstr();
        break;
      }
      Changed = true;

      // Assign block-local single-use values to the hand queues.
      //
      // Hardware semantics: every definition to `t` shifts older values into
      // `t#1..t#4` (similarly for `u` -> `u#1..u#4`). We only rewrite values
      // whose sole use occurs within the next 4 queued defs for a chosen hand.
      struct Segment {
        Register Reg;
        MachineInstr *DefMI = nullptr;
        unsigned DefOpNo = 0;
        unsigned DefIdx = 0;
        MachineInstr *UseMI = nullptr;
        unsigned UseOpNo = 0;
        unsigned UseIdx = 0;
        unsigned UseCount = 0;
        bool ClosedByRedef = false;
      };

      SmallVector<Segment, 32> Segs;
      DenseMap<unsigned, unsigned> ActiveSeg; // PhysReg.id() -> Segs index
      unsigned InstIdx = 0;

      auto isCandidatePhysReg = [&](Register Reg) -> bool {
        if (!Reg || !Reg.isPhysical())
          return false;
        if (Reg.id() >= Reserved.size())
          return false;
        if (Reserved.test(Reg.id()))
          return false;
        return true;
      };

      auto isLiveOutOfBlock = [&](Register Reg) -> bool {
        for (const MachineBasicBlock *Succ : MBB.successors()) {
          if (Succ && Succ->isLiveIn(Reg))
            return true;
        }
        return false;
      };

      for (MachineInstr &MI : MBB) {
        if (MI.isDebugInstr() || isMarkerInstr(MI))
          continue;

        // Process uses before defs to handle read-modify-write forms.
        for (unsigned OpNo = 0; OpNo < MI.getNumOperands(); ++OpNo) {
          MachineOperand &MO = MI.getOperand(OpNo);
          if (!MO.isReg() || MO.isImplicit() || MO.isDef())
            continue;

          Register Reg = MO.getReg();
          if (!isCandidatePhysReg(Reg))
            continue;

          auto It = ActiveSeg.find(Reg.id());
          if (It == ActiveSeg.end())
            continue;

          Segment &S = Segs[It->second];
          ++S.UseCount;
          if (S.UseCount == 1) {
            S.UseMI = &MI;
            S.UseOpNo = OpNo;
            S.UseIdx = InstIdx;
          }
        }

        for (unsigned OpNo = 0; OpNo < MI.getNumOperands(); ++OpNo) {
          MachineOperand &MO = MI.getOperand(OpNo);
          if (!MO.isReg() || MO.isImplicit() || !MO.isDef())
            continue;

          Register Reg = MO.getReg();
          if (!isCandidatePhysReg(Reg))
            continue;

          // Close the previous segment (if any) for this physical register.
          auto It = ActiveSeg.find(Reg.id());
          if (It != ActiveSeg.end()) {
            Segs[It->second].ClosedByRedef = true;
            ActiveSeg.erase(It);
          }

          Segment S;
          S.Reg = Reg;
          S.DefMI = &MI;
          S.DefOpNo = OpNo;
          S.DefIdx = InstIdx;
          ActiveSeg[Reg.id()] = Segs.size();
          Segs.push_back(S);
        }

        ++InstIdx;
      }

      SmallVector<unsigned, 32> CandidateSegs;
      CandidateSegs.reserve(Segs.size());
      for (unsigned I = 0; I < Segs.size(); ++I) {
        const Segment &S = Segs[I];
        if (!S.DefMI || !S.UseMI)
          continue;
        if (S.UseCount != 1)
          continue;
        if (S.UseIdx <= S.DefIdx)
          continue;
        // If the value is live-out, we can't remap it to the hand queue.
        if (!S.ClosedByRedef && isLiveOutOfBlock(S.Reg))
          continue;
        CandidateSegs.push_back(I);
      }

      if (!CandidateSegs.empty()) {
        enum class Hand : uint8_t { None, T, U };

        auto isTCompressibleDef = [&](const MachineInstr &MI) -> bool {
          switch (MI.getOpcode()) {
          case LinxISA::ADDrr:
          case LinxISA::SUBrr:
          case LinxISA::ANDrr:
          case LinxISA::ORrr:
            return true;
          case LinxISA::ADDIri:
          case LinxISA::SUBIri: {
            if (MI.getNumOperands() >= 3 && MI.getOperand(2).isImm())
              return isInt<5>(MI.getOperand(2).getImm());
            return false;
          }
          case LinxISA::LWI:
          case LinxISA::LDI: {
            if (MI.getNumOperands() >= 3 && MI.getOperand(2).isImm())
              return isInt<5>(MI.getOperand(2).getImm());
            return false;
          }
          default:
            return false;
          }
        };

        // Greedy assignment in reverse def order. For each candidate, choose a
        // hand where the value is still within the 4-deep queue at its use.
        SmallVector<unsigned, 32> Sorted = CandidateSegs;
        llvm::sort(Sorted, [&](unsigned A, unsigned B) {
          return Segs[A].DefIdx > Segs[B].DefIdx;
        });

        SmallVector<unsigned, 32> AssignedT;
        SmallVector<unsigned, 32> AssignedU;
        SmallVector<Hand, 32> AssignedHand(Segs.size(), Hand::None);
        SmallVector<unsigned, 32> AssignedBetween(Segs.size(), 0);

        auto countBetween = [&](ArrayRef<unsigned> Assigned, const Segment &S) {
          unsigned Between = 0;
          for (unsigned J : Assigned) {
            const Segment &B = Segs[J];
            if (B.DefIdx > S.DefIdx && B.DefIdx < S.UseIdx)
              ++Between;
          }
          return Between;
        };

        for (unsigned I : Sorted) {
          const Segment &S = Segs[I];
          unsigned BetweenT = countBetween(AssignedT, S);
          unsigned BetweenU = countBetween(AssignedU, S);

          const bool CanT = BetweenT <= 3;
          const bool CanU = BetweenU <= 3;
          if (!CanT && !CanU)
            continue;

          Hand H = Hand::None;
          unsigned Between = 0;

          // Prefer mapping defs that can become 16-bit ops to the T-hand.
          const bool PreferT = isTCompressibleDef(*S.DefMI);

          if (PreferT && CanT) {
            H = Hand::T;
            Between = BetweenT;
          } else if (CanT && CanU) {
            if (BetweenT <= BetweenU) {
              H = Hand::T;
              Between = BetweenT;
            } else {
              H = Hand::U;
              Between = BetweenU;
            }
          } else if (CanT) {
            H = Hand::T;
            Between = BetweenT;
          } else {
            H = Hand::U;
            Between = BetweenU;
          }

          AssignedHand[I] = H;
          AssignedBetween[I] = Between;
          if (H == Hand::T)
            AssignedT.push_back(I);
          else if (H == Hand::U)
            AssignedU.push_back(I);
        }

        for (unsigned I : Sorted) {
          const Segment &S = Segs[I];
          Hand H = AssignedHand[I];
          if (H == Hand::None)
            continue;

          const unsigned Index = AssignedBetween[I] + 1;
          Register UseReg =
              (H == Hand::T) ? getTQueueUseReg(Index) : getUQueueUseReg(Index);
          if (!UseReg)
            continue;

          MachineOperand &DefMO = S.DefMI->getOperand(S.DefOpNo);
          MachineOperand &UseMO = S.UseMI->getOperand(S.UseOpNo);
          DefMO.setReg(H == Hand::T ? LinxISA::U4 : LinxISA::U3); // "->t"/"->u"
          UseMO.setReg(UseReg); // "t#k"/"u#k"
          Changed = true;
        }
      }

      // Insert `BSTOP` only for the final laid-out block. When a `BSTART.*`
      // follows, it already terminates the previous block.
      auto InsertBStop = MBB.end();
      while (InsertBStop != MBB.begin() && std::prev(InsertBStop)->isDebugInstr())
        --InsertBStop;

      if (MBB.getNextNode()) {
        if (InsertBStop != MBB.begin() &&
            std::prev(InsertBStop)->getOpcode() == LinxISA::BSTOP) {
          std::prev(InsertBStop)->eraseFromParent();
          Changed = true;
        }
      } else {
        if (InsertBStop == MBB.begin() ||
            std::prev(InsertBStop)->getOpcode() != LinxISA::BSTOP) {
          BuildMI(MBB, InsertBStop, DebugLoc(), TII.get(LinxISA::BSTOP));
          Changed = true;
        }
      }
    }

    return Changed;
  }
};

} // end anonymous namespace

char LinxISABlockify::ID = 0;

INITIALIZE_PASS(LinxISABlockify, "linx-blockify", "Linx Blockify", false,
                false)

FunctionPass *llvm::createLinxISABlockifyPass() { return new LinxISABlockify(); }
