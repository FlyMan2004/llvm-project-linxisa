from mlir.ir import (
    Context, Location, InsertionPoint,
    Attribute, IndexType, IntegerType, F16Type, F32Type, StringAttr
)
from mlir.dialects import func, arith, scf, pto, builtin
from mlir.dialects.arith import CmpIPredicate


def parse_attr(ctx, s: str, what: str):
    try:
        return Attribute.parse(s, ctx)
    except Exception as e:
        raise RuntimeError(f"Failed to parse {what} attr from: {s}\nError: {e}")


def _idx_const(ctx, v: int):
    return arith.ConstantOp(IndexType.get(ctx), v).result


def build(
    M=32, K=256, N=32,
    validM=32, validK=256, validN=32,
    BASEK=32,
    # 下面两个 fractal size 你按工程真实 TileConfig 改一下：
    s_fractal_ab=512,
    s_fractal_c=1024,
):
    assert K % BASEK == 0
    iters = K // BASEK

    with Context() as ctx:
        pto.register_dialect(ctx, load=True)

        with Location.unknown(ctx):
            module = builtin.ModuleOp()
            module.attributes["pto.device-spec"] = StringAttr.get("Ascend910B1", ctx)

            # ---- element types ----
            t_out = F32Type.get(ctx)
            t_a = F32Type.get(ctx)
            t_b = F32Type.get(ctx)
            t_bias = F32Type.get(ctx)

            # ---- ptr types ----
            ptr_out = pto.PtrType.get(t_out, ctx)
            ptr_a = pto.PtrType.get(t_a, ctx)
            ptr_b = pto.PtrType.get(t_b, ctx)
            ptr_bias = pto.PtrType.get(t_bias, ctx)

            i1 = IntegerType.get_signless(1, ctx)

            # ---- tensor view types ----
            tv2_a = pto.TensorViewType.get(2, t_a, ctx)        # [validM, validK]
            tv2_b = pto.TensorViewType.get(2, t_b, ctx)        # [validK, validN]
            tv2_out = pto.TensorViewType.get(2, t_out, ctx)    # [validM, validN]
            tv2_bias = pto.TensorViewType.get(2, t_bias, ctx)  # [1, validN]

            # ---- tile view types ----
            tile_view_a = pto.TileViewType.get([M, BASEK], t_a, ctx)
            tile_view_b = pto.TileViewType.get([BASEK, N], t_b, ctx)
            tile_view_out = pto.TileViewType.get([M, N], t_out, ctx)
            tile_view_bias = pto.TileViewType.get([1, N], t_bias, ctx)

            # ---- address spaces ----
            mat = pto.AddressSpaceAttr.get(pto.AddressSpace.MAT, ctx)
            left = pto.AddressSpaceAttr.get(pto.AddressSpace.LEFT, ctx)
            right = pto.AddressSpaceAttr.get(pto.AddressSpace.RIGHT, ctx)
            acc = pto.AddressSpaceAttr.get(pto.AddressSpace.ACC, ctx)
            bias = pto.AddressSpaceAttr.get(pto.AddressSpace.BIAS, ctx)

            # ---- configs (3rd arg = s_fractal_size) ----
            # 说明：下面 layout/pad 都是“合理默认”，你按 C++ Tile 定义微调即可
            # MAT tile：搬运用，常见 row_major
            cfg_mat = pto.TileBufConfigAttr.get(
                pto.BLayoutAttr.get(pto.BLayout.ColMajor, ctx),
                pto.SLayoutAttr.get(pto.SLayout.RowMajor, ctx),
                s_fractal_ab,  # 这里也可以单独给 MAT 一个 size
                pto.PadValueAttr.get(pto.PadValue.Null, ctx),
                ctx,
            )

            cfg_mat_bias = pto.TileBufConfigAttr.get(
                pto.BLayoutAttr.get(pto.BLayout.RowMajor, ctx),
                pto.SLayoutAttr.get(pto.SLayout.NoneBox, ctx),
                s_fractal_ab,  # 这里也可以单独给 MAT 一个 size
                pto.PadValueAttr.get(pto.PadValue.Null, ctx),
                ctx,
            )


            # LEFT tile：TileLeft ... BLayout RowMajor, SLayout RowMajor, fractalAB
            cfg_left = pto.TileBufConfigAttr.get(
                pto.BLayoutAttr.get(pto.BLayout.RowMajor, ctx),
                pto.SLayoutAttr.get(pto.SLayout.RowMajor, ctx),
                s_fractal_ab,
                pto.PadValueAttr.get(pto.PadValue.Null, ctx),
                ctx,
            )

            # RIGHT tile：TileRight ... BLayout RowMajor, SLayout ColMajor, fractalAB
            cfg_right = pto.TileBufConfigAttr.get(
                pto.BLayoutAttr.get(pto.BLayout.RowMajor, ctx),
                pto.SLayoutAttr.get(pto.SLayout.ColMajor, ctx),
                s_fractal_ab,
                pto.PadValueAttr.get(pto.PadValue.Null, ctx),
                ctx,
            )

            # ACC tile：TileAcc ... BLayout ColMajor, SLayout RowMajor, fractalC
            cfg_acc = pto.TileBufConfigAttr.get(
                pto.BLayoutAttr.get(pto.BLayout.ColMajor, ctx),
                pto.SLayoutAttr.get(pto.SLayout.RowMajor, ctx),
                s_fractal_c,
                pto.PadValueAttr.get(pto.PadValue.Null, ctx),
                ctx,
            )

            # BIAS tile：一般不需要分形（这里给 0；你也可给一个专用 size）
            cfg_bias = pto.TileBufConfigAttr.get(
                pto.BLayoutAttr.get(pto.BLayout.RowMajor, ctx),
                pto.SLayoutAttr.get(pto.SLayout.NoneBox, ctx),
                512,
                pto.PadValueAttr.get(pto.PadValue.Null, ctx),
                ctx,
            )

            # ---- tile buf types (each has its own cfg) ----
            tile_buf_aMat = pto.TileBufType.get([M, BASEK], t_a, mat, [M, BASEK], cfg_mat, ctx)
            tile_buf_bMat = pto.TileBufType.get([BASEK, N], t_b, mat, [BASEK, N], cfg_mat, ctx)
            tile_buf_biasData = pto.TileBufType.get([1, N], t_bias, mat, [1, N], cfg_mat_bias, ctx)

            tile_buf_aTile = pto.TileBufType.get([M, BASEK], t_a, left, [M, BASEK], cfg_left, ctx)
            tile_buf_bTile = pto.TileBufType.get([BASEK, N], t_b, right, [BASEK, N], cfg_right, ctx)
            tile_buf_cTile = pto.TileBufType.get([M, N], t_out, acc, [M,N], cfg_acc, ctx)
            tile_buf_biasTile = pto.TileBufType.get([1, N], t_bias, bias, [1, N], cfg_bias, ctx)

            # ---- enum attrs ----
            EVENT_ID0 = parse_attr(ctx, "#pto.event<EVENT_ID0>", "EVENT_ID0")
            PIPE_MTE2 = parse_attr(ctx, "#pto.pipe<PIPE_MTE2>", "PIPE_MTE2")
            PIPE_MTE1 = parse_attr(ctx, "#pto.pipe<PIPE_MTE1>", "PIPE_MTE1")
            PIPE_M = parse_attr(ctx, "#pto.pipe<PIPE_M>", "PIPE_M")
            PIPE_FIX = parse_attr(ctx, "#pto.pipe<PIPE_FIX>", "PIPE_FIX")

            # ---- function ----
            # (out, A, B, bias, isBias)
            fn_ty = func.FunctionType.get([ptr_out, ptr_a, ptr_b, ptr_bias, i1], [])
            with InsertionPoint(module.body):
                fn = func.FuncOp("RunTMATMULSplitK", fn_ty)
                entry = fn.add_entry_block()

            with InsertionPoint(entry):
                out_ptr, a_ptr, b_ptr, bias_ptr, isBias = entry.arguments

                # ---- constants ----
                c0 = _idx_const(ctx, 0)
                c1 = _idx_const(ctx, 1)
                cOne = _idx_const(ctx, 1)

                cM = _idx_const(ctx, validM)
                cK = _idx_const(ctx, validK)
                cN = _idx_const(ctx, validN)

                cBASEK = _idx_const(ctx, BASEK)
                cIter = _idx_const(ctx, iters)

                cTileM = _idx_const(ctx, M)
                cTileN = _idx_const(ctx, N)

                # ---- make_tensor_view ----
                # A: [validM, validK], stride [validK, 1]
                tvA = pto.MakeTensorViewOp(tv2_a, a_ptr, [cM, cK], [cK, c1]).result
                # B: [validK, validN], stride [validN, 1]
                tvB = pto.MakeTensorViewOp(tv2_b, b_ptr, [cK, cN], [cN, c1]).result
                # OUT: [validM, validN], stride [validN, 1]
                tvOut = pto.MakeTensorViewOp(tv2_out, out_ptr, [cM, cN], [cN, c1]).result
                # BIAS: [1, validN], stride [validN, 1]
                tvBias = pto.MakeTensorViewOp(tv2_bias, bias_ptr, [cOne, cN], [cN, c1]).result

                # ---- alloc tiles ----
                aMatTile = pto.AllocTileOp(tile_buf_aMat).result
                bMatTile = pto.AllocTileOp(tile_buf_bMat).result
                biasDataTile = pto.AllocTileOp(tile_buf_biasData).result

                aTile = pto.AllocTileOp(tile_buf_aTile).result
                bTile = pto.AllocTileOp(tile_buf_bTile).result
                cTile = pto.AllocTileOp(tile_buf_cTile).result
                biasTile = pto.AllocTileOp(tile_buf_biasTile).result

                # ---- valid dims (passed into ops; alloc has no valid operands) ----
                # 对齐你 C++ TileLeft/Right/Acc/Bias 的 RowValid_/ColValid_

                # ---- loop for split-K ----
                loop = scf.ForOp(c0, cIter, c1, [])
                with InsertionPoint(loop.body):
                    i = loop.induction_variable

                    # kOff = i * BASEK
                    kOff = arith.MulIOp(i, cBASEK).result

                    # subviews for this split-K
                    svA = pto.SubviewOp(tile_view_a, tvA, [c0, kOff], [cTileM, cBASEK]).result
                    svB = pto.SubviewOp(tile_view_b, tvB, [kOff, c0], [cBASEK, cTileN]).result
                    svBias = pto.SubviewOp(tile_view_bias, tvBias, [c0, c0], [cOne, cTileN]).result

                    # ---- TLOAD ----
                    # 注意：TLOAD 的 valid dims 一般对应目标 tile 的有效区域（a/b/bias）
                    pto.TLoadOp(None, svA, aMatTile)
                    pto.TLoadOp(None, svB, bMatTile)

                    if_load_bias = scf.IfOp(isBias, [], hasElse=True)
                    with InsertionPoint(if_load_bias.then_block):
                        pto.TLoadOp(None, svBias, biasDataTile)
                        scf.YieldOp([])
                    with InsertionPoint(if_load_bias.else_block):
                        scf.YieldOp([])

                    # ---- sync: MTE2 -> MTE1 ----
                    pto.SetFlagOp(PIPE_MTE2, PIPE_MTE1, EVENT_ID0)
                    pto.WaitFlagOp(PIPE_MTE2, PIPE_MTE1, EVENT_ID0)

                    # ---- TMOV ----
                    # TMOV 也传对应 tile 的 valid dims（a/b/bias）
                    pto.TMovOp(None, aMatTile, aTile)
                    pto.TMovOp(None, bMatTile, bTile)

                    if_mov_bias = scf.IfOp(isBias, [], hasElse=True)
                    with InsertionPoint(if_mov_bias.then_block):
                        pto.TMovOp(None, biasDataTile, biasTile)
                        scf.YieldOp([])
                    with InsertionPoint(if_mov_bias.else_block):
                        scf.YieldOp([])

                    # ---- sync: MTE1 -> M ----
                    pto.SetFlagOp(PIPE_MTE1, PIPE_M, EVENT_ID0)
                    pto.WaitFlagOp(PIPE_MTE1, PIPE_M, EVENT_ID0)

                    # ---- i == 0 ? (bias? TMATMUL_BIAS : TMATMUL) : TMATMUL_ACC ----
                    is_i0 = arith.CmpIOp(CmpIPredicate.eq, i, c0).result
                    if_i0 = scf.IfOp(is_i0, [], hasElse=True)

                    # then: i == 0
                    with InsertionPoint(if_i0.then_block):
                        if_bias0 = scf.IfOp(isBias, [], hasElse=True)
                        with InsertionPoint(if_bias0.then_block):
                            # L0C 清空 + bias
                            # 约定：valid_dims_c 用于 C 的有效区域
                            pto.TMatmulBiasOp(None,aTile, bTile, biasTile, cTile)
                            scf.YieldOp([])
                        with InsertionPoint(if_bias0.else_block):
                            # L0C 清空
                            pto.TMatmulOp(None, aTile, bTile, cTile)
                            scf.YieldOp([])
                        scf.YieldOp([])

                    # else: i != 0
                    with InsertionPoint(if_i0.else_block):
                        # L0C 不清空 accumulate
                        pto.TMatmulAccOp(None, cTile, aTile, bTile, cTile)
                        scf.YieldOp([])

                    # ---- sync: M -> MTE2 ----
                    pto.SetFlagOp(PIPE_M, PIPE_MTE2, EVENT_ID0)
                    pto.WaitFlagOp(PIPE_M, PIPE_MTE2, EVENT_ID0)

                    scf.YieldOp([])

                # ---- after loop ----
                pto.SetFlagOp(PIPE_M, PIPE_FIX, EVENT_ID0)
                pto.WaitFlagOp(PIPE_M, PIPE_FIX, EVENT_ID0)

                # ---- TSTORE ----
                # 写回 OUT，传 C 的 valid dims
                svOut = pto.SubviewOp(tile_view_out, tvOut, [c0, c0], [cTileM, cTileN]).result
                pto.TStoreOp(None, cTile, svOut)

                func.ReturnOp([])
            module.operation.verify()
            return module


if __name__ == "__main__":
    m = build()
    print(m)
