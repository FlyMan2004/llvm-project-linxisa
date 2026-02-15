; RUN: llc -mtriple=linx64-unknown-linux-musl -o - %s | FileCheck %s

define i32 @f(i64 %n) nounwind {
entry:
  %p = alloca i8, i64 %n, align 16
  %q = ptrtoint ptr %p to i64
  %r = trunc i64 %q to i32
  ret i32 %r
}

; CHECK-LABEL: f:
; CHECK: addi
; CHECK: sub	sp,
; CHECK: ->sp
