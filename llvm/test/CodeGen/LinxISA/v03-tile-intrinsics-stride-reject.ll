; RUN: not --crash llc -mtriple=linx64 -O2 < %s 2>&1 | FileCheck %s

%linx.tile = type target("linx.tile")

declare %linx.tile @llvm.linx.tile.tload(ptr, i32, i32, i64, i64, i64, i64)

define void @bad_stride(ptr %src) {
entry:
  %t = call %linx.tile @llvm.linx.tile.tload(ptr %src, i32 8, i32 0, i64 0, i64 8, i64 8, i64 16)
  ret void
}

; CHECK: Linx: tile.tload requires stride_bytes=0 in strict-v0.3
