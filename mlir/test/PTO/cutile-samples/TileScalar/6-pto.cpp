#include "common/pto_instr.hpp"
using namespace pto;
template <typename T, int M, int N>
AICORE inline void Run_tilescalar_add_kernel_2d(__gm__ T* v1, __gm__ T* v2, int32_t v3, unsigned v4, unsigned v5) {
  unsigned v6 = 1024;
  unsigned v7 = 0;
  int64_t v8 = 4096;
  int64_t v9 = 0;
  unsigned v10 = 32;
  unsigned v11 = v4 * v10;
  unsigned v12 = v5 * v10;
  unsigned v13 = v11 * v6;
  unsigned v14 = v13 + v12;
  __gm__ T* v15 = v1 + v14;
  using GTShape_187651057073216 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651057073216 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651057073216 = GlobalTensor<T, GTShape_187651057073216, GTStride_187651057073216>;
  GT_187651057073216 v16 = GT_187651057073216(v15);
  unsigned v17 = v11 * v6;
  unsigned v18 = v17 + v12;
  __gm__ T* v19 = v2 + v18;
  using GTShape_187651057286896 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651057286896 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651057286896 = GlobalTensor<T, GTShape_187651057286896, GTStride_187651057286896>;
  GT_187651057286896 v20 = GT_187651057286896(v19);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v21;
  TASSIGN(v21, v9);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v22;
  TASSIGN(v22, v8);
  TLOAD(v21, v16);
  set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  TADDS(v22, v21, v3);
  set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  TSTORE(v20, v22);
  return;
}

extern "C" [aicore] void tilescalar_add_kernel_2d(__gm__ float* v1, __gm__ float* v2, int32_t v3, unsigned v4, unsigned v5) {
  Run_tilescalar_add_kernel_2d<float, 32, 32>(v1, v2, v3, v4, v5);
  return;
}
