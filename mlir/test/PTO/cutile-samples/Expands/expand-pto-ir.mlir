module {
  func.func @vec_expand_scalar_kernel_2d(%arg0: !pto.ptr<f32>) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c32 = arith.constant 32 : index
    %cst = arith.constant 3.140000e+00 : f32
    %0 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %1 = pto.alloc_tile : !pto.tile_buf<32x32xf32, memory_space = #pto.address_space<ub>>
    pto.texpands ins(%cst : f32) outs(%1 : !pto.tile_buf<32x32xf32, memory_space = #pto.address_space<ub>>)
    %2 = pto.subview %0, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    pto.tstore ins(%1 : !pto.tile_buf<32x32xf32, memory_space = #pto.address_space<ub>>) outs(%2 : !pto.tile_view<32x32xf32>)
    return
  }
}
