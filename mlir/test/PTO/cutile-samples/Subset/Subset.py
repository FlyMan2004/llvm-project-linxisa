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

            # GM Memory: 外部全局内存，大矩阵视图
            gm_type = pto.get_gm_type([32, 32], f32, ctx)

            # Workspace (UB): 本地高速缓存
            # 物理大小为 [32, 64]，正好能容纳两个 32x32 的 Tile
            # Layout: [Ping (32x32) | Pong (32x32)]
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

                # 定义偏移量常量 (SSA Value)
                c0 = arith.ConstantOp(idx, 0).result
                c32 = arith.ConstantOp(idx, 32).result

                # ==================================================================
                # Subset: Ping-Pong 切分逻辑详解
                # ==================================================================

                # [Ping Tile] - 用于当前计算
                # 参数解析:
                # 1. workspace : 源操作数 (Source)。必须是父级 TileBuf (这里是 32x64)。
                # 2. [c0, c0]  : 动态偏移量 (Offsets)。格式为 [Row, Col]。
                #                这里表示从 (行0, 列0) 开始切分。
                # 3. sizes     : 切片大小 (Static Sizes)。
                #                切出一个 32x32 的小块。
                # 物理含义: 占据 Workspace 的左半部分 (列 0~31)。
                ping = pto.SubsetOp(workspace, [c0, c0], sizes=[c32, c32])

                # [Pong Tile] - 用于预取下一块数据
                # 参数解析:
                # 1. workspace : 同一个源，实现了内存复用。
                # 2. [c0, c32] : 关键点！列偏移为 32。
                #                这意味着指针从第 32 列开始算起。
                # 3. sizes     : 同样是 32x32。
                # 物理含义: 占据 Workspace 的右半部分 (列 32~63)。
                pong = pto.SubsetOp(workspace, [c0, c32], sizes=[c32, c32])

                # ==================================================================
                # DPS 流水线操作
                # ==================================================================

                # [Task A] Compute: 使用 Ping 进行加法计算
                # 此时 Ping 内的数据是上一轮 Load 好的
                pto.TAddOp(ping, ping, ping)

                # [Task B] Prefetch: 从 GM 加载新数据到 Pong
                # 这是一个并行动作，不会干扰 Ping 的计算
                # 参数: (VoidResult, GM源, Pong目标, ValidDims)
                pto.TLoadOp(None, gm_src, pong, [])

                # [Task C] WriteBack: 将 Ping 的计算结果写回 GM
                pto.TStoreOp(None, ping, gm_dst, [])

                func.ReturnOp([])

            m.operation.verify()
            return m

if __name__ == "__main__":
    print(build_pingpong())
