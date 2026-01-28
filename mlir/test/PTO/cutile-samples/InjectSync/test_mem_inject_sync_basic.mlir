// RUN: ./bin/ptoas --pto-insert-sync %s | FileCheck %s

module {
  // Case 1: 基础流水线 MTE2 -> Vector -> MTE3
  func.func @test_basic_pipeline(%arg0: memref<16x16x16xf16, #pto.address_space<gm>>,
                                 %arg1: memref<16x16x16xf16, #pto.address_space<gm>>) {
    %ub = memref.alloc() : memref<16x16x16xf16, #pto.address_space<ub>>

    // CHECK: pto.load_dps
    // CHECK-NEXT: pto.set_flag[<PIPE_MTE2>, <PIPE_V>, <EVENT_ID0>]
    pto.load_dps ins(%arg0 : memref<16x16x16xf16, #pto.address_space<gm>>)
                 outs(%ub : memref<16x16x16xf16, #pto.address_space<ub>>)

    // CHECK: pto.wait_flag[<PIPE_MTE2>, <PIPE_V>, <EVENT_ID0>]
    // CHECK-NEXT: pto.addf_dps
    // CHECK-NEXT: pto.set_flag[<PIPE_V>, <PIPE_MTE3>, <EVENT_ID0>]
    pto.addf_dps ins(%ub, %ub : memref<16x16x16xf16, #pto.address_space<ub>>, memref<16x16x16xf16, #pto.address_space<ub>>)
                 outs(%ub : memref<16x16x16xf16, #pto.address_space<ub>>)

    // CHECK: pto.wait_flag[<PIPE_V>, <PIPE_MTE3>, <EVENT_ID0>]
    // CHECK-NEXT: pto.store_dps
    pto.store_dps ins(%ub : memref<16x16x16xf16, #pto.address_space<ub>>)
                  outs(%arg1 : memref<16x16x16xf16, #pto.address_space<gm>>)

    // CHECK: pto.barrier <PIPE_ALL>
    return
  }
}
