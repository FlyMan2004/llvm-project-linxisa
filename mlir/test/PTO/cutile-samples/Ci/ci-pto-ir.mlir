module {
  func.func @vec_ci_kernel_2d(%arg0: !pto.ptr<i32>, %arg1: i32) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c32 = arith.constant 32 : index
    %c0_0 = arith.constant 0 : index
    %c32_1 = arith.constant 32 : index
    %0 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xi32>
    %1 = pto.alloc_tile : <loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    pto.tci ins(%arg1 : i32) outs(%1 : !pto.tile_buf<loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) {descending = true}
    %2 = pto.subview %0, offsets = [%c0_0, %c0_0], sizes = [%c32_1, %c32_1] : !pto.tensor_view<2xi32> -> !pto.tile_view<32x32xi32>
    pto.tstore ins(%1 : !pto.tile_buf<loc=ub, dtype=i32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%2 : !pto.tile_view<32x32xi32>)
    return
  }
}
