add_llvm_component_library(LLVMLinxISAAsmParser
  LinxISAAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  LinxISADesc
  LinxISAInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  LinxISA
  )

