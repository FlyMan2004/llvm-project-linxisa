; RUN: llc -mtriple=linx64 -O2 < %s | FileCheck %s

define i64 @foo(i64 %a) {
entry:
  %slot = alloca i64, align 8
  store i64 %a, ptr %slot, align 8
  %v = load i64, ptr %slot, align 8
  ret i64 %v
}

; CHECK-LABEL: foo:
; CHECK-NOT: C.BSTART
; CHECK: FENTRY
; CHECK: C.BSTART
; CHECK: FRET.STK
; CHECK-NOT: C.BSTOP
