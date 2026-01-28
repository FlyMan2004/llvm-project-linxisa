#include "common/pto_instr.hpp"
using namespace pto;
template <typename T, int M, int N>
AICORE inline void Run_vec_and_kernel_2d(__gm__ T* v1, __gm__ T* v2, __gm__ T* v3, unsigned v4, unsigned v5) {
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
  using GTShape_187651200201328 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200201328 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200201328 = GlobalTensor<T, GTShape_187651200201328, GTStride_187651200201328>;
  GT_187651200201328 v16 = GT_187651200201328(v15);
  unsigned v17 = v11 * v6;
  unsigned v18 = v17 + v12;
  __gm__ T* v19 = v2 + v18;
  using GTShape_187651200413840 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200413840 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200413840 = GlobalTensor<T, GTShape_187651200413840, GTStride_187651200413840>;
  GT_187651200413840 v20 = GT_187651200413840(v19);
  unsigned v21 = v11 * v6;
  unsigned v22 = v21 + v12;
  __gm__ T* v23 = v3 + v22;
  using GTShape_187651200312112 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200312112 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200312112 = GlobalTensor<T, GTShape_187651200312112, GTStride_187651200312112>;
  GT_187651200312112 v24 = GT_187651200312112(v23);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v25;
  TASSIGN(v25, v9);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v26;
  TASSIGN(v26, v8);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v27;
  TASSIGN(v27, v9);
  TLOAD(v25, v16);
  TLOAD(v26, v20);
  set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  TAND(v27, v25, v26);
  set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  TSTORE(v24, v27);
  return;
}

extern "C" [aicore] void vec_and_kernel_2d(__gm__ float* v1, __gm__ float* v2, __gm__ float* v3, unsigned v4, unsigned v5) {
  Run_vec_and_kernel_2d<float, 32, 32>(v1, v2, v3, v4, v5);
  return;
}

template <typename T, int M, int N>
AICORE inline void Run_vec_abs_kernel_2d(__gm__ T* v1, __gm__ T* v2, unsigned v3, unsigned v4) {
  unsigned v5 = 1024;
  unsigned v6 = 0;
  int64_t v7 = 4096;
  int64_t v8 = 0;
  unsigned v9 = 32;
  unsigned v10 = v3 * v9;
  unsigned v11 = v4 * v9;
  unsigned v12 = v10 * v5;
  unsigned v13 = v12 + v11;
  __gm__ T* v14 = v1 + v13;
  using GTShape_187651200354256 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200354256 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200354256 = GlobalTensor<T, GTShape_187651200354256, GTStride_187651200354256>;
  GT_187651200354256 v15 = GT_187651200354256(v14);
  unsigned v16 = v10 * v5;
  unsigned v17 = v16 + v11;
  __gm__ T* v18 = v2 + v17;
  using GTShape_187651200291920 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200291920 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200291920 = GlobalTensor<T, GTShape_187651200291920, GTStride_187651200291920>;
  GT_187651200291920 v19 = GT_187651200291920(v18);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v20;
  TASSIGN(v20, v8);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v21;
  TASSIGN(v21, v7);
  TLOAD(v20, v15);
  set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  TABS(v21, v20);
  set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  TSTORE(v19, v21);
  return;
}

extern "C" [aicore] void vec_abs_kernel_2d(__gm__ float* v1, __gm__ float* v2, unsigned v3, unsigned v4) {
  Run_vec_abs_kernel_2d<float, 32, 32>(v1, v2, v3, v4);
  return;
}

