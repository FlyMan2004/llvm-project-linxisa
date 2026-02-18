// Verify Linx defaults use cross-correct linker/runtime policies.
//
// RUN: %clangxx -### --target=linx64-linx-none-elf %s 2>&1 | FileCheck %s --check-prefix=BAREMETAL
// RUN: %clangxx -### --target=linx64-unknown-linux-musl %s 2>&1 | FileCheck %s --check-prefix=LINUX-MUSL
// RUN: %clangxx -### --target=linx64-unknown-linux-gnu %s 2>&1 | FileCheck %s --check-prefix=LINUX-GNU

// BAREMETAL-NOT: "/usr/bin/g++"
// BAREMETAL: "{{.*}}ld.lld"
// BAREMETAL: "-lc++"
// BAREMETAL-NOT: "-lstdc++"

// LINUX-MUSL-NOT: "/usr/bin/ld"
// LINUX-MUSL: "{{.*}}ld.lld"
// LINUX-MUSL: "-lc++"
// LINUX-MUSL: "-lunwind"
// LINUX-MUSL-NOT: "-lstdc++"
// LINUX-MUSL-NOT: "-lgcc"

// LINUX-GNU-NOT: "/usr/bin/ld"
// LINUX-GNU: "{{.*}}ld.lld"
// LINUX-GNU: "-lc++"
// LINUX-GNU: "-lunwind"
// LINUX-GNU-NOT: "-lstdc++"
// LINUX-GNU-NOT: "-lgcc"

int main() { return 0; }
