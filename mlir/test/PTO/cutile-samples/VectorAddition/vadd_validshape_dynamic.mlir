module {
  func.func @vec_add_kernel_2d_dynamic(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>, %arg2: !pto.ptr<f32>, %arg3: i32, %arg4: i32) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c32 = arith.constant 32 : index
    %0 = arith.index_cast %arg3 : i32 to index
    %1 = arith.index_cast %arg4 : i32 to index
    %2 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %3 = pto.make_tensor_view %arg1, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %4 = pto.make_tensor_view %arg2, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %5 = pto.subview %2, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    %6 = pto.subview %3, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    %7 = pto.alloc_tile valid_row = %0 valid_col = %1 : <loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    %8 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    %9 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    pto.tload ins(%5 : !pto.tile_view<32x32xf32>) outs(%7 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    pto.tload ins(%6 : !pto.tile_view<32x32xf32>) outs(%8 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    pto.tadd ins(%7, %8 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>, !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%9 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    %10 = pto.subview %4, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    pto.tstore ins(%9 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%10 : !pto.tile_view<32x32xf32>)
    return
  }
}
