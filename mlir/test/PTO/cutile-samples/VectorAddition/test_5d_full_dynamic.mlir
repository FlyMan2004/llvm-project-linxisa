module {
  // 参数列表：
  // %arg0: 基地址指针
  // %arg_n, %arg_c: 动态偏移 (Offsets) -> 用于指针计算
  // %arg_h, %arg_w: 动态大小 (Sizes)   -> 用于构造函数
  func.func @test_5d_full_dynamic(%arg0: !pto.ptr<f32>,
                                  %arg_n: i32, %arg_c: i32,
                                  %arg_h: i32, %arg_w: i32) {
    // ========================================================================
    // 0. 常量定义
    // ========================================================================
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c16 = arith.constant 16 : index
    %c1024 = arith.constant 1024 : index
    %c1048576 = arith.constant 1048576 : index

    // 类型转换 i32 -> index
    %d_n = arith.index_cast %arg_n : i32 to index
    %d_c = arith.index_cast %arg_c : i32 to index
    %d_h = arith.index_cast %arg_h : i32 to index
    %d_w = arith.index_cast %arg_w : i32 to index

    // ========================================================================
    // 1. 定义 5D 物理视图
    // ========================================================================
    %0 = pto.make_tensor_view %arg0,
         shape = [%c1, %c1, %c16, %c1024, %c1024]
         strides = [%c1048576, %c1048576, %c1048576, %c1024, %c1]
         : !pto.tensor_view<5xf32>

    // ========================================================================
    // 2. 全动态 Subview
    // Offsets: [%d_n, %d_c, 0, 0, 0]  <-- 动态偏移
    // Sizes:   [1, 1, 16, %d_h, %d_w] <-- 动态大小
    // ========================================================================
    %1 = pto.subview %0,
         offsets = [%d_n, %d_c, %c0, %c0, %c0],
         sizes = [%c1, %c1, %c16, %d_h, %d_w]
         : !pto.tensor_view<5xf32> -> !pto.tile_view<1x1x16x?x?xf32>

    // ========================================================================
    // 3. Alloc Tile (Safe Size)
    // 物理 buffer 256x128 (防止 UB 溢出)
    // Valid Shape 跟随动态输入
    // ========================================================================
    %tile = pto.alloc_tile valid_row = %d_h valid_col = %d_w
            : <loc=ub, dtype=f32, rows=256, cols=128, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>

    // ========================================================================
    // 4. TLOAD
    // ========================================================================
    pto.tload ins(%1 : !pto.tile_view<1x1x16x?x?xf32>)
              outs(%tile : !pto.tile_buf<loc=ub, dtype=f32, rows=256, cols=128, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>)

    return
  }
}
