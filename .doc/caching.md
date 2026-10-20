# StrideJIT Caching Architecture & Implementation

This document provides a comprehensive technical overview of the compilation and execution caching infrastructure implemented across `StrideJit` and `StrideUtils`.

---

## 1. Overview & Motivation

Compiling a Stride program (`.stride`) to executable native machine code involves several sequential stages:

```
[ .stride Source ]
       │
       ▼ (1) Lexing & Parsing (AST::parseFile)
[ Raw AST ]
       │
       ▼ (2) AST Preparation & Scoping (StrideEnvironment::prepareTree)
[ Prepared AST ]
       │
       ▼ (3) Semantic Analysis & Preprocessing (ASTFunctions::preprocess)
[ Resolved AST ] ── (Tier 1 Cache: .scache)
       │
       ▼ (4) LLVM IR Generation (StrideGenerator::generateIr)
[ LLVM Module IR ] ── (Tier 3 Cache: .bc)
       │
       ▼ (5) LLVM Pass Pipeline Optimization (O2 Module Passes)
[ Optimized IR ]
       │
       ▼ (6) Machine Code Generation (LLVM TargetMachine / MC Layer)
[ Relocatable Object Code ] ── (Tier 2 Cache: .obj)
       │
       ▼ (7) In-Memory Dynamic Linking (ORC JIT ExecutionSession)
[ Executable Function Pointers ] (<Domain>_process)
```

For complex signal-processing graphs, game state pipelines, and repetitive test suites, performing full AST parsing, preprocessing, and LLVM code generation on every run introduces noticeable startup latency (typically 5–25 ms per domain).

The Stride caching infrastructure addresses this with **multi-tiered caching**:
- **Tier 1 (AST Cache, `.scache`)**: Skips text parsing, preparation, and semantic preprocessing.
- **Tier 2 (Compiled Machine Object Cache, `.obj`)**: Skips LLVM IR generation and optimization entirely, linking precompiled relocatable machine code directly into the in-memory JIT execution session.
- **Tier 3 (LLVM Bitcode Cache, `.bc`)**: Preserves optimized LLVM bitcode for inspection, debugging, and target re-linking.

---

## 2. Multi-Tiered Cache Architecture

### Tier 1: AST Serialization (`.scache`)
- **Producer / Consumer**: Handled by `CacheManager::saveCachedAST` and `CacheManager::loadCachedAST` in `StrideUtils`.
- **Format**: Compact binary AST format serialized via `ASTSerializer` / `ASTDeserializer`.
- **Payload Tag**: `prepared_JIT`.
- **Benefit**: Eliminates parser allocations and schema validation passes on warm loads.

### Tier 2: Relocatable Object Caching (`.obj`)
- **Producer**: In `StrideEnvironment::compileInMemory` (or `compileObjectToDisk`), on a cache miss, the compiled LLVM module is optimized and emitted via `StrideEnvironment::generateCompiledObject` to a native object `llvm::MemoryBuffer`.
- **Payload Tag**: `jit_obj_<Triple>_<OptLevel>` (e.g., `jit_obj_x86_64-pc-windows-msvc_O2.obj`).
- **Storage**: Binary payload stored in `.strdcache/` via `CacheManager::saveBinaryPayload`.
- **Fast-Path Loader**:
  On a cache hit in `compileInMemory()`:
  1. `CacheManager::instance().loadBinaryPayload(sourcePath, payloadTag)` retrieves the raw bytes.
  2. Wraps the payload in `llvm::MemoryBuffer::getMemBufferCopy`.
  3. Obtains a resource tracker:
     ```cpp
     auto rt = jit->getMainJITDylib().createResourceTracker();
     cantFail(jit->getObjLinkingLayer().add(rt, std::move(memBuffer)));
     ```
  4. Resolves `<Domain>_process` directly from the JIT dynamic library symbol table.
  5. Completely bypasses LLVM IR generation and LLVM optimization passes.

### Tier 3: LLVM Bitcode Caching (`.bc`)
- **Payload Tag**: `jit_obj_<Triple>_<OptLevel>.bc`.
- **Purpose**: Diagnostic inspection, cross-target compilation analysis, and bitcode archiving.

---

## 3. Cache Invalidation & Fingerprinting

To guarantee correctness, the cache key must change whenever any factor influencing the compiled output changes. The cache key incorporates:

