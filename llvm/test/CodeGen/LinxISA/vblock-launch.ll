; RUN: llc -mtriple=linx64 -O2 < %s | FileCheck %s

declare void @llvm.linx.vblock.launch(i32, ptr, i64, i64, i64, i32)

define void @vseq() {
entry:
  call void @llvm.linx.vblock.launch(i32 0, ptr null, i64 2, i64 3, i64 4, i32 0)
  ret void
}

; CHECK-LABEL: vseq:
; CHECK:      BSTART.VSEQ
; CHECK-NEXT: B.TEXT {{\.__linx_empty_body\.[0-9]+}}
; CHECK:      B.DIM{{.*->lb0}}
; CHECK:      B.DIM{{.*->lb1}}
; CHECK:      B.DIM{{.*->lb2}}
; CHECK:      {{^\.__linx_empty_body\.[0-9]+:}}
; CHECK-NEXT: C.BSTOP
