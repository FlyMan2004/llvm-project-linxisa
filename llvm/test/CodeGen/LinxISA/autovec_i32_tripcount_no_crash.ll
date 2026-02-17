; RUN: llc -mtriple=linx64 -O2 \
; RUN:   --linx-simt-autovec=1 --linx-simt-autovec-mode=mseq \
; RUN:   < %s | FileCheck %s

define void @i32_tripcount_store(ptr nocapture noundef writeonly %dst,
                                 ptr nocapture noundef readonly %src) {
entry:
  br label %loop

loop:                                             ; preds = %loop, %entry
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %idx = zext i32 %i to i64
  %sp = getelementptr inbounds float, ptr %src, i64 %idx
  %v = load float, ptr %sp, align 4
  %dp = getelementptr inbounds float, ptr %dst, i64 %idx
  store float %v, ptr %dp, align 4
  %i.next = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %i.next, 64
  br i1 %done, label %exit, label %loop

exit:                                             ; preds = %loop
  ret void
}

; CHECK-LABEL: i32_tripcount_store:
; CHECK: BSTART.MSEQ
