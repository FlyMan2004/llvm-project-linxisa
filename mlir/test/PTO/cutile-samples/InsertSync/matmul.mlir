module attributes {"pto.device-spec" = "Ascend910B1"} {
  func.func @RunTMATMULSplitK(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>, %arg2: !pto.ptr<f32>, %arg3: !pto.ptr<f32>, %arg4: i1) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c1_0 = arith.constant 1 : index
    %c32 = arith.constant 32 : index
    %c256 = arith.constant 256 : index
    %c32_1 = arith.constant 32 : index
    %c32_2 = arith.constant 32 : index
    %c8 = arith.constant 8 : index
    %0 = pto.make_tensor_view %arg1, shape = [%c32, %c256] strides = [%c256, %c1] : !pto.tensor_view<2xf32>
    %1 = pto.make_tensor_view %arg2, shape = [%c256, %c32_1] strides = [%c32_1, %c1] : !pto.tensor_view<2xf32>
    %2 = pto.make_tensor_view %arg0, shape = [%c32, %c32_1] strides = [%c32_1, %c1] : !pto.tensor_view<2xf32>
    %3 = pto.make_tensor_view %arg3, shape = [%c1_0, %c32_1] strides = [%c32_1, %c1] : !pto.tensor_view<2xf32>
    %4 = pto.alloc_tile : <32x32xf32, memory_space = #pto.address_space<mat>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>
    %5 = pto.alloc_tile : <32x32xf32, memory_space = #pto.address_space<mat>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>
    %6 = pto.alloc_tile : <1x32xf32, memory_space = #pto.address_space<mat>>
    %7 = pto.alloc_tile : <32x32xf32, memory_space = #pto.address_space<left>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>
    %8 = pto.alloc_tile : <32x32xf32, memory_space = #pto.address_space<right>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=2 : i32, s_fractal_size=512, pad=0 : i32>>
    %9 = pto.alloc_tile : <32x32xf32, memory_space = #pto.address_space<acc>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=1024, pad=0 : i32>>
    %10 = pto.alloc_tile : <1x32xf32, memory_space = #pto.address_space<bias>>
    scf.for %arg5 = %c0 to %c8 step %c1 {
      %12 = arith.muli %arg5, %c32_2 : index
      %13 = pto.subview %0, offsets = [%c0, %12], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
      %14 = pto.subview %1, offsets = [%12, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
      %15 = pto.subview %3, offsets = [%c0, %c0], sizes = [1, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<1x32xf32>
      pto.tload ins(%13 : <32x32xf32>) outs(%4 : <32x32xf32, memory_space = #pto.address_space<mat>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>)
      pto.tload ins(%14 : <32x32xf32>) outs(%5 : <32x32xf32, memory_space = #pto.address_space<mat>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>)
      scf.if %arg4 {
        pto.tload ins(%15 : <1x32xf32>) outs(%6 : <1x32xf32, memory_space = #pto.address_space<mat>>)
      } else {
      }
      //pto.set_flag[<PIPE_MTE2>, <PIPE_MTE1>, <EVENT_ID0>]
      //pto.wait_flag[<PIPE_MTE2>, <PIPE_MTE1>, <EVENT_ID0>]
      pto.tmov ins(%4 : <32x32xf32, memory_space = #pto.address_space<mat>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>) outs(%7 : <32x32xf32, memory_space = #pto.address_space<left>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>)
      pto.tmov ins(%5 : <32x32xf32, memory_space = #pto.address_space<mat>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>) outs(%8 : <32x32xf32, memory_space = #pto.address_space<right>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=2 : i32, s_fractal_size=512, pad=0 : i32>>)
      scf.if %arg4 {
        pto.tmov ins(%6 : <1x32xf32, memory_space = #pto.address_space<mat>>) outs(%10 : <1x32xf32, memory_space = #pto.address_space<bias>>)
      } else {
      }
      //pto.set_flag[<PIPE_MTE1>, <PIPE_M>, <EVENT_ID0>]
      //pto.wait_flag[<PIPE_MTE1>, <PIPE_M>, <EVENT_ID0>]
      %16 = arith.cmpi eq, %arg5, %c0 : index
      scf.if %16 {
        scf.if %arg4 {
          pto.tmatmul.bias ins(%7, %8, %10 : <32x32xf32, memory_space = #pto.address_space<left>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>, <32x32xf32, memory_space = #pto.address_space<right>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=2 : i32, s_fractal_size=512, pad=0 : i32>>, <1x32xf32, memory_space = #pto.address_space<bias>>) outs(%9 : <32x32xf32, memory_space = #pto.address_space<acc>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=1024, pad=0 : i32>>)
        } else {
          pto.tmatmul ins(%7, %8 : <32x32xf32, memory_space = #pto.address_space<left>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>, <32x32xf32, memory_space = #pto.address_space<right>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=2 : i32, s_fractal_size=512, pad=0 : i32>>) outs(%9 : <32x32xf32, memory_space = #pto.address_space<acc>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=1024, pad=0 : i32>>)
        }
      } else {
        pto.tmatmul.acc ins(%9, %7, %8 : <32x32xf32, memory_space = #pto.address_space<acc>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=1024, pad=0 : i32>>, <32x32xf32, memory_space = #pto.address_space<left>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=1 : i32, s_fractal_size=512, pad=0 : i32>>, <32x32xf32, memory_space = #pto.address_space<right>, config = #pto.tile_buf_config<blayout=0 : i32, slayout=2 : i32, s_fractal_size=512, pad=0 : i32>>) outs(%9 : <32x32xf32, memory_space = #pto.address_space<acc>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=1024, pad=0 : i32>>)
      }
      //pto.set_flag[<PIPE_M>, <PIPE_MTE2>, <EVENT_ID0>]
      //pto.wait_flag[<PIPE_M>, <PIPE_MTE2>, <EVENT_ID0>]
    }
    //pto.set_flag[<PIPE_M>, <PIPE_FIX>, <EVENT_ID0>]
    //pto.wait_flag[<PIPE_M>, <PIPE_FIX>, <EVENT_ID0>]
    %11 = pto.subview %2, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    pto.tstore ins(%9 : <32x32xf32, memory_space = #pto.address_space<acc>, config = #pto.tile_buf_config<blayout=1 : i32, slayout=1 : i32, s_fractal_size=1024, pad=0 : i32>>) outs(%11 : <32x32xf32>)
    return
  }
}
