# LinxISA LLVM Compiler

## Scope
`compiler/llvm` is the LinxISA LLVM fork used by this superproject for compiler, linker, and runtime bring-up. It is the canonical source for Linx target codegen, MC/asm, ABI lowering, and toolchain binaries used by AVS and Linux/QEMU integration.

## Upstream
- Repository: `https://github.com/LinxISA/llvm-project`
- Merge-back target branch: `main`

## What This Submodule Owns
- LinxISA LLVM backend (`llvm/lib/Target/Linx*`)
- Linx-specific Clang/LLD integration
- Linx toolchain artifacts used by superproject gates

## Canonical Build and Test Commands
Run from `/Users/zhoubot/linx-isa`.

```bash
cmake -S compiler/llvm/llvm -B compiler/llvm/build-linxisa-clang -G Ninja \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_TARGETS_TO_BUILD=LinxISA
cmake --build compiler/llvm/build-linxisa-clang --target clang lld

cd /Users/zhoubot/linx-isa/avs/compiler/linx-llvm/tests
CLANG=/Users/zhoubot/linx-isa/compiler/llvm/build-linxisa-clang/bin/clang ./run.sh
CLANGXX=/Users/zhoubot/linx-isa/compiler/llvm/build-linxisa-clang/bin/clang++ ./run_cpp.sh
```

## LinxISA Integration Touchpoints
- Primary compiler lane in `tools/regression/run.sh`
- Strict cross-repo gate in `tools/regression/strict_cross_repo.sh`
- ABI/runtime coupling with `lib/musl`, `lib/glibc`, and kernel userspace bring-up

## Related Docs
- `/Users/zhoubot/linx-isa/docs/project/navigation.md`
- `/Users/zhoubot/linx-isa/docs/bringup/`
- `/Users/zhoubot/linx-isa/avs/compiler/linx-llvm/README.md`
