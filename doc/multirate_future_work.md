# Multi-Rate Processing: Future Work & Architectural Roadmap

This document outlines future enhancements, optimization passes, and runtime extensions for multi-rate signal processing in `stridejit` that build upon the core multi-rate compilation architecture documented in [multirate_compilation.md](file:///c:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/doc/multirate_compilation.md).

---

## 1. Configurable Stream Coalescing Modes (`RateCoalescingMode`)

### Background & Motivation
Currently, `stridegen` and `stridejit` execute topological rate staging by default (`streamStages`), coalescing uniform same-rate streams under unified decimation branches (`DownsampleExprAST`) and execution loops (`UpsampleExprAST`).

While optimal for runtime execution, fine-grained control over coalescing is valuable for:
- **Interactive Debugging / Line Stepping**: Stepping through stream evaluation in exact source order without cross-stream batching.
- **Compilation Speed vs. Optimization Tradeoffs**: Ultra-fast single-pass compilation for live code reloading vs. fully optimized dependency DAG scheduling.

### Proposed Architecture

```cpp
namespace strd {

enum class RateCoalescingMode {
    Topological,  // Default: Groups independent same-rate streams via dependency DAG
    Adjacent,     // Merges strictly contiguous same-rate streams without reordering
    None          // 1:1 on-the-fly wrapping (exact textual order, no coalescing)
};

} // namespace strd
```

### Planned Extensions
1. **Option Exposure**: Add `RateCoalescingMode rateCoalescingMode` to `StrideCompilerOptions`.
2. **Bypass Path**: In `StrideGenerator::generateCodeForTree`, inspect `options.rateCoalescingMode`. When set to `None`, bypass the `streamStages` loop and emit stream segments in textual declaration order using per-stream wrappers.
3. **Adjacent Merging**: Implement a fast scan that batches only strictly consecutive streams sharing an identical rate without constructing a dependency DAG.

---

## 2. DSP Sample Rate Filtering & Interpolation

### Background & Motivation
In digital signal processing (DSP), naïve decimation produces frequency aliasing (mirroring high-frequency content into the lower passband), and naïve repeating/sample-and-hold upsampling produces imaging artifacts (high-frequency spectral replicas).

Currently, downsampling decimates values directly (`c == 0`), and upsampling evaluates the stream block $N$ times per domain tick.

### Planned Extensions

#### A. Anti-Aliasing Decimation Filters
- **Standard Library Filter Blocks**: Provide half-band, Butterworth, and finite impulse response (FIR) filter modules in the standard library.
- **Automatic Filter Insertion Hook**:
  - Compiler optimization pass to optionally inject a low-pass decimation filter prior to downsampling boundaries where high-frequency attenuation is requested.
  - Phase-accumulator synchronized filter evaluation.

#### B. Polynomial & Bandlimited Upsampling Interpolation
- **Interpolation Methods**:
  - Linear interpolation ($y[n] = (1 - \alpha) y_0 + \alpha y_1$).
  - Cubic Hermite / Catmull-Rom splines.
  - Sinc / polyphase filter banks for bandlimited reconstruction.
- **State Integration**: Store historical sample values in caller-allocated `__state` to support multi-tap reconstruction across domain process calls.

---

## 3. Inter-Domain Scheduling & Multi-Rate Synchronization

### Background & Motivation
Presently, multi-rate compilation handles rate transitions *within* a single domain (`_domainDefinition`). However, complex systems (such as audio engines paired with game simulation loops or UI event dispatchers) often feature multiple domains running on separate hardware clocks or OS threads:
- **Audio Domain**: 44.1 kHz / 48 kHz (real-time audio thread, strict deadline).
- **Control / Physics Domain**: 100 Hz – 1 kHz (fixed-step physics thread).
- **UI / Render Domain**: 60 Hz – 144 Hz (frame-paced render loop).

### Planned Extensions

```
┌─────────────────────────┐               ┌─────────────────────────┐
│     AudioDom (48 kHz)   │               │   PhysicsDom (120 Hz)   │
│                         │               │                         │
│  [Audio Stream Engine]  │               │ [Physics Simulation]    │
└────────────┬────────────┘               └────────────┬────────────┘
             │                                         │
             ▼                                         ▲
      ┌──────────────────────────────────────────────────────┐
      │ Lock-Free SPSC Ring Buffer (Cross-Domain Bridge)     │
      └──────────────────────────────────────────────────────┘
```

1. **Cross-Domain Streams**: Semantic syntax and compiler support for streaming signals between domains (e.g. `PhysicsSignal >> AudioModulator`).
2. **Lock-Free SPSC Buffering**: Generate single-producer single-consumer (SPSC) circular queues or atomic mailboxes in C/C++ runtime bindings.
3. **Domain Clock Orchestration**: Runtime coordinator managing thread affinity, execution barriers, and tick phase alignment between distinct domains.

---

## 4. Hardware Acceleration & Whole-Program Optimization Engine

### Background & Motivation
The stream scheduling architecture in `stridejit` is intentionally decoupled from hardware-specific backends. As detailed in the planning roadmap:

```
┌────────────────────────────────────────────────────────┐
│         General Compiler Optimization Engine           │
│                    (Future / Broad)                    │
│                                                        │
│  - Target Device Dispatch (CPU / GPU / DSP / FPGA)     │
│  - Hardware Accelerated Kernel Offloading              │
│  - SIMD Vectorization & Memory Layout Optimization     │
│  - Whole-Program Domain Scheduling                     │
└───────────────────────────┬────────────────────────────┘
                            │
                            │ Configures: target options, scheduling mode
                            ▼
┌────────────────────────────────────────────────────────┐
│           Lightweight Stream Scheduler                 │
│                (StrideJIT Backend)                     │
└────────────────────────────────────────────────────────┘
```

### Planned Extensions
1. **SIMD Vectorization of Upsampled Batches**:
   - For uniform upsampled stream batches (e.g. $N = 4$ or $N = 8$), unroll and vectorize operations using LLVM vector types (`<4 x float>`, `<8 x float>`) or AVX2/NEON intrinsics.
2. **Kernel Offloading**:
   - Identify heavy parallel or decoupled multi-rate stages suitable for offloading to compute shaders or OpenCL/Vulkan compute pipelines.
3. **Memory Layout Coalescing**:
   - Reorder state struct fields and dynamic array buffers to ensure cache-line alignment and eliminate false sharing during multi-threaded domain execution.
