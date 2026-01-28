module {
  func.func @cube_matmul_vadd_2d(
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

    %slice_a_gm = tensor.extract_slice %arg_a[%offset_row, %offset_col] [32, 32] [1, 1]
      : tensor<1024x1024xf32> to tensor<32x32xf32>
    %slice_b_gm = tensor.extract_slice %arg_b[%offset_row, %offset_col] [32, 32] [1, 1]
      : tensor<1024x1024xf32> to tensor<32x32xf32>
    %slice_d_gm = tensor.extract_slice %arg_d[%offset_row, %offset_col] [32, 32] [1, 1]
      : tensor<1024x1024xf32> to tensor<32x32xf32>

    %a_ub_init = tensor.empty() : tensor<32x32xf32>
    %b_ub_init = tensor.empty() : tensor<32x32xf32>
    %d_ub_init = tensor.empty() : tensor<32x32xf32>

    %tile_a_ub = pto.load_dps ins(%slice_a_gm : tensor<32x32xf32>)
                            outs(%a_ub_init : tensor<32x32xf32>)
                            -> tensor<32x32xf32>
    %tile_b_ub = pto.load_dps ins(%slice_b_gm : tensor<32x32xf32>)
                            outs(%b_ub_init : tensor<32x32xf32>)
                            -> tensor<32x32xf32>
    %tile_d_ub = pto.load_dps ins(%slice_d_gm : tensor<32x32xf32>)
                            outs(%d_ub_init : tensor<32x32xf32>)
                            -> tensor<32x32xf32>

    %mm_ub_init = tensor.empty() : tensor<32x32xf32>
    %tile_mm_ub = pto.matmul_dps ins(%tile_a_ub, %tile_b_ub : tensor<32x32xf32>, tensor<32x32xf32>)
                               outs(%mm_ub_init : tensor<32x32xf32>)
                               -> tensor<32x32xf32>

    %mm_gm_init = tensor.empty() : tensor<32x32xf32>
    %tile_mm_gm = pto.store_dps ins(%tile_mm_ub : tensor<32x32xf32>)
                              outs(%mm_gm_init : tensor<32x32xf32>)
                              -> tensor<32x32xf32>

    %out_ub_init = tensor.empty() : tensor<32x32xf32>
    %tile_out_ub = pto.addf ins(%tile_mm_ub, %tile_d_ub : tensor<32x32xf32>, tensor<32x32xf32>)
                         outs(%out_ub_init : tensor<32x32xf32>)
                         -> tensor<32x32xf32>

    %out_gm_init = tensor.empty() : tensor<32x32xf32>
    %tile_out_gm = pto.store_dps ins(%tile_out_ub : tensor<32x32xf32>)
                               outs(%out_gm_init : tensor<32x32xf32>)
                               -> tensor<32x32xf32>

    %result = tensor.insert_slice %tile_out_gm into %arg_out[%offset_row, %offset_col] [32, 32] [1, 1]
      : tensor<32x32xf32> into tensor<1024x1024xf32>

    return %result : tensor<1024x1024xf32>
  }
}
