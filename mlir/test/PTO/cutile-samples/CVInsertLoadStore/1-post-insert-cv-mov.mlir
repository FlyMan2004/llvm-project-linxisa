module {
  func.func @vec_add_mul_add_dependency(
    %arg_a: !pto.ptr<f32>,
    %arg_b: !pto.ptr<f32>,
    %arg_c: !pto.ptr<f32>,
    %M: index, %N: index, %lda: index, %ldb: index, %ldc: index
  ) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c32 = arith.constant 32 : index

    // 1. 创建 Views
    %a = pto.make_tensor_view %arg_a, shape = [%M,%N] strides = [%lda,%c1] : !pto.tensor_view<2xf32>
    %b = pto.make_tensor_view %arg_b, shape = [%M,%N] strides = [%ldb,%c1] : !pto.tensor_view<2xf32>
    %c = pto.make_tensor_view %arg_c, shape = [%M,%N] strides = [%ldc,%c1] : !pto.tensor_view<2xf32>

    %m0 = arith.muli %c0, %c32 : index
    %n0 = arith.muli %c0, %c32 : index

    // 2. Subviews
    %a_tile_view = pto.subview %a, offsets = [%m0,%n0], sizes = [32,32]
                  : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    %b_tile_view = pto.subview %b, offsets = [%m0,%n0], sizes = [32,32]
                  : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    %c_tile_view = pto.subview %c, offsets = [%m0,%n0], sizes = [32,32]
                  : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>

    // ==========================================
    // 3. Load
    // ==========================================
    // [MTE2]
    %tile_a = pto.load %a_tile_view : !pto.tile_view<32x32xf32> -> !pto.tile<32x32xf32>
    %tile_b = pto.load %b_tile_view : !pto.tile_view<32x32xf32> -> !pto.tile<32x32xf32>

    // ==========================================
    // 4. Operation 1: Add (Vector)
    // ==========================================
    // [Vector Pipeline]
    // dependency: MTE2 -> Vector (Should sync here)
    %tile_sum1 = pto.addf %tile_a, %tile_b
      : (!pto.tile<32x32xf32>, !pto.tile<32x32xf32>) -> !pto.tile<32x32xf32>

    %tile_sum1_cv = pto.mov %tile_sum1 : !pto.tile<32x32xf32> -> !pto.tile<32x32xf32>

    // ==========================================
    // 5. Operation 2: Matmul (Matrix)
    // ==========================================
    // [M Pipeline] (Read via MTE1 logic)
    // dependency: Vector -> M (Should sync here)
    // D = Sum1 * Sum1
    %tile_mm = pto.matmul ins(%tile_sum1_cv, %tile_sum1_cv : !pto.tile<32x32xf32>, !pto.tile<32x32xf32>)
              -> !pto.tile<32x32xf32>

    %tile_mm_cv = pto.mov %tile_mm : !pto.tile<32x32xf32> -> !pto.tile<32x32xf32>

    // ==========================================
    // 6. Operation 3: Add (Vector)
    // ==========================================
    // [Vector Pipeline]
    // dependency: M -> Vector (Should sync here)
    // E = D + Sum1
    %tile_res = pto.addf %tile_mm_cv, %tile_sum1
      : (!pto.tile<32x32xf32>, !pto.tile<32x32xf32>) -> !pto.tile<32x32xf32>

    // ==========================================
    // 7. Store
    // ==========================================
    // [MTE3]
    // dependency: Vector -> MTE3 (Should sync here)
    pto.store %tile_res, %c_tile_view
      : (!pto.tile<32x32xf32>, !pto.tile_view<32x32xf32>) -> ()

    return
  }
}
