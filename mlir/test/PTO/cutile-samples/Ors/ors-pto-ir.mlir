module {
  func.func @ors_kernel_2d(%arg0: !pto.ptr<i32>, %arg1: !pto.ptr<i32>) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c32 = arith.constant 32 : index
    %c88_i32 = arith.constant 88 : i32
    %0 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xi32>
    %1 = pto.make_tensor_view %arg1, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xi32>
    %2 = pto.subview %0, offsets = [%c0, %c0], sizes = [%c32, %c32] : !pto.tensor_view<2xi32> -> !pto.tile_view<32x32xi32>
    %3 = pto.subview %1, offsets = [%c0, %c0], sizes = [%c32, %c32] : !pto.tensor_view<2xi32> -> !pto.tile_view<32x32xi32>
    %4 = pto.alloc_tile : <loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    %5 = pto.alloc_tile : <loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    pto.tload ins(%2 : !pto.tile_view<32x32xi32>) outs(%4 : !pto.tile_buf<loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    pto.tors ins(%4 : !pto.tile_buf<loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>, %c88_i32 : i32) outs(%5 : !pto.tile_buf<loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    pto.tstore ins(%5 : !pto.tile_buf<loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%3 : !pto.tile_view<32x32xi32>)
    return
  }
}
