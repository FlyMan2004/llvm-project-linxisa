; RUN: rm -f %t.mseq.json %t.mpar.json
; RUN: llc -mtriple=linx64 -O2 --linx-simt-autovec=1 --linx-simt-autovec-mode=mseq --linx-simt-autovec-remarks=%t.mseq.json < %s > /dev/null
; RUN: llc -mtriple=linx64 -O2 --linx-simt-autovec=1 --linx-simt-autovec-mode=mpar-safe --linx-simt-autovec-remarks=%t.mpar.json < %s > /dev/null
; RUN: FileCheck %s --check-prefix=MSEQ < %t.mseq.json
; RUN: FileCheck %s --check-prefix=MPAR < %t.mpar.json

define void @store_walk(ptr nocapture %a, ptr nocapture %b, ptr nocapture %c) #0 {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %inc, %loop ]
  %bp = getelementptr inbounds float, ptr %b, i64 %i
  %bv = load float, ptr %bp, align 4
  %cp = getelementptr inbounds float, ptr %c, i64 %i
  %cv = load float, ptr %cp, align 4
  %sum = fadd float %bv, %cv
  %ap = getelementptr inbounds float, ptr %a, i64 %i
  store float %sum, ptr %ap, align 4
  %inc = add nuw i64 %i, 1
  %cmp = icmp ult i64 %inc, 128
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

define void @store_walk_parallel(ptr nocapture %a, ptr nocapture %b, ptr nocapture %c) #0 {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %inc, %loop ]
  %bp = getelementptr inbounds float, ptr %b, i64 %i
  %bv = load float, ptr %bp, align 4
  %cp = getelementptr inbounds float, ptr %c, i64 %i
  %cv = load float, ptr %cp, align 4
  %sum = fadd float %bv, %cv
  %ap = getelementptr inbounds float, ptr %a, i64 %i
  store float %sum, ptr %ap, align 4
  %inc = add nuw i64 %i, 1
  %cmp = icmp ult i64 %inc, 128
  br i1 %cmp, label %loop, label %exit, !llvm.loop !0

exit:
  ret void
}

attributes #0 = { noinline nounwind }
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.vectorize.enable", i1 true}

; MSEQ: "function":"store_walk"
; MSEQ: "status":"lowered"
; MSEQ: "configured_mode":"mseq"
; MSEQ: "selected_mode":"mseq"

; MPAR: "function":"store_walk"
; MPAR: "status":"lowered"
; MPAR: "configured_mode":"mpar-safe"
; MPAR: "selected_mode":"mseq"

; MSEQ: "function":"store_walk_parallel"
; MSEQ: "status":"lowered"
; MSEQ: "configured_mode":"mseq"
; MSEQ: "selected_mode":"mseq"

; MPAR: "function":"store_walk_parallel"
; MPAR: "status":"lowered"
; MPAR: "configured_mode":"mpar-safe"
; MPAR: "selected_mode":"mpar"
