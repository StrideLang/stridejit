# StrideJIT

StrideJIT is the LLVM-based JIT compiler and code generation engine for the Stride programming language.

## LLVM Compatibility

> **Note**: Current support is for **LLVM v18** and **LLVM v22**.

* **LLVM v18**: Supported via system packages (e.g. `llvm-18-dev` on Ubuntu 24.04).
* **LLVM v22**: Supported via CMake FetchContent (`llvmorg-22.1.5` tag) or pre-built local installations.

## Building

### Using Pre-installed LLVM (Default)

Configure CMake with the path to your LLVM installation:

```bash
cmake -B build -G Ninja \
  -DSTRIDEJIT_BUILD_LLVM=OFF \
  -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build
```

### Building LLVM from Source via FetchContent

To automatically fetch and compile LLVM 22 from source:

```bash
cmake -B build -G Ninja -DSTRIDEJIT_BUILD_LLVM=ON
cmake --build build
```
