; RUN: llc -mtriple=linx64 -O2 < %s | FileCheck %s

declare <1024 x i32> @llvm.linx.tma.tload(ptr, i32)
declare void @llvm.linx.tma.tstore(ptr, <1024 x i32>, i32)
declare <1024 x i32> @llvm.linx.cube.mamulb(<1024 x i32>, <1024 x i32>, i32, i32, i32)

define void @cube_tile(ptr %a, ptr %b, ptr %dst) {
entry:
  %ta = call <1024 x i32> @llvm.linx.tma.tload(ptr %a, i32 8)
  %tb = call <1024 x i32> @llvm.linx.tma.tload(ptr %b, i32 8)
  %out = call <1024 x i32> @llvm.linx.cube.mamulb(<1024 x i32> %ta, <1024 x i32> %tb, i32 4, i32 4, i32 4)
  call void @llvm.linx.tma.tstore(ptr %dst, <1024 x i32> %out, i32 8)
  ret void
}

; CHECK-LABEL: cube_tile:
; CHECK: BSTART.TMA{{[[:space:]]+}}TLOAD,
; CHECK: BSTART.TMA{{[[:space:]]+}}TLOAD,
; CHECK: BSTART.CUBE{{[[:space:]]+}}MAMULB,
; CHECK: BSTART.CUBE{{[[:space:]]+}}ACCCVT,
; CHECK: BSTART.TMA{{[[:space:]]+}}TSTORE,
