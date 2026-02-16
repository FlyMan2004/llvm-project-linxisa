; RUN: rm -f %t.remarks.json
; RUN: llc -mtriple=linx64 -O2 --linx-simt-autovec=1 --linx-simt-autovec-mode=auto --linx-simt-autovec-remarks=%t.remarks.json < %s | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=REMARK < %t.remarks.json

define void @s111(ptr nocapture %a, ptr nocapture %b) {
entry:
  %v = load float, ptr %b, align 4
  store float %v, ptr %a, align 4
  ret void
}

; ASM-LABEL: s111:
; ASM-NOT: BSTART.MSEQ
; ASM-NOT: BSTART.MPAR

; REMARK: "function":"s111"
; REMARK: "status":"reject"
; REMARK: "reason":"no_loop_candidate"
; REMARK-NOT: "fallback_marker"
