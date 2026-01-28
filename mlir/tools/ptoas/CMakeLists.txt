set(LLVM_LINK_COMPONENTS
  Support
  Core
)

add_mlir_tool(ptoas
  ptoas.cpp

  DEPENDS
  MLIRPTODialect
  MLIRPTOEnumsIncGen
  MLIRPTOAttributesIncGen
  MLIRPTOTransforms
  MLIRPTOIncGen
)

target_link_libraries(ptoas PRIVATE
  MLIRPTODialect
  MLIRPTOTransforms
  MLIRRegisterAllDialects
  MLIRBufferizationTransforms
  MLIRArithTransforms
  MLIRTensorTransforms
  MLIRFuncTransforms
  MLIRIR
  MLIRParser
  MLIRPass
  MLIREmitCDialect
  MLIRSCFDialect
  MLIRGPUDialect
  MLIRTransforms
  MLIRTargetCpp
  MLIRAffineDialect
  MLIRArithDialect
  MLIRTensorDialect
  MLIRControlFlowDialect
  MLIRBufferizationDialect
  MLIRFuncDialect
  MLIRLLVMDialect
  MLIRSupport
  MLIRTransformUtils
  MLIRTransforms
)
