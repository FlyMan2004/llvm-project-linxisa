; RUN: llc -mtriple=linx64-unknown-linux-gnu -relocation-model=pic -O2 < %s | FileCheck %s

define i32 @switch_dense(i32 %x) {
entry:
  switch i32 %x, label %def [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
  ]

c0:
  ret i32 10
c1:
  ret i32 11
c2:
  ret i32 12
c3:
  ret i32 13
c4:
  ret i32 14
c5:
  ret i32 15
c6:
  ret i32 16
c7:
  ret i32 17
def:
  ret i32 -1
}

; CHECK-LABEL: switch_dense:
; CHECK-NOT: .LJTI
