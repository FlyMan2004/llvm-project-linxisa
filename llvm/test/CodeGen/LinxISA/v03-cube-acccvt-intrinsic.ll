; RUN: llc -mtriple=linx64 -O2 < %s | FileCheck %s

%linx.tile = type target("linx.tile")

declare %linx.tile @llvm.linx.tile.tload(ptr, i32, i32, i64, i64, i64, i64)
declare void @llvm.linx.tile.tstore(ptr, %linx.tile, i32, i32, i64, i64, i64, i64)
declare %linx.tile @llvm.linx.cube.acccvt(%linx.tile, i32, i32, i64, i64)

define void @acccvt_roundtrip(ptr %src, ptr %dst) {
entry:
  %acc = call %linx.tile @llvm.linx.tile.tload(ptr %src, i32 8, i32 0, i64 0, i64 8, i64 8, i64 0)
  %out = call %linx.tile @llvm.linx.cube.acccvt(%linx.tile %acc, i32 8, i32 1, i64 7, i64 0)
  call void @llvm.linx.tile.tstore(ptr %dst, %linx.tile %out, i32 8, i32 1, i64 0, i64 8, i64 8, i64 0)
  ret void
}

; CHECK-LABEL: acccvt_roundtrip:
; CHECK: BSTART.TLOAD
; CHECK: BSTART.ACCCVT
; CHECK: B.ARG{{.*}}7
; CHECK: BSTART.TSTORE
