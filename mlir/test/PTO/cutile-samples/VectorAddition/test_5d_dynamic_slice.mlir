module {
  func.func @test_5d_dynamic_valid_shape(%arg0: !pto.ptr<f32>, %arg_h: i32, %arg_w: i32) {
    // 0. 常量定义 (确保这些都在)
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index     // <--- 需要用到这个
    %c16 = arith.constant 16 : index   // <--- 需要用到这个
    %c256 = arith.constant 256 : index
    %c1024 = arith.constant 1024 : index
    %c1048576 = arith.constant 1048576 : index

    %d_h = arith.index_cast %arg_h : i32 to index
    %d_w = arith.index_cast %arg_w : i32 to index

    // 1. 定义 View
    %0 = pto.make_tensor_view %arg0,
         shape = [%c1, %c1, %c16, %c1024, %c1024]
         strides = [%c1048576, %c1048576, %c1048576, %c1024, %c1]
         : !pto.tensor_view<5xf32>

    // 2. 动态 Subview
    // [修正点] sizes 列表全部使用 %变量
    %1 = pto.subview %0,
         offsets = [%c0, %c0, %c0, %c0, %c0],
         // 将 [1, 1, 16, %d_h, %d_w] 修改为:
         sizes = [%c1, %c1, %c16, %d_h, %d_w]
         : !pto.tensor_view<5xf32> -> !pto.tile_view<1x1x16x?x?xf32>

    // 3. Alloc Tile (保持不变)
    %tile = pto.alloc_tile valid_row = %d_h valid_col = %d_w
            : <loc=ub, dtype=f32, rows=256, cols=128, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>

    // 4. TLOAD (保持不变)
    pto.tload ins(%1 : !pto.tile_view<1x1x16x?x?xf32>)
              outs(%tile : !pto.tile_buf<loc=ub, dtype=f32, rows=256, cols=128, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>)

    return
  }
}
