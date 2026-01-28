module {
  // out = matmul(A, B) + C   (tile-wise 32x32)
  func.func @cube_matmul_vadd_2d(
      %arg_a: memref<?xf32, #pto.address_space<gm>>,
      %arg_b: memref<?xf32, #pto.address_space<gm>>,
      %arg_c: memref<?xf32, #pto.address_space<gm>>,
      %arg_out: memref<?xf32, #pto.address_space<gm>>,
      %M: index,     // total rows
      %N: index,     // total cols
      %lda: index,   // leading dim for A
      %ldb: index,   // leading dim for B
      %ldc: index,   // leading dim for C/OUT
      %tile_idx_x: index,
      %tile_idx_y: index
  ) {
    %c1  = arith.constant 1  : index
    %c32 = arith.constant 32 : index

    // 1) reinterpret cast: 1D GM -> 2D GM descriptor (no alloc, no copy)
    %A2D = memref.reinterpret_cast %arg_a
      to offset: [0], sizes: [%M, %N], strides: [%lda, %c1]
      : memref<?xf32, #pto.address_space<gm>>
        to memref<?x?xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>

    %B2D = memref.reinterpret_cast %arg_b
      to offset: [0], sizes: [%M, %N], strides: [%ldb, %c1]
      : memref<?xf32, #pto.address_space<gm>>
        to memref<?x?xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>

    %C2D = memref.reinterpret_cast %arg_c
      to offset: [0], sizes: [%M, %N], strides: [%ldc, %c1]
      : memref<?xf32, #pto.address_space<gm>>
        to memref<?x?xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>

    %O2D = memref.reinterpret_cast %arg_out
      to offset: [0], sizes: [%M, %N], strides: [%ldc, %c1]
      : memref<?xf32, #pto.address_space<gm>>
        to memref<?x?xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>

    // 2) tile offset
    %off_m = arith.muli %tile_idx_y, %c32 : index
    %off_n = arith.muli %tile_idx_x, %c32 : index

    // 3) GM subviews (32x32 tile)
    %a_gm = memref.subview %A2D[%off_m, %off_n] [32, 32] [1, 1]
      : memref<?x?xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>
        to memref<32x32xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>

    %b_gm = memref.subview %B2D[%off_m, %off_n] [32, 32] [1, 1]
      : memref<?x?xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>
        to memref<32x32xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>

    %c_gm = memref.subview %C2D[%off_m, %off_n] [32, 32] [1, 1]
      : memref<?x?xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>
        to memref<32x32xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>

    %o_gm = memref.subview %O2D[%off_m, %off_n] [32, 32] [1, 1]
      : memref<?x?xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>
        to memref<32x32xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>

    // 4) DMA load (GM -> CBUF)
    %a_cbuf = memref.alloc() {alignment = 64 : i64} : memref<32x32xf32, #pto.address_space<cbuf>>
    pto.load_dps ins(%a_gm : memref<32x32xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>)
                 outs(%a_cbuf : memref<32x32xf32, #pto.address_space<cbuf>>)
                 init_out_buffer = false

    %b_cbuf = memref.alloc() {alignment = 64 : i64} : memref<32x32xf32, #pto.address_space<cbuf>>
    pto.load_dps ins(%b_gm : memref<32x32xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>)
                 outs(%b_cbuf : memref<32x32xf32, #pto.address_space<cbuf>>)
                 init_out_buffer = false

    %c_cbuf = memref.alloc() {alignment = 64 : i64} : memref<32x32xf32, #pto.address_space<cbuf>>
    pto.load_dps ins(%c_gm : memref<32x32xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>)
                 outs(%c_cbuf : memref<32x32xf32, #pto.address_space<cbuf>>)
                 init_out_buffer = false

    // 5) Matmul (CBUF x CBUF -> CC)
    %mm_cc = memref.alloc() {alignment = 64 : i64} : memref<32x32xf32, #pto.address_space<cc>>
    pto.matmul_dps ins(%a_cbuf, %b_cbuf
                       : memref<32x32xf32, #pto.address_space<cbuf>>,
                         memref<32x32xf32, #pto.address_space<cbuf>>)
                   outs(%mm_cc : memref<32x32xf32, #pto.address_space<cc>>)

    // 6) Bring matmul result to CBUF for vadd (CC -> CBUF)
    %mm_cbuf = memref.alloc() {alignment = 64 : i64} : memref<32x32xf32, #pto.address_space<cbuf>>
    pto.mov_dps ins(%mm_cc : memref<32x32xf32, #pto.address_space<cc>>)
           outs(%mm_cbuf : memref<32x32xf32, #pto.address_space<cbuf>>)

    // 7) VADD (CBUF + CBUF -> CBUF)
    %out_cbuf = memref.alloc() {alignment = 64 : i64} : memref<32x32xf32, #pto.address_space<cbuf>>
    pto.addf_dps ins(%c_cbuf, %mm_cbuf
                     : memref<32x32xf32, #pto.address_space<cbuf>>,
                       memref<32x32xf32, #pto.address_space<cbuf>>)
                 outs(%out_cbuf : memref<32x32xf32, #pto.address_space<cbuf>>)

    // 8) Store back (CBUF -> GM subview)
    pto.store_dps ins(%out_cbuf : memref<32x32xf32, #pto.address_space<cbuf>>)
                  outs(%o_gm : memref<32x32xf32, strided<[?, 1], offset: ?>, #pto.address_space<gm>>)

    return
  }
}
