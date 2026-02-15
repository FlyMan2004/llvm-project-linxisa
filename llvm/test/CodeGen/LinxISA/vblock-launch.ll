; RUN: llc -mtriple=linx64 -O2 < %s | FileCheck %s

declare void @llvm.linx.vblock.launch(i32, ptr, i64, i64, i64, i32, i64, i64, i64, i64, i64, i64)

define void @vseq() {
entry:
  call void @llvm.linx.vblock.launch(i32 0, ptr null, i64 2, i64 3, i64 4, i32 0,
                                     i64 0, i64 0, i64 0, i64 0, i64 0, i64 0)
  ret void
}

define void @vseq_rdc() #0 {
entry:
  call void @llvm.linx.vblock.launch(i32 0, ptr null, i64 8, i64 1, i64 1, i32 0,
                                     i64 0, i64 0, i64 0, i64 0, i64 0, i64 0)
  ret void
}

; CHECK-LABEL: vseq:
; CHECK:      BSTART.MSEQ
; CHECK-NEXT: B.TEXT {{\.__linx_vblock_body\.[0-9]+}}
; CHECK:      C.B.DIMI{{[[:space:]]+}}2,{{.*->lb0}}
; CHECK:      C.B.DIMI{{[[:space:]]+}}3,{{.*->lb1}}
; CHECK:      C.B.DIMI{{[[:space:]]+}}4,{{.*->lb2}}
; CHECK:      {{^\.__linx_vblock_body\.[0-9]+:}}
; CHECK:      C.BSTOP

; CHECK-LABEL: vseq_rdc:
; CHECK:      BSTART.MSEQ
; CHECK:      B.TEXT {{\.__linx_vblock_body\.[0-9]+}}
; CHECK:      {{^\.__linx_vblock_body\.[0-9]+:}}
; CHECK:      v.rdadd vt#1, ->a0
; CHECK:      C.BSTOP

attributes #0 = { "linx-vblock-body-asm"="  v.rdadd vt#1.sw, ->a0\0A  C.BSTOP\0A" }
