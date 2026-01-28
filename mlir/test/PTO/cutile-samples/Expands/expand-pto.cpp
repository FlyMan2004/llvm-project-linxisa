#include "common/pto_instr.hpp"
using namespace pto;
__global__ AICORE void vec_expand_scalar_kernel_2d(__gm__ float* v1) {
  unsigned v2 = 0;
  float v3 = 3.1400001f;
  int32_t v4 = 32;
  int32_t v5 = 1;
  int32_t v6 = 0;
  int64_t v7 = 0;
  using T = float;
  Tile<TileType::Vec, float, 32, 32, BLayout::RowMajor, 32, 32, SLayout::NoneBox, 512, PadValue::Null> v8;
  TASSIGN(v8, v7);
  TEXPANDS(v8, v3);
  unsigned v9 = (unsigned) v4;
  unsigned v10 = v6 * v9;
  unsigned v11 = v2 + v10;
  unsigned v12 = (unsigned) v5;
  unsigned v13 = v6 * v12;
  unsigned v14 = v11 + v13;
  __gm__ float* v15 = v1 + v14;
  using GTShape_187652083911648 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187652083911648 = pto::Stride<1, 1, 1, 32, 1>;
  using GT_187652083911648 = GlobalTensor<float, GTShape_187652083911648, GTStride_187652083911648>;
  GT_187652083911648 v16 = GT_187652083911648(v15);
  TSTORE(v16, v8);
  return;
}
