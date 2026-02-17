; RUN: llc -mtriple=linx64 -O2 \
; RUN:   --linx-simt-autovec=1 --linx-simt-autovec-mode=mseq \
; RUN:   --linx-simt-autovec-remarks=%t.remarks.json \
; RUN:   < %s | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=REMARK < %t.remarks.json

define void @dot_like_loop(ptr nocapture noundef readonly %a,
                           ptr nocapture noundef readonly %b,
                           ptr nocapture noundef writeonly %out) {
entry:
  br label %loop

loop:                                             ; preds = %loop, %entry
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi float [ 0.000000e+00, %entry ], [ %acc.next, %loop ]
  %pa = getelementptr inbounds float, ptr %a, i64 %i
  %va = load float, ptr %pa, align 4
  %pb = getelementptr inbounds float, ptr %b, i64 %i
  %vb = load float, ptr %pb, align 4
  %prod = fmul float %va, %vb
  %acc.next = fadd float %acc, %prod
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 64
  br i1 %done, label %exit, label %loop

exit:                                             ; preds = %loop
  store float %acc.next, ptr %out, align 4
  ret void
}

; ASM-LABEL: dot_like_loop:
; ASM-NOT: BSTART.MSEQ
; ASM-NOT: BSTART.MPAR

; REMARK: "function":"dot_like_loop"
; REMARK: "status":"reject"
; REMARK: "reason":"matmul_requires_cube_intrinsic"