### 1. Source File Hash & Dependencies
- SHA-256 content hash of the target `.stride` source file.
- **Transitive Dependency Tracking**: `m_fileDependencies` tracks imported or included `.stride` modules. Any modification to an imported dependency invalidates all parent cache entries.

### 2. Architecture Target & Optimization Level
Cache keys and tags explicitly partition objects by:
- **Target Triple**: e.g. `x86_64-pc-windows-msvc`, `x86_64-unknown-linux-gnu`, `thumbv6m-none-eabi`.
- **Opt Level**: `O0`, `O1`, `O2`, `O3`.

### 3. Compiler Build & Commit Invalidation
When the compiler binary itself changes (e.g., bug fixes to code generation, instruction lowering, or AST preprocessing), existing cached artifacts must be invalidated.

- **Build Hash Embedding**: `CMakeLists.txt` injects the Git commit hash (`GIT_COMMIT_HASH`) as `STRIDE_COMPILER_BUILD_HASH`.
- **Runtime Override**: `CacheManager::setCompilerBuildHashOverride(hash)` and `clearCompilerBuildHashOverride()` allow test suites and runtime environments to dynamically verify invalidation behavior.
- **Automatic Invalidation**: If the stored build hash does not match the active runtime build hash, the cache entry is treated as a miss and regenerated.

---

## 4. Cache Storage & Directory Resolution

The cache directory is resolved dynamically by `CacheManager::resolveCacheDirForSource(sourcePath)`:

1. **Explicit Directory Override**: Configured programmatically via `CacheManager::instance().setCacheDirectory(customPath)`.
2. **Environment Variable Override**: If `STRIDE_CACHE_DIR` is set in the environment, all caches are redirected to that directory.
3. **Local Project Cache (Default)**: A `.strdcache/` folder created adjacent to the source `.stride` file.
   - Both `.strdcache/` and `*.scache` are excluded in `.gitignore` to prevent committing cached binaries.

---

## 5. Test Cache Isolation & Safety

Unit tests must execute deterministically and must not depend on or contaminate persistent workspace caches.

### `DisableCacheTestListener`
Located in [`tests/testcacheguard.hpp`](file:///c:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/tests/testcacheguard.hpp):
- Inherits from GoogleTest's `::testing::EmptyTestEventListener`.
- Hooked in `tests/main.cpp` via `listeners.Append(new strd::test::DisableCacheTestListener())`.
- On every `OnTestStart` and `OnTestEnd`:
  - Enforces `CacheManager::instance().setEnabled(false)`.
  - Clears any custom cache directory overrides.
  - Clears compiler build hash overrides.
  - Resets cache statistics counters (`resetStats()`).

### `ScopedCacheEnable` (RAII)
Tests that explicitly evaluate caching performance or binary object retrieval (such as `BenchmarkASTCacheVsParse` and `CompiledObjectAndBitcodeCaching`) use the RAII helper:

```cpp
TEST(StrideEnvironmentTest, CompiledObjectAndBitcodeCaching) {
  strd::test::ScopedCacheEnable cacheGuard; // Isolated temp dir + caching enabled

  std::filesystem::path tempDir = cacheGuard.tempDir() / "my_test";
  // ... run compiles and verify cache hits ...
} // On destruction: cache cleared, temp dir recursively deleted, caching disabled
```

### State Logging
In [`CacheManager::setEnabled(bool)`](file:///c:/Users/Andres/source/repos/boardgame/libgame/external/strideutils/src/cachemanager.cpp):
Whenever caching transitions between enabled and disabled, an explicit `LOG_INFO` statement is emitted:
```
[CacheManager] Caching ENABLED
[CacheManager] Caching DISABLED
```
This guarantees complete visibility in test runner logs and debug traces.

---

## 6. Performance Benchmarks

Benchmarks comparing cold vs. warm compilation times in [`tests/environment_tests.cpp`](file:///c:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/tests/environment_tests.cpp):

| Phase | Cold Time | Warm Time (Cached) | Speedup Factor |
| :--- | :--- | :--- | :--- |
| **AST Parse & Preprocess (`.scache`)** | ~1.5 – 3.0 ms | ~0.08 – 0.15 ms | **15x – 25x** |
| **End-to-End JIT (`.obj` load)** | ~8.0 – 15.0 ms | ~0.3 – 0.8 ms | **15x – 40x** |

Warm JIT compilation simply deserializes the relocatable object and performs in-memory symbol resolution, making reload operations virtually instantaneous.
