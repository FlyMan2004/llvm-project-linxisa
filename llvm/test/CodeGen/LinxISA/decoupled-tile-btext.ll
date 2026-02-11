; RUN: llc -mtriple=linx64 -O2 < %s | FileCheck %s

declare <1024 x i32> @llvm.linx.tma.tload(ptr, i32)
declare void @llvm.linx.tma.tstore(ptr, <1024 x i32>, i32)

define void @tload_store(ptr %src, ptr %dst) {
entry:
  %t = call <1024 x i32> @llvm.linx.tma.tload(ptr %src, i32 8)
  call void @llvm.linx.tma.tstore(ptr %dst, <1024 x i32> %t, i32 8)
  ret void
}

; CHECK-LABEL: tload_store:
; CHECK:      BSTART.TMA{{[[:space:]]+}}TLOAD,
; CHECK:      BSTART.TMA{{[[:space:]]+}}TSTORE,
; CHECK-NOT:  B.TEXT
