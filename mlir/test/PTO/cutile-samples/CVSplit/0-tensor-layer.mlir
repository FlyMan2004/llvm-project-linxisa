module {
  func.func @cube_matmul_vadd_2d_nodps(
      %arg_a: tensor<1024x1024xf32>,
      %arg_b: tensor<1024x1024xf32>,
      %arg_d: tensor<1024x1024xf32>,
      %arg_out: tensor<1024x1024xf32>,
      %tile_idx_x: index,
      %tile_idx_y: index
  ) -> tensor<1024x1024xf32> {

    %c32 = arith.constant 32 : index
    %offset_row = arith.muli %tile_idx_y, %c32 : index
    %offset_col = arith.muli %tile_idx_x, %c32 : index

    %slice_a = tensor.extract_slice %arg_a[%offset_row, %offset_col] [32, 32] [1, 1]
      : tensor<1024x1024xf32> to tensor<32x32xf32>
    %slice_b = tensor.extract_slice %arg_b[%offset_row, %offset_col] [32, 32] [1, 1]
      : tensor<1024x1024xf32> to tensor<32x32xf32>
    %slice_d = tensor.extract_slice %arg_d[%offset_row, %offset_col] [32, 32] [1, 1]
      : tensor<1024x1024xf32> to tensor<32x32xf32>

    %tile_a = pto.load ins(%slice_a : tensor<32x32xf32>) -> tensor<32x32xf32>
    %tile_b = pto.load ins(%slice_b : tensor<32x32xf32>) -> tensor<32x32xf32>
    %tile_d = pto.load ins(%slice_d : tensor<32x32xf32>) -> tensor<32x32xf32>

    %tile_mm = pto.matmul ins(%tile_a, %tile_b : tensor<32x32xf32>, tensor<32x32xf32>)
               -> tensor<32x32xf32>

    %tile_out = linalg.add ins(%tile_mm, %tile_d : tensor<32x32xf32>, tensor<32x32xf32>)
               outs(%tile_mm : tensor<32x32xf32>) -> tensor<32x32xf32>

    %tile_out_gm = pto.store ins(%tile_out : tensor<32x32xf32>) -> tensor<32x32xf32>

    %result = tensor.insert_slice %tile_out_gm into %arg_out[%offset_row, %offset_col] [32, 32] [1, 1]
      : tensor<32x32xf32> into tensor<1024x1024xf32>

    return %result : tensor<1024x1024xf32>
  }
}
