#include "common/pto_instr.hpp"
using namespace pto;
template <typename T, int M, int N>
AICORE inline void Run_tci_kernel_2d(__gm__ T* v1, unsigned v2, unsigned v3, int32_t v4) {
  unsigned v5 = 1024;
  unsigned v6 = 0;
  int64_t v7 = 0;
  unsigned v8 = 32;
  unsigned v9 = v2 * v8;
  unsigned v10 = v3 * v8;
  unsigned v11 = v9 * v5;
  unsigned v12 = v11 + v10;
  __gm__ T* v13 = v1 + v12;
  using GTShape_187651612416192 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651612416192 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651612416192 = GlobalTensor<T, GTShape_187651612416192, GTStride_187651612416192>;
  GT_187651612416192 v14 = GT_187651612416192(v13);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v15;
  TASSIGN(v15, v7);
  TCI<Tile<TileType::Vec, T, 32, 32, -1, -1>, int32_t, 0>(v15, v4);
  set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  TSTORE(v14, v15);
  return;
}

extern "C" [aicore] void tci_kernel_2d(__gm__ float* v1, unsigned v2, unsigned v3, int32_t v4) {
  Run_tci_kernel_2d<float, 32, 32>(v1, v2, v3, v4);
  return;
}
