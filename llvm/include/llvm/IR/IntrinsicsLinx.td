//===- IntrinsicsLinx.td - Defines Linx intrinsics --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines Linx-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "linx" in {

  // -------------------------------------------------------------------------
  // Tile block intrinsics (TAU)
  //
  // Linx models hardware tile registers as opaque vectors.
  //
  // Bring-up convention: frontends typically use <1024 x i32> (4 KiB) as the
  // tile payload type, but TableGen does not provide a builtin llvm_v1024i32_ty
  // alias. Use llvm_anyvector_ty so the intrinsic can be called with any vector
  // type and let the backend impose target constraints.
  // The intrinsics are lowered by the Linx backend into tile blocks:
  //   BSTART.TMA / BSTART.CUBE + B.IOT/B.IOTI/B.DIM descriptors.
  // -------------------------------------------------------------------------

  // <anyvector> @llvm.linx.tma.tload(ptr base, i32 immSizeCode)
  // SizeCode convention: log2(bytes). Bring-up uses SizeCode=12 for 4 KiB.
  def int_linx_tma_tload :
      Intrinsic<[llvm_anyvector_ty],
                [llvm_ptr_ty, llvm_i32_ty],
                [IntrReadMem, IntrArgMemOnly,
                 NoCapture<ArgIndex<0>>,
                 ImmArg<ArgIndex<1>>]>;

  // void @llvm.linx.tma.tstore(ptr base, <anyvector> tile, i32 immSizeCode)
  def int_linx_tma_tstore :
      Intrinsic<[],
                [llvm_ptr_ty, llvm_anyvector_ty, llvm_i32_ty],
                [IntrWriteMem, IntrArgMemOnly,
                 NoCapture<ArgIndex<0>>,
                 ImmArg<ArgIndex<2>>]>;

  // <anyvector> @llvm.linx.cube.mamulb(<anyvector> a, <anyvector> b,
  //                                    i32 immM, i32 immN, i32 immK)
  def int_linx_cube_mamulb :
      Intrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                [IntrNoMem, IntrHasSideEffects,
                 ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>]>;
}
