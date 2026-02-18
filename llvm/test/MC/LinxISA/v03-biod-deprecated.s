# RUN: not llvm-mc -triple=linx64 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

	.text
biod_deprecated:
	BSTART.TLOAD INT32
	B.IOD
	C.BSTOP

# CHECK: error: B.IOD is deprecated in strict-v0.3; use B.IOR/B.IOT/B.IOTI
