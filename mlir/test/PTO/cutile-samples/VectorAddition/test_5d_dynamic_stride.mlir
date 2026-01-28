module {
  // 新增 %arg_stride 用于模拟动态步长
  func.func @test_5d_dynamic_stride(%arg0: !pto.ptr<f32>,
                                    %arg_h: i32, %arg_w: i32,
                                    %arg_stride: i32) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c16 = arith.constant 16 : index
    %c1024 = arith.constant 1024 : index

    // 转换动态参数
    %d_h = arith.index_cast %arg_h : i32 to index
    %d_w = arith.index_cast %arg_w : i32 to index
    %d_stride = arith.index_cast %arg_stride : i32 to index

    // ========================================================================
    // 1. 定义动态 Stride 的物理视图
    // 注意：strides 列表中使用了 %d_stride
    // ========================================================================
    %0 = pto.make_tensor_view %arg0,
         shape = [%c1, %c1, %c16, %c1024, %c1024]
         strides = [%d_stride, %d_stride, %d_stride, %c1024, %c1]
         : !pto.tensor_view<5xf32>

    // 2. Subview
    %1 = pto.subview %0,
         offsets = [%c0, %c0, %c0, %c0, %c0],
         sizes = [%c1, %c1, %c16, %d_h, %d_w]
         : !pto.tensor_view<5xf32> -> !pto.tile_view<1x1x16x?x?xf32>

    // 3. Alloc & Load (保持不变)
    %tile = pto.alloc_tile valid_row = %d_h valid_col = %d_w
            : <loc=ub, dtype=f32, rows=256, cols=128, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>

    pto.tload ins(%1 : !pto.tile_view<1x1x16x?x?xf32>)
              outs(%tile : !pto.tile_buf<loc=ub, dtype=f32, rows=256, cols=128, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>)

    return
  }
}
