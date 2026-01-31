include_directories( ${CMAKE_CURRENT_BINARY_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/.. )

add_llvm_component_library(LLVMLinxISADesc
  LinxISAAsmBackend.cpp
  LinxISAELFObjectWriter.cpp
  LinxISAInstPrinter.cpp
  LinxISAMCCodeEmitter.cpp
  LinxISAMCAsmInfo.cpp
  LinxISAMCTargetDesc.cpp

  # Spec-driven opcode/field tables (generated in the LinxISA repo, vendored here).
  linxisa_opcodes.c

  LINK_COMPONENTS
  MC
  MCParser
  Support
  LinxISAInfo

  ADD_TO_COMPONENT
  LinxISA
  )

add_dependencies(LLVMLinxISADesc LinxISACommonTableGen)
