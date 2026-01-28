// mlir/include/mlir/Dialect/PTO/IR/PTOInterfaces.td

#ifndef MLIR_DIALECT_PTO_IR_PTOINTERFACES
#define MLIR_DIALECT_PTO_IR_PTOINTERFACES

include "mlir/IR/OpBase.td"

// 定义 OpPipeInterface
def OpPipeInterface : OpInterface<"OpPipeInterface"> {
  let description = [{
    Interface for operations to expose their execution pipeline (Pipe) on Ascend architecture.
    This is used by the InjectSync pass to insert synchronization barriers between
    different hardware units (e.g., MTE2 -> Vector).
  }];

  let cppNamespace = "::mlir::pto";

  let methods = [
    InterfaceMethod<
      "Get the hardware pipe where this operation executes.",
      "::mlir::pto::PIPE",  // 返回类型对应 PTOAttrs.td 中的定义
      "getPipe"
    >
  ];
}

#endif // MLIR_DIALECT_PTO_IR_PTOINTERFACES
