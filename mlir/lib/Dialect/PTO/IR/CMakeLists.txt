add_mlir_dialect_library(MLIRPTODialect
  PTO.cpp
  PTOAttrs.cpp
  PTOTypeDefs.cpp
  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/Dialect/PTO

  DEPENDS
  MLIRPTOEnumsIncGen
  MLIRPTOAttributesIncGen
  MLIRPTOTypeDefsIncGen
  MLIRPTOInterfacesIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)
