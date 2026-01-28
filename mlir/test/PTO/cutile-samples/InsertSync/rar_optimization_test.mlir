module attributes {"pto.device-spec" = "Ascend910B1"} {
  func.func @rar_hazard_check(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>) {
    %c0 = arith.constant 0 : index
    %c32 = arith.constant 32 : index
    %c1 = arith.constant 1 : index

    // 两个不同的 GM 源
    %view0 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %view1 = pto.make_tensor_view %arg1, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>

    // 两个 UB Buffer
    %buf0 = pto.alloc_tile : <32x32xf32, memory_space = #pto.address_space<ub>>
    %buf1 = pto.alloc_tile : <32x32xf32, memory_space = #pto.address_space<ub>>

    // 两个 Load 操作的 Subview
    %tile0 = pto.subview %view0, offsets=[%c0, %c0], sizes=[32,32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    %tile1 = pto.subview %view1, offsets=[%c0, %c0], sizes=[32,32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>

    // [Test Target] 连续两个 GM Load
    // 它们都在 MTE2 流水线上，都读取 GM。
    // 这是一个 RAR (Read-After-Read) 场景。

    // Load 1
    pto.tload ins(%tile0 : <32x32xf32>) outs(%buf0 : <32x32xf32, memory_space = #pto.address_space<ub>>)

    // Load 2
    // 预期：这里不应该有 Barrier！
    pto.tload ins(%tile1 : <32x32xf32>) outs(%buf1 : <32x32xf32, memory_space = #pto.address_space<ub>>)

    return
  }
}
