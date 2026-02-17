# RUN: not llvm-mc -triple=linx64 %s -o /dev/null 2>&1 | FileCheck %s

	.text
bad_v03_tepl_tileop:
	BSTART.TEPL 1024, FP16

# CHECK: error: BSTART.TEPL TileOp10 must be in range 0..1023
