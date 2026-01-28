set(LLVM_TARGET_DEFINITIONS Passes.td)

mlir_tablegen(Passes.h.inc -gen-pass-decls -name PTO)

add_mlir_dialect_library(MLIRPTOTransforms
  InsertLoadStoreForMixCV.cpp
  PTOInsertCVMov.cpp
  PTOConvertToDPS.cpp
  PTOInsertSync.cpp
  PTOViewToMemref.cpp
  PTOConvert.cpp
  Utils.cpp
  OptMemPlanForPipeline.cpp
  AllocToPointerCast.cpp
  InferPTOMemScope.cpp
  PTOPlanMemory.cpp
  PTORemoveRedundantBarrier.cpp
  BufferizableOpInterfaceImpl.cpp
  ConvertToPTOOp.cpp
  PTOHighDimLowering.cpp
  PTOVFloopGather.cpp
  PTOIRTranslator.cpp
  SyncCommon.cpp
  BlockSyncAnalysis.cpp
  MemoryDependentAnalyzer.cpp
  MoveSyncState.cpp
  RemoveRedundantSync.cpp
  SyncEventIdAllocation.cpp
  SyncCodegen.cpp

  ADDITIONAL_HEADER_DIRS
    ${MLIR_MAIN_INCLUDE_DIR}/mlir/Dialect/PTO

  DEPENDS
    MLIRPTOPassesIncGen
    MLIRPTOIncGen
    MLIRPTOEnumsIncGen
    MLIRPTOAttributesIncGen
    MLIRPTOTypeDefsIncGen

  LINK_LIBS PUBLIC
    MLIRIR
    MLIRPass
    MLIRPTODialect
    MLIRFuncDialect
    MLIRMemRefDialect
    MLIRArithDialect
    MLIRAffineDialect
    MLIREmitCDialect
    MLIRTargetCpp
    MLIRTransformUtils
    MLIRTransforms
    MLIRTensorDialect
    MLIRSCFToEmitC
)
