#include "common/pto_instr.hpp"
using namespace pto;
__global__ AICORE void vec_ci_kernel_2d(__gm__ int32_t* v1, int32_t v2) {
  unsigned v3 = 1;
  unsigned v4 = 0;
  int32_t v5 = 32;
  int32_t v6 = 1;
  int64_t v7 = 0;
  using T = float;
  Tile<TileType::Vec, int32_t, 32, 32, BLayout::RowMajor, 32, 32, SLayout::NoneBox, 512, PadValue::Null> v8;
  TASSIGN(v8, v7);
  TCI<Tile<TileType::Vec, int32_t, 32, 32, BLayout::RowMajor, 32, 32, SLayout::NoneBox, 512, PadValue::Null>, int32_t, 1>(v8, v2);
  unsigned v9 = (unsigned) v5;
  unsigned v10 = v4 * v9;
  unsigned v11 = v4 + v10;
  unsigned v12 = (unsigned) v6;
  unsigned v13 = v4 * v12;
  unsigned v14 = v11 + v13;
  __gm__ int32_t* v15 = v1 + v14;
  using GTShape_94918777430832 = pto::Shape<32, 32>;
  using GTStride_94918777430832 = pto::Stride<32, 1>;
  GTShape_94918777430832 v16 = GTShape_94918777430832();
  GTStride_94918777430832 v17 = GTStride_94918777430832();
  using GT_94918777430832 = GlobalTensor<int32_t, GTShape_94918777430832, GTStride_94918777430832>;
  GT_94918777430832 v18 = GT_94918777430832(v15, v16, v17);
  TSTORE(v18, v8);
  return;
}
