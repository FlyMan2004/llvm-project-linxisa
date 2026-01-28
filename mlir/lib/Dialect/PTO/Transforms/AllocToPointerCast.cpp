//===- AllocToPointerCast.cpp - convert memref.AllocOp to pto.pointercastOp.//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AllocToPointerCast.h"
#include "mlir/Dialect/PTO/Transforms/Passes.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace mlir {
#define GEN_PASS_DEF_ALLOCTOPOINTERCAST
#include "mlir/Dialect/PTO/Transforms/Passes.h.inc"

} // namespace mlir

using namespace mlir;
using namespace mlir::pto;

namespace {} // namespace

LogicalResult MemrefAllocaOpToPointerCastOpPattern::matchAndRewrite(
    memref::AllocOp op, PatternRewriter &rewriter) const {
  const auto &currentMemRefType = cast<BaseMemRefType>(op.getType());

  auto iter = buffer2Offsets.find(op.getResult());
  // assert(iter != buffer2Offsets.end() && "address should be found");

  SmallVector<Value> addrs;
  for (auto &offset : iter->second) {
    auto constantIntOffsetOp =
        rewriter.create<arith::ConstantIntOp>(op->getLoc(), offset, 64);
    addrs.push_back(constantIntOffsetOp);
  }

  // [修改 1] 从 ValueRange 中拆解出 row 和 col
  // memref.alloc 的 getDynamicSizes() 返回的是变长列表。
  // 既然我们只支持 2D Tile，且如果是动态 shape 通常两个维度都是动态的 (?x?)，
  // 我们直接按顺序提取。
  Value vRow, vCol;
  auto dynSizes = op.getDynamicSizes();

  if (dynSizes.size() >= 2) {
      vRow = dynSizes[0];
      vCol = dynSizes[1];
  } else if (dynSizes.size() == 1) {
      // 极其罕见的混合情况 (例如 32x?)，视具体需求处理，这里默认取第一个
      // 或者根据维度索引判断是 row 还是 col，这里暂时从简
      vCol = dynSizes[0];
  }

  // [修改 2] 调用新的 Builder 签名
  // 1. ValueRange(addrs) -> 传递物理地址列表
  // 2. vRow ? vRow : Value() -> 传递 Value 对象（如果为空则传空 Value）
  // 3. TileBufConfigAttr() -> 传递空 Attribute 对象 (不能传 nullptr)

  auto ptoPointerCastOp = rewriter.create<pto::PointerCastOp>(
      op.getLoc(),
      currentMemRefType,
      ValueRange(addrs),      // addrs
      vRow ? vRow : Value(),  // valid_row
      vCol ? vCol : Value(),  // valid_col
      TileBufConfigAttr()     // config (空对象)
  );

  rewriter.replaceOp(op, ptoPointerCastOp->getResults());
  return success();
}

// LogicalResult UpdateWorkSpaceAllocaOpOffsetPattern::matchAndRewrite(
//     bishengir::memref_ext::AllocWorkspaceOp op,
//     PatternRewriter &rewriter) const {
//   if (!op.getOffset().empty()) {
//     return failure();
//   }
//   auto iter = buffer2Offsets.find(op.getResult());
//   assert(iter != buffer2Offsets.end() && "address should be found");

//   SmallVector<Value> argOffset;
//   for (auto &offset : iter->second) {
//     Value newOffset =
//         rewriter.create<arith::ConstantIndexOp>(op->getLoc(), offset)
//             .getResult();
//     argOffset.push_back(newOffset);
//   }
//   auto allocWorkspaceOp =
//       rewriter.create<bishengir::memref_ext::AllocWorkspaceOp>(
//           op.getLoc(), op->getResultTypes(), op.getWorkspaceArg(),
//           op.getDynamicSize(), argOffset);
//   rewriter.replaceOp(op, allocWorkspaceOp->getResults());
//   return success();
// }
