; RUN: llc -mtriple=linx64 -O2 --linx-enable-neg-imm-canon < %s | FileCheck %s

define i64 @add_neg_small(i64 %x) {
entry:
  %a = add i64 %x, -9
  ret i64 %a
}

; CHECK-LABEL: add_neg_small:
; CHECK: subi a0, 9,{{[[:space:]]+}}->a0

define i64 @add_neg_large(i64 %x) {
entry:
  %a = add i64 %x, -50000
  ret i64 %a
}

; CHECK-LABEL: add_neg_large:
; CHECK: hl.subi a0, 50000,{{[[:space:]]+}}->a0

define i64 @add_lhs_const(i64 %x) {
entry:
  %a = add i64 -13, %x
  ret i64 %a
}

; CHECK-LABEL: add_lhs_const:
; CHECK: subi a0, 13,{{[[:space:]]+}}->a0

define i64 @sub_neg_small(i64 %x) {
entry:
  %a = sub i64 %x, -7
  ret i64 %a
}

; CHECK-LABEL: sub_neg_small:
; CHECK: addi a0, 7,{{[[:space:]]+}}->a0

define i64 @sub_neg_large(i64 %x) {
entry:
  %a = sub i64 %x, -70000
  ret i64 %a
}

; CHECK-LABEL: sub_neg_large:
; CHECK: hl.addi a0, 70000,{{[[:space:]]+}}->a0
