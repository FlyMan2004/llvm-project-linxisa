# RUN: llvm-mc -filetype=obj -triple=linx64-linux-musl %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=OBJ
# RUN: ld.lld -shared --entry=foo -o %t %t.o
# RUN: llvm-readobj -r --dyn-relocations %t | FileCheck %s --check-prefix=LINK
# RUN: llvm-objdump -d %t | FileCheck %s --check-prefix=DIS

  .text
  .globl foo
  .type foo,@function
foo:
  addtpc %tpcrel_hi(_DYNAMIC), ->t
  addi   t#1, %tpcrel_lo(_DYNAMIC), ->t
.Lfunc_end0:
  .size foo, .Lfunc_end0-foo

# OBJ: R_LinxV5_TPCREL_HI20 _DYNAMIC
# OBJ: R_LinxV5_TPCREL_LO12_I _DYNAMIC

# LINK: Relocations [
# LINK-NEXT: ]
# LINK: Dynamic Relocations {
# LINK-NEXT: }

# DIS: <foo>:
# DIS: addtpc
# DIS: addi
