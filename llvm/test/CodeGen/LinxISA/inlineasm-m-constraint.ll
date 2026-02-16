; RUN: llc -mtriple=linx64 -O0 < %s | FileCheck %s

; Ensure Linx lowers and prints inline-asm memory constraints instead of
; crashing in DAG isel.

define void @inlineasm_m(ptr %p) {
entry:
  call void asm sideeffect "# MEM=$0", "m,~{memory}"(ptr %p)
  ret void
}

; CHECK-LABEL: inlineasm_m:
; CHECK: # MEM=[
; CHECK-SAME: ]
