//===-- LinxISAAsmPrinter.cpp - LinxISA Assembly Printer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISA.h"
#include "LinxISAMCInstLower.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "MCTargetDesc/LinxISAOpcodeTables.h"
#include "TargetInfo/LinxISATargetInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class LinxISAAsmPrinter : public llvm::AsmPrinter {
  std::unique_ptr<LinxISAMCInstLower> MCInstLowering;
  SmallPtrSet<const MachineBasicBlock *, 32> BodyLabelsEmitted;
  SmallPtrSet<const MachineInstr *, 32> SkippedFusedSetRet;

public:
  explicit LinxISAAsmPrinter(TargetMachine &TM,
                             std::unique_ptr<MCStreamer> Streamer)
      : llvm::AsmPrinter(TM, std::move(Streamer)) {}

  StringRef getPassName() const override { return "Linx Assembly Printer"; }

  bool runOnMachineFunction(MachineFunction &MF) override {
    MCInstLowering = std::make_unique<LinxISAMCInstLower>(
        OutContext, *this, *MF.getSubtarget().getRegisterInfo());
    BodyLabelsEmitted.clear();
    SkippedFusedSetRet.clear();
    return llvm::AsmPrinter::runOnMachineFunction(MF);
  }

  void emitInstruction(const MachineInstr *MI) override;

  static char ID;
};

} // end anonymous namespace

void LinxISAAsmPrinter::emitInstruction(const MachineInstr *MI) {
  if (SkippedFusedSetRet.contains(MI))
    return;
  if (MI->isDebugInstr())
    return;

  switch (MI->getOpcode()) {
  // Call frame pseudos should have been eliminated by PEI.
  case LinxISA::ADJCALLSTACKDOWN:
  case LinxISA::ADJCALLSTACKUP:
    return;
  default:
    break;
  }

  bool Emitted = false;
  if (MI->getOpcode() == LinxISA::BSTART_STD_CALL &&
      OutStreamer->hasRawTextSupport()) {
    MachineBasicBlock *MBB = const_cast<MachineBasicBlock *>(MI->getParent());
    if (MBB) {
      auto It = const_cast<MachineInstr *>(MI)->getIterator();
      auto NextIt = std::next(It);
      while (NextIt != MBB->end() && NextIt->isDebugInstr())
        ++NextIt;

      if (NextIt != MBB->end() && NextIt->getOpcode() == LinxISA::SETRET) {
        MCInst BStartInst;
        MCInstLowering->Lower(MI, BStartInst);
        MCInst SetRetInst;
        MCInstLowering->Lower(&*NextIt, SetRetInst);

        SmallString<128> Line;
        raw_svector_ostream OS(Line);
        OS << "BSTART\tCALL, ";

        if (BStartInst.getNumOperands() >= 1) {
          const MCOperand &TargetOp = BStartInst.getOperand(0);
          if (TargetOp.isExpr())
            MAI->printExpr(OS, *TargetOp.getExpr());
          else if (TargetOp.isImm())
            OS << TargetOp.getImm();
        }

        OS << ", ra=";
        if (SetRetInst.getNumOperands() >= 1) {
          const MCOperand &RetOp = SetRetInst.getOperand(0);
          if (RetOp.isExpr())
            MAI->printExpr(OS, *RetOp.getExpr());
          else if (RetOp.isImm())
            OS << RetOp.getImm();
        }

        OutStreamer->emitRawText(OS.str());
        SkippedFusedSetRet.insert(&*NextIt);
        Emitted = true;
      }
    }
  }

  if (!Emitted) {
    MCSubtargetInfo STI = getSubtargetInfo();
    MCInst TmpInst;
    MCInstLowering->Lower(MI, TmpInst);
    OutStreamer->emitInstruction(TmpInst, STI);
  }

  // For readability, emit a "body" label immediately after block-start markers.
  // This keeps the canonical MBB symbol at the BSTART address (for fixups),
  // while providing a label at the first non-marker instruction.
  switch (MI->getOpcode()) {
  case LinxISA::CBSTART_STD:
  case LinxISA::BSTART_STD_FALL:
  case LinxISA::BSTART_STD_DIRECT:
  case LinxISA::BSTART_STD_COND:
  case LinxISA::BSTART_STD_CALL:
  case LinxISA::BSTART_STD_IND:
  case LinxISA::BSTART_STD_ICALL:
  case LinxISA::BSTART_STD_RET: {
    if (!OutStreamer->hasRawTextSupport())
      break;

    const MachineBasicBlock *MBB = MI->getParent();
    if (!MBB || BodyLabelsEmitted.contains(MBB))
      break;
    BodyLabelsEmitted.insert(MBB);

    SmallString<64> Name;
    if (MCSymbol *Sym = MBB->getSymbol()) {
      StringRef BBName = Sym->getName();
      if (!BBName.empty())
        Name += BBName;
    }
    if (Name.empty()) {
      const MachineFunction *MF = MBB->getParent();
      raw_svector_ostream OS(Name);
      OS << "BB" << (MF ? MF->getFunctionNumber() : 0) << '_' << MBB->getNumber();
    }
    Name += ".body";
    OutStreamer->emitLabel(OutContext.getOrCreateSymbol(Name));
    break;
  }
  default:
    break;
  }
}

char LinxISAAsmPrinter::ID = 0;

INITIALIZE_PASS(LinxISAAsmPrinter, "linx-asm-printer",
                "Linx Assembly Printer", false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLinxISAAsmPrinter() {
  RegisterAsmPrinter<LinxISAAsmPrinter> X32(getTheLinx32Target());
  RegisterAsmPrinter<LinxISAAsmPrinter> X64(getTheLinx64Target());
}
