# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=linx64 a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=linx64 b.s -o b.o
# RUN: ld.lld a.o b.o -o nofusion
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 nofusion | FileCheck %s --check-prefix=NOFUSE
# RUN: ld.lld --linx-relax-seq-fusion a.o b.o -o fusion
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 fusion | FileCheck %s --check-prefix=FUSE
# RUN: ld.lld --linx-relax-seq-fusion --no-relax a.o b.o -o norelax
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 norelax | FileCheck %s --check-prefix=NORELAX
# RUN: ld.lld --linx-relax-seq-fusion --emit-relocs a.o b.o -o fusion.reloc
# RUN: llvm-objdump -dr --no-show-raw-insn --triple=linx64 fusion.reloc | FileCheck %s --check-prefix=REL

# NOFUSE-LABEL: <_start>:
# NOFUSE: addtpc
# NOFUSE: addi
# NOFUSE: ldi
# NOFUSE: addtpc
# NOFUSE: addi
# NOFUSE: sdi

# FUSE-LABEL: <_start>:
# FUSE: ld.pcr
# FUSE: sd.pcr

# NORELAX-LABEL: <_start>:
# NORELAX: addtpc
# NORELAX: addi
# NORELAX: ldi
# NORELAX: addtpc
# NORELAX: addi
# NORELAX: sdi

# REL-LABEL: <_start>:
# REL: ld.pcr
# REL: R_LINX_PCR17_LOAD
# REL: sd.pcr
# REL: R_LINX_PCR17_STORE
# REL-NOT: R_LINX_PCREL_HI20
# REL-NOT: R_LINX_LO12

#--- a.s
	.text
	.globl	_start
_start:
	addtpc sym, ->t
	addi t#1, sym, ->t
	ldi [t#1, 0], ->u
	addtpc sym, ->t
	addi t#1, sym, ->t
	sdi u#1, [t#1, 0]
	c.bstop

#--- b.s
	.data
	.globl	sym
sym:
	.quad	0
