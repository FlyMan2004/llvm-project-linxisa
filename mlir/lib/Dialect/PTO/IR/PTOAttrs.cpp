//===- PTOAttrs.cpp ------------------------------------------------*- C++ -*-===//
#include "mlir/Dialect/PTO/IR/PTO.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/Parser/Parser.h"          // parseAttribute
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/Support/Casting.h"

using namespace mlir;
using namespace mlir::pto;

TileBufConfigAttr TileBufConfigAttr::getDefault(MLIRContext *ctx) {
  Builder b(ctx);

  // enums attr 在你工程里是 IntegerAttr 子类；用 i32 integer attr 直接表达最稳
  auto i32 = IntegerType::get(ctx, 32);

  // 默认：RowMajor=0, NoneBox=0, Zero=1
  Attribute bl = IntegerAttr::get(i32, /*RowMajor*/ 0);
  Attribute sl = IntegerAttr::get(i32, /*NoneBox*/ 0);
  Attribute pv = IntegerAttr::get(i32, /*Null*/ 0);

  IntegerAttr sz = b.getI32IntegerAttr(512);

  return TileBufConfigAttr::get(ctx, bl, sl, sz, pv);
}

bool TileBufConfigAttr::isDefault() const {
  auto d = getDefault(getContext());
  return getBLayout() == d.getBLayout() &&
         getSLayout() == d.getSLayout() &&
         getSFractalSize() == d.getSFractalSize() &&
         getPad() == d.getPad();
}

LogicalResult TileBufConfigAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                       Attribute bLayout,
                                       Attribute sLayout,
                                       IntegerAttr sFractalSize,
                                       Attribute pad) {
    auto bl = mlir::dyn_cast_or_null<IntegerAttr>(bLayout);
    auto sl = mlir::dyn_cast_or_null<IntegerAttr>(sLayout);
    auto pv = mlir::dyn_cast_or_null<IntegerAttr>(pad);
  if (!bl || !bl.getType().isInteger(32))
    return emitError() << "blayout must be i32 integer attr", failure();
  if (!sl || !sl.getType().isInteger(32))
    return emitError() << "slayout must be i32 integer attr", failure();
  if (!pv || !pv.getType().isInteger(32))
    return emitError() << "pad must be i32 integer attr", failure();

  if (!sFractalSize || !sFractalSize.getType().isInteger(32))
    return emitError() << "s_fractal_size must be i32", failure();

  int32_t s = (int32_t)sFractalSize.getInt();
  if (s != 32 && s != 16 && s != 512 && s != 1024)
    return emitError() << "unsupported s_fractal_size: " << s, failure();

  // 值域检查（按你 enum）
  int32_t blv = (int32_t)bl.getInt();
  if (blv != 0 && blv != 1)
    return emitError() << "unsupported blayout value: " << blv, failure();

  int32_t slv = (int32_t)sl.getInt();
  if (slv < 0 || slv > 2)
    return emitError() << "unsupported slayout value: " << slv, failure();

  int32_t pvv = (int32_t)pv.getInt();
  if (pvv < 0 || pvv > 3)
    return emitError() << "unsupported pad value: " << pvv, failure();

  return success();
}

// ---- TileBufConfigAttr custom asm ----
// 现在的 parse/print 也建议改成直接读写 Attribute（不再依赖 PTO_*_Enum）
Attribute TileBufConfigAttr::parse(AsmParser &p, Type) {
  MLIRContext *ctx = p.getContext();
  auto def = TileBufConfigAttr::getDefault(ctx);

  Attribute bl = def.getBLayout();
  Attribute sl = def.getSLayout();
  IntegerAttr sz = def.getSFractalSize();
  Attribute pv = def.getPad();

  if (p.parseLess()) return {};

  // allow empty: #pto.tile_buf_config<>
  if (succeeded(p.parseOptionalGreater()))
    return TileBufConfigAttr::get(ctx, bl, sl, sz, pv);

  while (true) {
    StringRef key;
    if (p.parseKeyword(&key)) return {};
    if (p.parseEqual()) return {};

    if (key == "blayout") {
      if (p.parseAttribute(bl)) return {};
    } else if (key == "slayout") {
      if (p.parseAttribute(sl)) return {};
    } else if (key == "s_fractal_size") {
      int32_t v;
      if (p.parseInteger(v)) return {};
      sz = IntegerAttr::get(IntegerType::get(ctx, 32), v);
    } else if (key == "pad") {
      if (p.parseAttribute(pv)) return {};
    } else {
      p.emitError(p.getCurrentLocation(), "unknown key in tile_buf_config: ") << key;
      return {};
    }

    if (succeeded(p.parseOptionalGreater()))
      break;
    if (p.parseComma()) return {};
  }

  return TileBufConfigAttr::get(ctx, bl, sl, sz, pv);
}

void TileBufConfigAttr::print(AsmPrinter &p) const {
  p << "<";
  p << "blayout=" << getBLayout();
  p << ", slayout=" << getSLayout();
  p << ", s_fractal_size=" << (int32_t)getSFractalSize().getInt();
  p << ", pad=" << getPad();
  p << ">";
}
