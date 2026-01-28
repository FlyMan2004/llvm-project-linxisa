module {
  func.func @test_5d_tload(%arg0: !pto.ptr<f32>) {
    // 定义常量
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c16 = arith.constant 16 : index
    %c256 = arith.constant 256 : index
    %c1024 = arith.constant 1024 : index
    %c1048576 = arith.constant 1048576 : index

    // ========================================================================
    // 1. 定义 5D 物理视图
    // 类型语法: !pto.tensor_view<Rank x Type>
    // ========================================================================
    %0 = pto.make_tensor_view %arg0,
         shape = [%c1, %c1, %c16, %c1024, %c1024]
         strides = [%c1048576, %c1048576, %c1048576, %c1024, %c1]
         : !pto.tensor_view<5xf32>

    // ========================================================================
    // 2. 切出 5D 子视图 (作为 Tile 的数据源)
    // 类型语法: !pto.tile_view<Shape x Type>
    // [修正] 将 partition_tensor_view 改为 tile_view
    // ========================================================================
    %1 = pto.subview %0,
         offsets = [%c0, %c0, %c0, %c0, %c0],
         sizes = [1, 1, 16, 16, 16]
         : !pto.tensor_view<5xf32> -> !pto.tile_view<1x1x16x16x16xf32>

    // ========================================================================
    // 3. 准备目标 Tile Buffer (2D)
    // ========================================================================
    %tile = pto.alloc_tile
            : <loc=ub, dtype=f32, rows=256, cols=16, v_row=256, v_col=16, blayout=row_major, slayout=none_box, fractal=512, pad=0>

    // ========================================================================
    // 4. 执行 TLOAD (5D TileView -> 2D TileBuf)
    // [修正] 输入类型改为 !pto.tile_view<1x1x16x16x16xf32>
    // ========================================================================
    pto.tload ins(%1 : !pto.tile_view<1x1x16x16x16xf32>)
              outs(%tile : !pto.tile_buf<loc=ub, dtype=f32, rows=256, cols=16, v_row=256, v_col=16, blayout=row_major, slayout=none_box, fractal=512, pad=0>)

    return
  }
}
