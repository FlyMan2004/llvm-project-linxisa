#include "common/pto_instr.hpp"
using namespace pto;
template <typename T, int M, int N>
AICORE inline void Run_test_textract_dps_2d(__gm__ T* v1, __gm__ T* v2, unsigned v3, unsigned v4, unsigned v5, unsigned v6) {
  unsigned v7 = 1024;
  unsigned v8 = 0;
  int64_t v9 = 0;
  unsigned v10 = 32;
  unsigned v11 = v3 * v10;
  unsigned v12 = v4 * v10;
  unsigned v13 = v11 * v7;
  unsigned v14 = v13 + v12;
  __gm__ T* v15 = v1 + v14;
  using GTShape_187651624050224 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651624050224 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651624050224 = GlobalTensor<T, GTShape_187651624050224, GTStride_187651624050224>;
  GT_187651624050224 v16 = GT_187651624050224(v15);
  unsigned v17 = v11 * v7;
  unsigned v18 = v17 + v12;
  __gm__ T* v19 = v2 + v18;
  using GTShape_187651624259264 = pto::Shape<1, 1, 1, 16, 16>;
  using GTStride_187651624259264 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651624259264 = GlobalTensor<T, GTShape_187651624259264, GTStride_187651624259264>;
  GT_187651624259264 v20 = GT_187651624259264(v19);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v21;
  TASSIGN(v21, v9);
  Tile<TileType::Vec, T, 16, 16, -1, -1> v22;
  TASSIGN(v22, v9);
  TLOAD(v21, v16);
  set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  TEXTRACT(v22, v21, v5, v6);
  set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  TSTORE(v20, v22);
  return;
}

extern "C" [aicore] void test_textract_dps_2d(__gm__ float* v1, __gm__ float* v2, unsigned v3, unsigned v4, unsigned v5, unsigned v6) {
  Run_test_textract_dps_2d<float, 32, 16>(v1, v2, v3, v4, v5, v6);
  return;
}