template <typename T, int M, int N>
AICORE inline void Run_vec_add_kernel_2d(__gm__ T* v1, __gm__ T* v2, __gm__ T* v3, unsigned v4, unsigned v5) {
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
  using GTShape_187651200268112 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200268112 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200268112 = GlobalTensor<T, GTShape_187651200268112, GTStride_187651200268112>;
  GT_187651200268112 v16 = GT_187651200268112(v15);
  unsigned v17 = v11 * v6;
  unsigned v18 = v17 + v12;
  __gm__ T* v19 = v2 + v18;
  using GTShape_187651200186080 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200186080 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200186080 = GlobalTensor<T, GTShape_187651200186080, GTStride_187651200186080>;
  GT_187651200186080 v20 = GT_187651200186080(v19);
  unsigned v21 = v11 * v6;
  unsigned v22 = v21 + v12;
  __gm__ T* v23 = v3 + v22;
  using GTShape_187651200721056 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200721056 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200721056 = GlobalTensor<T, GTShape_187651200721056, GTStride_187651200721056>;
  GT_187651200721056 v24 = GT_187651200721056(v23);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v25;
  TASSIGN(v25, v9);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v26;
  TASSIGN(v26, v8);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v27;
  TASSIGN(v27, v9);
  TLOAD(v25, v16);
  TLOAD(v26, v20);
  set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  TADD(v27, v25, v26);
  set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  TSTORE(v24, v27);
  return;
}

extern "C" [aicore] void vec_add_kernel_2d(__gm__ float* v1, __gm__ float* v2, __gm__ float* v3, unsigned v4, unsigned v5) {
  Run_vec_add_kernel_2d<float, 32, 32>(v1, v2, v3, v4, v5);
  return;
}

template <typename T, int M, int N>
AICORE inline void Run_vec_addc_kernel_2d(__gm__ T* v1, __gm__ T* v2, __gm__ T* v3, __gm__ T* v4, unsigned v5, unsigned v6) {
  unsigned v7 = 1024;
  unsigned v8 = 0;
  int64_t v9 = 4096;
  int64_t v10 = 8192;
  int64_t v11 = 0;
  unsigned v12 = 32;
  unsigned v13 = v5 * v12;
  unsigned v14 = v6 * v12;
  unsigned v15 = v13 * v7;
  unsigned v16 = v15 + v14;
  __gm__ T* v17 = v1 + v16;
  using GTShape_187651200726528 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200726528 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200726528 = GlobalTensor<T, GTShape_187651200726528, GTStride_187651200726528>;
  GT_187651200726528 v18 = GT_187651200726528(v17);
  unsigned v19 = v13 * v7;
  unsigned v20 = v19 + v14;
  __gm__ T* v21 = v2 + v20;
  using GTShape_187651200726768 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200726768 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200726768 = GlobalTensor<T, GTShape_187651200726768, GTStride_187651200726768>;
  GT_187651200726768 v22 = GT_187651200726768(v21);
  unsigned v23 = v13 * v7;
  unsigned v24 = v23 + v14;
  __gm__ T* v25 = v3 + v24;
  using GTShape_187651200727424 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200727424 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200727424 = GlobalTensor<T, GTShape_187651200727424, GTStride_187651200727424>;
  GT_187651200727424 v26 = GT_187651200727424(v25);
  unsigned v27 = v13 * v7;
  unsigned v28 = v27 + v14;
  __gm__ T* v29 = v4 + v28;
  using GTShape_187651200727664 = pto::Shape<1, 1, 1, 32, 32>;
  using GTStride_187651200727664 = pto::Stride<1, 1, 1, 1024, 1>;
  using GT_187651200727664 = GlobalTensor<T, GTShape_187651200727664, GTStride_187651200727664>;
  GT_187651200727664 v30 = GT_187651200727664(v29);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v31;
  TASSIGN(v31, v11);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v32;
  TASSIGN(v32, v9);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v33;
  TASSIGN(v33, v10);
  Tile<TileType::Vec, T, 32, 32, -1, -1> v34;
  TASSIGN(v34, v11);
  TLOAD(v31, v18);
  TLOAD(v32, v22);
  TLOAD(v33, v26);
  set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
  TADDC(v34, v31, v32, v33);
  set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
  TSTORE(v30, v34);
  return;
}

extern "C" [aicore] void vec_addc_kernel_2d(__gm__ float* v1, __gm__ float* v2, __gm__ float* v3, __gm__ float* v4, unsigned v5, unsigned v6) {
  Run_vec_addc_kernel_2d<float, 32, 32>(v1, v2, v3, v4, v5, v6);
  return;
}
