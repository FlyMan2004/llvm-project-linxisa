from mlir.ir import Context, Location, Module, InsertionPoint
from mlir.dialects import func, arith, pto
from mlir.ir import F32Type, IndexType

def build_pingpong():
    with Context() as ctx:
        pto.register_dialect(ctx, load=True)

        with Location.unknown(ctx):
            m = Module.create()
            f32 = F32Type.get(ctx)
            idx = IndexType.get(ctx)

            # ======================================================
            # 1. 极简类型定义 (隐藏了所有复杂性)
            # ======================================================

            # 自动创建带 stride<[?,1]> 和 <gm> 的复杂 MemRef
            # 就像 pto.TensorViewType.get(...) 一样简单
            gm_type = pto.get_gm_type([32, 32], f32, ctx)

            # 自动应用 <ub> 空间和连续 Layout
            # 写法回归到最原始的简单形式
            ws_type = pto.TileBufType.get([32, 64], f32, context=ctx)

            # ======================================================
            # 2. 逻辑主体
            # ======================================================
            fn_ty = func.FunctionType.get([gm_type, gm_type, ws_type], [])

            with InsertionPoint(m.body):
                fn = func.FuncOp("test_double_buffer_step", fn_ty)
                entry = fn.add_entry_block()

            with InsertionPoint(entry):
                gm_src, gm_dst, workspace = entry.arguments
                c0 = arith.ConstantOp(idx, 0).result
                c32 = arith.ConstantOp(idx, 32).result

                # Subset: Ping [0,0], Pong [0,32]
                # 这里不需要指定 Result 类型，C++ 会自动推导
                ping = pto.SubsetOp(workspace, [c0, c0], sizes=[c32, c32]).result
                pong = pto.SubsetOp(workspace, [c0, c32], sizes=[c32, c32]).result

                # DPS: Compute, Prefetch, WriteBack
                pto.TAddOp(ping, ping, ping)
                pto.TLoadOp(None, gm_src, pong, [])
                pto.TStoreOp(None, ping, gm_dst, [])

                func.ReturnOp([])

            m.operation.verify()
            return m

if __name__ == "__main__":
    print(build_pingpong())
