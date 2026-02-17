; RUN: llc -mtriple=linx64 -O2 --linx-enable-neg-imm-canon --linx-codesize-balance-mode=balanced < %s | FileCheck %s --check-prefix=BAL
; RUN: llc -mtriple=linx64 -O2 --linx-enable-neg-imm-canon --linx-codesize-balance-mode=static-first < %s | FileCheck %s --check-prefix=STATIC

define i64 @f_default(i64 %x) {
entry:
  %a = add i64 %x, -50000
  ret i64 %a
}

define i64 @f_optsize(i64 %x) optsize {
entry:
  %a = add i64 %x, -50000
  ret i64 %a
}

; BAL-LABEL: f_default:
; BAL-NOT: hl.subi
; BAL-LABEL: f_optsize:
; BAL: hl.subi a0, 50000,{{[[:space:]]+}}->a0

; STATIC-LABEL: f_default:
; STATIC: hl.subi a0, 50000,{{[[:space:]]+}}->a0
