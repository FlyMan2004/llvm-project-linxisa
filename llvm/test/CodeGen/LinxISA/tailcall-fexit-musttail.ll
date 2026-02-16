; RUN: llc -mtriple=linx64 -O2 < %s | FileCheck %s

declare i64 @tail_target(i64)
@tail_fn_ptr = global ptr @tail_target

define i64 @tail_direct(i64 %x) {
entry:
  %r = musttail call i64 @tail_target(i64 %x)
  ret i64 %r
}

define i64 @tail_indirect(i64 %x) {
entry:
  %fn = load ptr, ptr @tail_fn_ptr
  %r = musttail call i64 %fn(i64 %x)
  ret i64 %r
}

; CHECK-LABEL: tail_direct:
; CHECK: FENTRY
; CHECK: FEXIT
; CHECK: BSTART{{[[:space:]]+}}DIRECT, tail_target{{(@plt)?}}
; CHECK-NOT: BSTART{{[[:space:]]+}}CALL
; CHECK-NOT: FRET.STK

; CHECK-LABEL: tail_indirect:
; CHECK: FENTRY
; CHECK: FEXIT
; CHECK: C.BSTART{{(\.STD)?}}{{[[:space:]]+}}IND
; CHECK: c.setc.tgt
; CHECK-NOT: BSTART{{[[:space:]]+}}CALL
; CHECK-NOT: FRET.STK
