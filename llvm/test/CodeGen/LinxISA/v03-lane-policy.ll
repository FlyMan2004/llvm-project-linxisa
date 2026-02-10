; RUN: llc -mtriple=linx64 -O2 < %s | FileCheck %s

declare void @llvm.linx.vblock.launch(i32, ptr, i64, i64, i64, i32)

define void @vseq() {
entry:
  call void @llvm.linx.vblock.launch(i32 0, ptr null, i64 2, i64 3, i64 4, i32 0)
  ret void
}

define void @vpar() {
entry:
  call void @llvm.linx.vblock.launch(i32 1, ptr null, i64 5, i64 6, i64 7, i32 0)
  ret void
}

; CHECK-LABEL: vseq:
; CHECK: BSTART.VSEQ
; CHECK: B.DIM{{.*->LB0}}
; CHECK: B.DIM{{.*->LB1}}
; CHECK: B.DIM{{.*->LB2}}
; CHECK-LABEL: vpar:
; CHECK: BSTART.VPAR
; CHECK: B.DIM{{.*->LB0}}
; CHECK: B.DIM{{.*->LB1}}
; CHECK: B.DIM{{.*->LB2}}
; CHECK-NOT: BSTART{{.}}PAR
