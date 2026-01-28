//valid shape均为常量的情况
//module {
//  func.func @vec_add_kernel_2d(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>, %arg2: !pto.ptr<f32>) {
//    %c0 = arith.constant 0 : index
//    %c1 = arith.constant 1 : index
//    %c32 = arith.constant 32 : index
//    %0 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
//    %1 = pto.make_tensor_view %arg1, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
//    %2 = pto.make_tensor_view %arg2, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
//    %3 = pto.subview %0, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
//    %4 = pto.subview %1, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
//    // 两个维度的参数都是常量，直接解析valid shape的静态值，定义tile的shape信息，设置v_row和v_col的值
//    %5 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=1, v_col=1, blayout=row_major, slayout=none_box, fractal=512, pad=0>
//    %6 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
//    %7 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
//    pto.tload ins(%3 : !pto.tile_view<32x32xf32>) outs(%5 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=1, v_col=1, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
//    pto.tload ins(%4 : !pto.tile_view<32x32xf32>) outs(%6 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
//    pto.tadd ins(%5, %6 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=1, v_col=1, blayout=row_major, slayout=none_box, fractal=512, pad=0>, !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%7 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
//    %8 = pto.subview %2, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
//    pto.tstore ins(%7 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%8 : !pto.tile_view<32x32xf32>)
//    return
//  }
//}
//最后emitC的时候将动态类型的Tile表示为Tile<TileType::Vec, float, 32, 32, BLayout::RowMajor, 1, 1, SLayout::NoneBox, 512, PadValue::Null> v27; 因为我们已经知道valid row和valid col的静态值为1。tile类型声明也是一样处理

// valid shape都是变量的情况
//module {
//  func.func @vec_add_kernel_2d(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>, %arg2: !pto.ptr<f32>, %arg3: i32, %arg4: i32) {
//    %c0 = arith.constant 0 : index
//    %c1 = arith.constant 1 : index
//    %c32 = arith.constant 32 : index
//    %v_row_idx = arith.index_cast %arg3 : i32 to index
//    %v_col_idx = arith.index_cast %arg4 : i32 to index
//    %0 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
//    %1 = pto.make_tensor_view %arg1, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
//    %2 = pto.make_tensor_view %arg2, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
//    %3 = pto.subview %0, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
//    %4 = pto.subview %1, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
//    // 两个维度的参数都是变量，tile类型当中v_row和v_col使用？表示动态
//    %5 = pto.alloc_tile valid_row = %v_row_idx valid_col = %v_col_idx : <loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>
//    %6 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
//    %7 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
//    pto.tload ins(%3 : !pto.tile_view<32x32xf32>) outs(%5 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
//    pto.tload ins(%4 : !pto.tile_view<32x32xf32>) outs(%6 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
//    pto.tadd ins(%5, %6 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=?, blayout=row_major, slayout=none_box, fractal=512, pad=0>, !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%7 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
//    %8 = pto.subview %2, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
//    pto.tstore ins(%7 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%8 : !pto.tile_view<32x32xf32>)
//    return
//  }
//}

//最后emitC的时候将动态类型的Tile表示为Tile<TileType::Vec, float, 32, 32, BLayout::RowMajor, -1, -1, SLayout::NoneBox, 512, PadValue::Null> v27(valid_row,valid_col); 其中两个-1表示valid row和valid col是动态值，需要后面的两个传参（valid_row， valid_col）。tile类型声明也是一样处理

module {
  func.func @vec_add_kernel_2d(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>, %arg2: !pto.ptr<f32>, %arg3: i32) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c32 = arith.constant 32 : index
    %v_row_idx = arith.index_cast %arg3 : i32 to index
    %0 = pto.make_tensor_view %arg0, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %1 = pto.make_tensor_view %arg1, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %2 = pto.make_tensor_view %arg2, shape = [%c32, %c32] strides = [%c32, %c1] : !pto.tensor_view<2xf32>
    %3 = pto.subview %0, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    %4 = pto.subview %1, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    // Definition: v_row=?, v_col=32
    %5 = pto.alloc_tile valid_row = %v_row_idx : <loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    %6 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    %7 = pto.alloc_tile : <loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>
    pto.tload ins(%3 : !pto.tile_view<32x32xf32>) outs(%5 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    pto.tload ins(%4 : !pto.tile_view<32x32xf32>) outs(%6 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    pto.tadd ins(%5, %6 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=?, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>, !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%7 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>)
    %8 = pto.subview %2, offsets = [%c0, %c0], sizes = [32, 32] : !pto.tensor_view<2xf32> -> !pto.tile_view<32x32xf32>
    pto.tstore ins(%7 : !pto.tile_buf<loc=ub, dtype=f32, rows=32, cols=32, v_row=32, v_col=32, blayout=row_major, slayout=none_box, fractal=512, pad=0>) outs(%8 : !pto.tile_view<32x32xf32>)
    return
  }
}
