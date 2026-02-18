# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=linx64 a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=linx64 b.s -o b.o
# RUN: ld.lld a.o b.o -o relax
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 relax | FileCheck %s --check-prefix=RELAX
# RUN: ld.lld --no-relax a.o b.o -o norelax
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 norelax | FileCheck %s --check-prefix=NORELAX
# RUN: ld.lld --linx-relax-seq-fusion a.o b.o -o seq
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 seq | FileCheck %s --check-prefix=SEQ

# RELAX-LABEL: <_start>:
# RELAX: ld.pcr
# RELAX: sd.pcr

# NORELAX-LABEL: <_start>:
# NORELAX: hl.ld.pcr
# NORELAX: hl.sd.pcr

# SEQ-LABEL: <_start>:
# SEQ: ld.pcr
# SEQ: sd.pcr

#--- a.s
	.text
	.globl	_start
_start:
	hl.ld.pcr [sym], ->u
	hl.sd.pcr u#1, [sym]
	c.bstop

#--- b.s
	.data
	.globl	sym
sym:
	.quad	0x1234
