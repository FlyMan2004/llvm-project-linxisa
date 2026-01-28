module attributes {"pto.device-spec" = "Ascend910B1"} {
  func.func @nested_loop_sync(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c10 = arith.constant 10 : index
    %c32 = arith.constant 32 : index

    // [Fix] 使用 2D 形状 (32x32 = 1024 elements)
    // 这样 strides 和 offsets 都是 2 个元素，避免越界
    %0 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %1 = pto.make_tensor_view %arg1, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>

    // [Fix] Tile 也是 2D (32x32)
    %buf = pto.alloc_tile : <32x32xf32, memory_space = #pto.address_space<ub>>

    // Outer Loop
    scf.for %i = %c0 to %c10 step %c1 {

      // [Fix] Offsets 也是 2D [%c0, %c0]
      %src_view = pto.subview %0, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>

      // Producer: Load GM -> UB (Write %buf)
      // 这里的 Write 必须等待上一轮 Inner Loop 的 Read 完成
      pto.tload ins(%src_view : <32x32xf32>) outs(%buf : <32x32xf32, memory_space = #pto.address_space<ub>>)

      // Inner Loop
      scf.for %j = %c0 to %c10 step %c1 {

        // [Fix] Offsets 也是 2D
        %dst_view = pto.subview %1, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>

        // Consumer: Store UB -> GM (Read %buf)
        pto.tstore ins(%buf : <32x32xf32, memory_space = #pto.address_space<ub>>) outs(%dst_view : <32x32xf32>)
      }
    }
    return
  }
}
