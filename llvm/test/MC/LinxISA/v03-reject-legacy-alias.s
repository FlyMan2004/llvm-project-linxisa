# RUN: split-file %s %t
# RUN: llvm-mc -triple=linx64 -filetype=obj %t/bstart_par.s -o /dev/null
# RUN: not llvm-mc -triple=linx64 -filetype=obj %t/legacy_l.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LEGACYL

# LEGACYL: error: legacy alias 'L.BSTOP' is not allowed in v0.3

#--- bstart_par.s
	.text
bstart_par:
	BSTART.PAR 33, 4

#--- legacy_l.s
	.text
legacy_l:
	L.BSTOP
