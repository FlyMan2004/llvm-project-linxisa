#include "common/pto_instr.hpp"
using namespace pto;
template <typename T, int M, int N>
AICORE inline void Run_tcolexpand_kernel_2d(__gm__ T* v1, __gm__ T* v2, unsigned v3, unsigned v4) {
  unsigned v5 = 1024;
  unsigned v6 = 0;
  int64_t v7 = 0;
  int64_t v8 = 4096;
  unsigned v9 = 32;
  unsigned v10 = v3 * v9;
  unsigned v11 = v4 * v9;
  unsigned v12 = v10 * v5;
  unsigned v13 = v12 + v11;
  __gm__ T* v14 = v1 + v13;
  using GTShape_187651105106992 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651105106992 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651105106992 = GlobalTensor<T, GTShape_187651105106992, GTStride_187651105106992>;
  GT_187651105106992 v15 = GT_187651105106992(v14);
  unsigned v16 = v10 * v5;
  unsigned v17 = v16 + v11;
  __gm__ T* v18 = v2 + v17;
  using GTShape_187651105320656 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651105320656 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651105320656 = GlobalTensor<T, GTShape_187651105320656, GTStride_187651105320656>;
  GT_187651105320656 v19 = GT_187651105320656(v18);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v20;
  TASSIGN(v20, v7);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v21;
  TASSIGN(v21, v8);
  TLOAD(v20, v15);
  set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  TCOLEXPAND();
  set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  TSTORE(v19, v21);
  return;
}

extern "C" [aicore] void tcolexpand_kernel_2d(__gm__ float* v1, __gm__ float* v2, unsigned v3, unsigned v4) {
  Run_tcolexpand_kernel_2d<float, 32, 32>(v1, v2, v3, v4);
  return;
}
