#ifndef MLIR_C_DIALECT_PTO_H
#define MLIR_C_DIALECT_PTO_H

#include "mlir-c/IR.h"

#ifdef __cplusplus
extern "C" {
#endif

// Provides: mlirGetDialectHandle__pto__()
MLIR_DECLARE_CAPI_DIALECT_REGISTRATION(PTO, pto);

// ---- !pto.ptr<elem> ----
bool mlirPTOTypeIsAPtrType(MlirType type);
MlirType mlirPTOPtrTypeGet(MlirContext ctx, MlirType elementType);
MlirType mlirPTOPtrTypeGetElementType(MlirType type);

// ---- #pto.address_space<...> ----
bool mlirPTOAttrIsAAddressSpaceAttr(MlirAttribute attr);

// Create: #pto.address_space<ub/gm/...>
MlirAttribute mlirPTOAddressSpaceAttrGet(MlirContext ctx, int32_t value);

// Read back enum value (0..6)
int32_t mlirPTOAddressSpaceAttrGetValue(MlirAttribute attr);

// ---- !pto.tensor_view<rank x elem> ----
bool mlirPTOTypeIsATensorViewType(MlirType type);
MlirType mlirPTOTensorViewTypeGet(MlirContext ctx, int64_t rank,
                                  MlirType elementType);
int64_t mlirPTOTensorViewTypeGetRank(MlirType type);
MlirType mlirPTOTensorViewTypeGetElementType(MlirType type);

// ---- !pto.tile_view<shape x elem> ----
bool mlirPTOTypeIsATileViewType(MlirType type);
MlirType mlirPTOTileViewTypeGet(MlirContext ctx, intptr_t rank,
                                const int64_t *shape, MlirType elementType);
intptr_t mlirPTOTileViewTypeGetRank(MlirType type);
MlirType mlirPTOTileViewTypeGetElementType(MlirType type);
// 返回内部 shape 数组指针（只读）；numDimsOut 返回维度数
const int64_t *mlirPTOTileViewTypeGetShape(MlirType type, intptr_t *numDimsOut);

// ---- !pto.tile<shape x elem> ----
bool mlirPTOTypeIsATileType(MlirType type);
MlirType mlirPTOTileTypeGet(MlirContext ctx, intptr_t rank,
                            const int64_t *shape, MlirType elementType);
intptr_t mlirPTOTileTypeGetRank(MlirType type);
MlirType mlirPTOTileTypeGetElementType(MlirType type);
const int64_t *mlirPTOTileTypeGetShape(MlirType type, intptr_t *numDimsOut);

// ---- TileBufType ----
MLIR_CAPI_EXPORTED bool mlirPTOTypeIsATileBufType(MlirType type);

MLIR_CAPI_EXPORTED MlirType mlirPTOTileBufTypeGet(
    MlirContext ctx, intptr_t rank, const int64_t *shape,
    MlirType elementType, MlirAttribute memorySpace);

MLIR_CAPI_EXPORTED MlirType mlirPTOTileBufTypeGetWithConfig(
    MlirContext ctx, intptr_t rank, const int64_t *shape,
    MlirType elementType, MlirAttribute memorySpace, MlirAttribute config);
// ---- Enum attrs helpers (i32-backed) ----
MLIR_CAPI_EXPORTED MlirAttribute mlirPTOBLayoutAttrGet(MlirContext ctx, int32_t value);
MLIR_CAPI_EXPORTED MlirAttribute mlirPTOSLayoutAttrGet(MlirContext ctx, int32_t value);
MLIR_CAPI_EXPORTED MlirAttribute mlirPTOPadValueAttrGet(MlirContext ctx, int32_t value);
MLIR_CAPI_EXPORTED MlirAttribute mlirPTORoundModeAttrGet(MlirContext ctx, int32_t value);
// ---- TileBufConfigAttr ----
MLIR_CAPI_EXPORTED bool mlirPTOAttrIsATileBufConfigAttr(MlirAttribute attr);

MLIR_CAPI_EXPORTED MlirAttribute mlirPTOTileBufConfigAttrGetDefault(MlirContext ctx);

MLIR_CAPI_EXPORTED MlirAttribute mlirPTOTileBufConfigAttrGet(
    MlirContext ctx,
    MlirAttribute bLayout, MlirAttribute sLayout,
    MlirAttribute sFractalSize, MlirAttribute pad);
MLIR_CAPI_EXPORTED MlirType mlirPTOTileBufTypeGetWithValidShape(
    MlirContext ctx, intptr_t rank, const int64_t *shape, MlirType elementType,
    MlirAttribute memorySpace, intptr_t validRank, const int64_t *validShape);

MLIR_CAPI_EXPORTED MlirType mlirPTOTileBufTypeGetWithValidShapeAndConfig(
    MlirContext ctx, intptr_t rank, const int64_t *shape, MlirType elementType,
    MlirAttribute memorySpace, intptr_t validRank, const int64_t *validShape,
    MlirAttribute config);

#ifdef __cplusplus
}
#endif

#endif // MLIR_C_DIALECT_PTO_H
