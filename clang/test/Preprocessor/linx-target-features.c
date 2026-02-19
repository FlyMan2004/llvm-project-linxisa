// RUN: %clang --target=linx64-unknown-linux-gnu -x c -E -dM %s -o - | FileCheck %s --check-prefix=BASE64
// RUN: %clang --target=linx64-unknown-linux-gnu -march=linx64+lnx-v+lnx-m+lnx-sys -x c -E -dM %s -o - | FileCheck %s --check-prefix=EXT64
// RUN: %clang --target=linx32-unknown-linux-gnu -march=linx32+lnx-c+lnx-f+lnx-a -x c -E -dM %s -o - | FileCheck %s --check-prefix=EXT32
//
// BASE64: #define __LINX64__
// BASE64: #define __LINX_EXT_S32__
// BASE64: #define __LINX_EXT_S64__
// BASE64-NOT: #define __LINX_EXT_V__
//
// EXT64-DAG: #define __LINX_EXT_S32__
// EXT64-DAG: #define __LINX_EXT_S64__
// EXT64-DAG: #define __LINX_EXT_SYS__
// EXT64-DAG: #define __LINX_EXT_V__
// EXT64-DAG: #define __LINX_EXT_M__
//
// EXT32: #define __LINX32__
// EXT32-DAG: #define __LINX_EXT_S32__
// EXT32-DAG: #define __LINX_EXT_C__
// EXT32-DAG: #define __LINX_EXT_F__
// EXT32-DAG: #define __LINX_EXT_A__
// EXT32-NOT: #define __LINX_EXT_S64__

int linx_target_features_preprocessor_test;
