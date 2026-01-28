module {
  func.func @vec_add_scalar_kernel_2d(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c32 = arith.constant 32 : index
    %c256 = arith.constant 256 : index
    %0 = pto.make_tensor_view %arg0, shape = [%c1, %c256] strides = [%c256, %c1] : !pto.tensor_view<2xf32>
    %1 = pto.make_tensor_view %arg1, shape = [%c1, %c256] strides = [%c256, %c1] : !pto.tensor_view<2xf32>
    %2 = pto.subview %0, offsets = [%c0, %c0], sizes = [%c1, %c256] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    %3 = pto.alloc_tile : <loc=ub, dtype=f32, rows=1, cols=256, v_row=1, v_col=256, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    %4 = pto.alloc_tile : <loc=ub, dtype=f32, rows=1, cols=256, v_row=1, v_col=256, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    pto.tload ins(%2 : !pto.tile_view<32x32xf32>) outs(%3 : !pto.tile_buf<loc=ub, dtype=f32, rows=1, cols=256, v_row=1, v_col=256, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    pto.tmrgsort ins(%3 : !pto.tile_buf<loc=ub, dtype=f32, rows=1, cols=256, v_row=1, v_col=256, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%4 : !pto.tile_buf<loc=ub, dtype=f32, rows=1, cols=256, v_row=1, v_col=256, blayout=row_major, slayout=none_box, fractal=512, pad=0>) blockLen = 64
    %5 = pto.subview %1, offsets = [%c0, %c0], sizes = [%c32, %c32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    pto.tstore ins(%4 : !pto.tile_buf<loc=ub, dtype=f32, rows=1, cols=256, v_row=1, v_col=256, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%5 : !pto.tile_view<32x32xf32>)
    return
  }
}
