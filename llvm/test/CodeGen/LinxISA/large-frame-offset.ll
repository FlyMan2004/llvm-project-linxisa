; RUN: llc -mtriple=linx64-unknown-linux-musl -o - %s | FileCheck %s

define i64 @large_frame_access() nounwind {
entry:
  %arr = alloca [8192 x i8], align 16
  %gep = getelementptr inbounds [8192 x i8], ptr %arr, i64 0, i64 8184
  store i8 1, ptr %gep, align 1
  %v = load i8, ptr %gep, align 1
  %ext = zext i8 %v to i64
  ret i64 %ext
}

; CHECK-LABEL: large_frame_access:
; CHECK: hl.addi
; CHECK: sbi
