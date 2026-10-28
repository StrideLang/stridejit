# Buffers & Frequency Domain Processing

Status: **prototype, in progress**. This document records the design agreed for `buffer` entities and block-based (frequency domain) processing in Stride, what is in the tree today, known problems, and the work that remains.

---

## 1. Status at a Glance

| Area | State |
| :--- | :--- |
| Design / spec | Agreed, written down in section 2. Open questions 1–3 resolved (section 7) |
| `buffer` declaration → backing storage | Root level only, LLVM globals, sized by `BufferSpec` |
| Validation (sizes, modes, constants, private properties) | Implemented (`BufferSpec`, `validateDataTypes`) |
| Scalar push (`AudioIn >> Buf`) | Implemented |
| Trigger modes (`Full`, `Half`, `Quarter`, `Periodic`) | Implemented with count/hop accounting |
| Trigger block (`trigger: <switch>`, partial flush) | Implemented |
| `Buf.ready >> Reaction(buffer: Buf)` | Implemented, zero-copy ordered frame view |
| `ModeSliding` (compiler-decided mirroring) | Implemented |
| `ModePingPong` | Implemented (frames only; `firstHalf`/`secondHalf` views not yet) |
| `ModeFifo`, `ModeOverlapAdd` | **Not implemented** (rejected with a compile error) |
| `Domain_init` reset | Implemented |
| Buffer → buffer, buffer → sink draining | **Not implemented** |
| Windowing / generic array vectorization | **Not implemented** (design decided, 2.7) |
| FFT / IFFT reactions | **Not implemented** |
| System/platform binding (DMA, sections, alignment) | **Not implemented** |
| State in state structs, buffers inside modules | **Not implemented** (still globals) |
| C header / AOT (`stridec`) support | **Not considered yet** |

> [!NOTE]
> All `JITBufferTest.*` tests pass (section 6). Domain inputs and outputs declared with `domain:` live in the domain state struct, so tests drive them with `setStateVar`/`getStateVar` and `invoke(..., {state})`, not pointer arguments.

---

## 2. Agreed Design

### 2.1 Role of a buffer
A `buffer` is the **conceptual bridge between execution granularities and domains**: scalar sample streams ↔ blocks/frames, fast domains ↔ slow/event-driven ones. It stores data and announces when a block is ready. Two things are deliberately **not** part of the buffer:

* **Math** (windowing, FFT): these are ordinary functions/reactions.
* **Physical placement** (allocation, alignment, DMA, cache policy): this lives in a separate system/platform binding (2.9), so algorithm code stays portable.

### 2.2 Namespacing
Stride packages are declared once per file (`package Name`) and accessed with `::`. Modes and triggers live in one `Buffer` package:

```stride
# stride/buffer.stride
package Buffer

constant ModeSliding    { }
constant ModeFifo       { }
constant ModeOverlapAdd { }
constant ModePingPong   { }

constant TriggerFull     { }
constant TriggerHalf     { }
constant TriggerQuarter  { }
constant TriggerPeriodic { }
```

The compiler accepts `Buffer::<Name>` or a bare `<Name>` from this list for `mode` and `triggerMode`. Anything else is a compile error.

### 2.3 Declaration schema
```stride
buffer AnalysisBuffer {
    type:        Real                 # element type
    capacity:    1024                 # ring length (history kept)
    frameSize:   1024                 # readable window N (default: capacity)
    hop:         256                  # advance step H (TriggerPeriodic)
    mode:        Buffer::ModeSliding
    triggerMode: Buffer::TriggerPeriodic
    trigger:     Flush                # optional trigger block (2.4)
}
```

`capacity`, `frameSize` and `hop` are constant integer expressions (literals, `constant`s, `+ - * /`).

**Sizes.** A buffer has four sizes. Users set the first three, and the compiler picks the fourth:

| Size | Set by | Meaning |
| :--- | :--- | :--- |
| `capacity` | user | Ring length: how much history the buffer keeps |
| `frameSize` (N) | user, default `capacity` (`capacity / 2` for `ModePingPong`) | Window a reaction reads. Public as `Buf.size` |
| hop (H) | `triggerMode` (Full = capacity, Half = capacity/2, Quarter = capacity/4) or `hop` for `TriggerPeriodic` | Samples between two built-in triggers |
| storage | compiler | `capacity`, or `2 × capacity` when mirrored (2.8) |

They must satisfy `capacity ≥ frameSize ≥ hop > 0`. Giving `hop` without a `triggerMode` selects `TriggerPeriodic`. Giving a `hop` that disagrees with a non-periodic trigger mode is an error.

Public, read-only properties (the only ones user code may reference; anything else is a compile error):

| Property | Kind | Meaning |
| :--- | :--- | :--- |
| `Buf.ready` | switch | A frame is ready this tick |
| `Buf.size` | compile-time Int | `frameSize` |
| `Buf.count` | Int | Samples held since the last reset or flush, capped at `capacity` |
| `Buf.firstHalf` / `Buf.secondHalf` | bundle views | Ping-pong halves (DMA HT/TC). **Not implemented yet** |

Everything else (write index, hop counter, pending flush, mirror region, DMA descriptors) is private.

> [!IMPORTANT]
> Goal: keep schemas small, constrained and explicit, with a clear split between public and private properties. Port/entity property computation (like `size` on ports today) currently happens in C++ and can't be seen from the language. In the long run, buffer logic should be expressible in Stride itself (see the self-hosting sketch in 2.10).

### 2.4 Triggers
**Built-in trigger modes** map onto hardware counters / DMA interrupts and cost only index bookkeeping per sample:

| Mode | Fires | Hardware analogue |
| :--- | :--- | :--- |
| `TriggerFull` | every `capacity` samples | DMA transfer-complete |
| `TriggerHalf` | every `capacity / 2` samples | DMA half-transfer + complete (ping-pong) |
| `TriggerQuarter` | every `capacity / 4` samples | quad buffering / 75 % overlap |
| `TriggerPeriodic` | every `hop` samples | STFT hop |

A built-in trigger fires on a push when the buffer holds at least a full frame (`count ≥ frameSize`) and `hop` samples have arrived since the last trigger. Built-in triggers therefore always deliver full frames. The first one fires once `frameSize` samples are in.

**Trigger block.** A buffer can register a switch as its `trigger` property. The switch is an ordinary block, written by streams, so the condition (transient detector, GPIO, threshold, …) is plain Stride:

```stride
switch Flush { default: off }

buffer Capture {
    type: Real
    capacity: 1024
    trigger: Flush
}

[Level, Threshold] >> Greater() >> Flush;
AudioIn >> Capture;
Capture.ready >> Analyse(buffer: Capture);
```

Semantics (the only mode for now):

* **Partial flush.** When the switch is on at push time, `ready` fires on that tick even if the frame isn't full. The frame view holds `min(count, frameSize)` valid samples, oldest first, starting at element 0. Elements beyond that are unspecified, so reactions must use `Buf.count`.
* After the reacting tick, the buffer **empties**: on the next push, the write index, `count` and the hop counter restart at 0, and the next built-in trigger needs a full frame again. The reset is deferred to the next push, so reactions running in the flush tick still read the partial frame.
* The trigger **adds** to the built-in trigger mode (logical or). A flush on the same tick as a built-in trigger fires once.
* The switch is **level-sensitive** and **sampled when the sample is pushed**. Write it before the push stream to take effect on the same tick, otherwise the previous tick's value is used. Each tick it stays on flushes again.
* **Cost.** The flush check, the pending-flush flag and the reset only exist when a buffer registers a `trigger`. Buffers without one generate the same code as before.

A condition can still trigger a **reaction** directly (`TransientDetected >> Analyse(buffer: Capture);`). That reads whatever the buffer holds, without flushing it.

Triggers coming from another domain are deferred until cross-domain buffering is designed. They will use whatever mechanism moves data between domains then (section 8).

### 2.5 Triggering reactions
Reactions have no main input ports; their input is the trigger switch (AGENTS.md §10). The buffer is passed as a **property**, so the reaction gets the data without a copy:

```stride
AnalysisBuffer.ready >> FFT(buffer: AnalysisBuffer) >> [Magnitude, Phase];
```

The property argument is a pointer to the **oldest sample of the current frame**, so `InBuf[0]` is the oldest sample and `InBuf[N - 1]` the newest. Inside, the reaction sizes its outputs from the buffer, the same way modules already use `InputPort.size`:

```stride
reaction FFT {
    ports: [
        propertyInputPort BufferPort { name: "buffer" block: InBuffer }
        mainOutputPort MagPort   { block: Mag }
        mainOutputPort PhasePort { block: Phase }
    ]
    blocks: [
        signal Mag[(InBuffer.size / 2) + 1]   { type: Real }
        signal Phase[(InBuffer.size / 2) + 1] { type: Real }
    ]
}
```

(Sizing reaction blocks from `InBuffer.size` inside the reaction is not implemented yet. Fixtures declare the size explicitly.)

### 2.6 Transfer semantics (resolutions of the "sink size" discussion)
1. **Sink size = window length N.** `Buf >> Sink[N]` reads N elements. The compiler checks `N == Buf.frameSize` and reports an error if they differ.
2. **`hop` controls the advance**, not the sink. This is what makes overlapping STFT windows possible.
3. **Scalar sink** (`Buf >> AudioOut`) drains one sample per tick of the consumer domain. There is no `.sample` property; the sink's declaration decides.
4. **Different sizes need an explicit bridging buffer**: `DmaRx >> Accumulator >> ...`. Buffer → buffer is a **consuming pop + accumulate**.
5. **Peek vs pop.** Reading a frame into a reaction or analysis block is a **non-destructive peek**. Chained buffer transfers and FIFO reads **pop**. A fan-out from one buffer must not shift data for its siblings. (A trigger-block flush is the one exception: it empties the buffer for everyone after the tick.)
6. **Prefer zero-copy property passing** (`ready >> R(buffer: B)`) over `B >> Frame[N] >> R()`. The second form allocates a stack copy and adds a `memcpy`.

### 2.7 Windowing and vectorization
Windowing is just elementwise multiplication by a constant table:

```stride
constant HannCoeffs[1024] { type: Real ... }
InBuffer * HannCoeffs >> Windowed;
```

Vectorization is a **general** codegen property of array/bundle arithmetic (`+ - * /`), lowered to SIMD (AVX/NEON/MVE) or CMSIS-DSP. It is not specific to windows.

**Window tables (decided).** Window shapes are written as **compile-time generator functions** (`Hann(N)`, `Hamming(N)`, `Blackman(N)`), which the compiler evaluates into **precomputed read-only constant arrays**. Source stays short and size-generic (`Hann(InBuffer.size)`). The emitted code is the same as a hand-written constant table: no runtime computation, foldable lookups, and placement in flash/ROM on embedded targets. Explicit constant arrays stay supported for custom windows.

### 2.8 Storage strategy by mode
| Mode | Backing store | Notes |
| :--- | :--- | :--- |
| `ModePingPong` / `TriggerHalf` | 1 × capacity | DMA writes in place; halves exposed as views. **Never mirrored.** |
| `ModeFifo` | 1 × capacity | head/tail ring |
| `ModeSliding` | decided by the compiler (below) | Contiguous windows across the wrap for SIMD/FFT |
| `ModeOverlapAdd` | ≥ frameSize + hop | accumulate frames, drain samples |

**Mirroring is decided by the compiler.** Frames start at multiples of the hop. A frame can only cross the end of the ring if frames overlap (`frameSize > hop`) or the hop doesn't divide the capacity. In those cases the compiler mirrors the storage (2 × capacity, each sample written twice), so the newest N samples are always contiguous at `[index + capacity - n]`. Otherwise the ring is used as is and the frame is the last completed hop-aligned block. Partial flushes restart the ring at 0, so they keep that alignment.

| Example | Mirrored |
| :--- | :--- |
| `capacity 1024, frameSize 1024, TriggerFull` | no |
| `capacity 8, ModePingPong` (N = H = 4) | no |
| `capacity 1024, frameSize 1024, hop 256` | yes (overlap) |
| `capacity 6, frameSize 4, hop 4` | yes (hop doesn't divide capacity) |

Memory-constrained targets will be able to turn mirroring off through the system binding (2.9), falling back to mask/modulo indexing. The power-of-two capacity → mask-wrap optimization can be applied automatically.

### 2.9 Platform / system binding
```stride
system RP2040Target {
    bind AnalysisBuffer: {
        section:    ".sram_dma"
        alignment:  64
        dmaChannel: 1
        mirror:     off
    }
}
```
Desktop/JIT default: aligned storage in the domain state struct.

### 2.10 Additional behaviours identified (for completeness)
Tapped delay line (multiple read offsets), triple buffering (lock-free across unsynchronized clocks), pre-trigger/history capture (N samples before and M after an event), and delimiter/packet FIFOs (UART/SPI/MIDI framing).

Self-hosting sketch: a buffer could eventually be a library `module` with a persistent array, index signals and a `ready` output port, written entirely in Stride.

---

## 3. What Is Implemented

### 3.1 Files
| File | Change |
| :--- | :--- |
| [bufferspec.hpp](../include/stride/stridejit/bufferspec.hpp) / [bufferspec.cpp](../src/bufferspec.cpp) | New `BufferSpec`: resolves `capacity`/`frameSize`/`hop` (constant expressions), `mode`/`triggerMode` (`Buffer::` constants), the `trigger` block, defaults, validation and the mirroring decision. Single source for every other part. |
| [binaryexprast.hpp](../include/stride/stridejit/binaryexprast.hpp) / [binaryexprast.cpp](../src/binaryexprast.cpp) | `BufferPushExprAST` rewritten on `BufferSpec` (count/hop accounting, mirror write, trigger block). New `BufferViewExprAST` returns the frame pointer. |
| [stridecompiler.cpp](../src/stridecompiler.cpp) | Buffer storage sized from `BufferSpec::storageSize()` |
| [stridegenerator.cpp](../src/stridegenerator.cpp) | Synthesizes `<B>_index`, `_count`, `_since`, `_ready`, `_flush` (only with a trigger block) and the constant `_size`. `Buf.ready` is a trigger only for buffers. Buffer property arguments become `BufferViewExprAST`. `<Domain>_init` resets the buffers its streams push into. |
| [strideenvironment.cpp](../src/strideenvironment.cpp) | `generateIr` fails on invalid buffers and on access to private buffer properties |
| [tests/jit_buffer_tests.cpp](../tests/jit_buffer_tests.cpp) | See section 5, Phase 5 |

### 3.2 Generated behaviour of `X >> B`
```
if flush pending:  index = count = since = 0        # trigger block only
store X → B[index]   (and B[index + capacity] when mirrored)
index = index + 1 == capacity ? 0 : index + 1
count = min(count + 1, capacity)
since = since + 1
ready = (count >= frameSize && since >= hop) || trigger   # "|| trigger" only with a trigger block
since = ready ? 0 : since
flush pending = trigger                                   # trigger block only
```
`ready` is overwritten on every push, so it holds for exactly one tick. A triggered reaction stream (`B.ready >> R(...)`) is wrapped in the existing conditional (`ResetExprAST`) on the global `B_ready`.

### 3.3 Frame view passed to reactions
```
n = min(count, frameSize)
mirrored:      start = index + capacity - n
not mirrored:  start = count < frameSize ? 0
                     : (floor(index / hop) * hop + capacity - frameSize) mod capacity
pointer = &B[start]
```
The view is in bounds and in order on every tick, not only when `ready` is on.

---

## 4. Known Issues and Deviations

**Correctness / semantics**
1. `ModeFifo` and `ModeOverlapAdd` are rejected. Scalar drain and buffer → buffer transfers don't exist yet.
2. `firstHalf` / `secondHalf` views for ping-pong are not implemented. Ping-pong reactions receive the ready half as the frame.
3. Elements of a partial frame beyond `min(count, frameSize)` are unspecified (no zero-padding).
4. Reaction blocks can't be sized from the buffer property (`InBuf[InBuffer.size]`) yet.

**Architecture**

5. **State lives in LLVM globals.** This matches how root-level signals are handled today, but it conflicts with AGENTS.md §11 (state must be stack-allocated in state structs, not stored in globals). Buffers inside modules/reactions aren't supported at all.
6. **`buffer` is not declared in the framework schema/type library.** Validation is done in C++ by `BufferSpec`, not by the schema, and `CodeAnalysis::determineNodeRole` has no specific knowledge of buffers.
7. **The `MemberAccessAST` global fallback is still generic.** Validation now rejects private buffer properties, but codegen still resolves any `X.y` to a global `X_y`.
8. **Synthesized declarations** (`_index`, etc.) are built in C++. This is exactly the hidden, language-invisible logic the design wants to avoid.
9. `Buffer::` constants are matched by name and namespace, not resolved through an import of `package Buffer`.

**Tests / tooling**

10. CTest registers `stridejit_tests` as a single test, so `ctest -R "JITBufferTest"` matches nothing (section 6).
11. Not covered: C header emission (AGENTS.md §12 parameter ordering), `stridec` AOT, debug info for buffer state.

---

## 5. Remaining Work (Roadmap)

### Phase 0: Stabilize what's merged
- [ ] Split the working-tree changes into (a) buffer work and (b) unrelated fixes; commit both.
- [ ] Build and run `JITBufferTest` (section 6) and fix failures.
- [x] Add `framework: _JitFramework` to the fixtures.
- [x] Reset buffer state in `Domain_init`.

### Phase 1: Make `buffer` a first-class, explicit entity
- [ ] Declare the `buffer` type and its property schema in the framework library, with an explicit public/private split.
- [x] Resolve `mode` and `triggerMode` to `Buffer::` constants (error on unknown values) instead of substring matching.
- [x] Evaluate `capacity`, `frameSize` and `hop` from constants and expressions; validate `capacity ≥ frameSize ≥ hop > 0`.
- [~] Buffer-aware property resolution: private properties are rejected by validation; codegen still uses the generic fallback.
- [ ] Move buffer state into domain/module **state structs** (AGENTS.md §11), including `defaultConstant` initialization; support buffers inside modules.

### Phase 2: Transfer semantics
- [x] Count/hop-based trigger accounting for all modes.
- [x] Ordered read view for reaction property passing (compiler-decided mirroring).
- [x] Trigger block with partial flush.
- [ ] `frameSize == sink.size` compile-time check with a clear diagnostic.
- [ ] Scalar drain (`B >> Scalar`) and buffer → buffer accumulate (pop semantics).
- [ ] Peek vs pop rules for fan-out.
- [~] `ModePingPong`: frames done; `firstHalf` / `secondHalf` views missing.
- [x] `ModeSliding` mirrored storage (opt-out comes with the system binding).
- [ ] `ModeOverlapAdd` accumulate/drain.
- [ ] `ModeFifo` with overrun/underrun behaviour defined.
- [ ] Size reaction blocks from the buffer property (`InBuf[InBuffer.size]`).

### Phase 3: Spectral processing
- [ ] Generic vectorized array arithmetic in codegen (verify LLVM vectorizes, or emit vector IR explicitly).
- [ ] Window generators (`Hann(N)`, …) evaluated at compile time into constant arrays (2.7).
- [ ] `FFT` / `IFFT` reactions that read size and type from the buffer property; host backend (e.g. KissFFT) and CMSIS-DSP backend via `platformModule`.
- [ ] Spectral representation (polar bundles first; `Complex` type later).

### Phase 4: Platform and toolchain
- [ ] `system ... { bind <Buffer>: {...} }` syntax and plumbing (section, alignment, DMA channel, cache policy, `mirror: off`).
- [ ] DMA interrupt generation for HT/TC on embedded targets.
- [ ] C header emission and `stridec` AOT support for buffers.

### Phase 5: Tests (fixtures under `tests/data/`)
| Fixture | Verifies | State |
| :--- | :--- | :--- |
| `buffer_trigger.stride` | `TriggerFull` on capacity | written |
| `buffer_sliding_window_hop.stride` | N=4, H=2 over capacity 8: overlapping frames in order across the wrap; `_init` restart | written |
| `buffer_periodic_wrap.stride` | hop that doesn't divide the capacity (mirrored) | written |
| `buffer_dma_pingpong.stride` | `TriggerHalf` alternation, no mirroring | written |
| `buffer_partial_flush.stride` | trigger block, partial frame, `Buf.count`, deferred reset | written |
| `buffer_invalid.stride` | `frameSize > capacity` rejected | written |
| `buffer_fifo_scalar.stride` | push/pull across domains | todo |
| `buffer_resizing_accumulate.stride` | 128 → 1024 accumulation | todo |
| `buffer_overlap_add.stride` | reconstruction | todo |
| `stft_reaction_spectral_gate.stride` | end-to-end STFT → gate → ISTFT | todo |
| `buffer_in_module.stride` | state-struct placement, no globals | todo |

Per AGENTS.md §6, always dereference and check values, not just pointer validity.

---

## 6. How to Verify

```powershell
cmake --build build --config Debug --target stridejit_tests
# CTest sees one test named "stridejit_tests"; filter inside gtest instead:
<build-dir>\libgame\external\stridejit\tests\stridejit_tests.exe --gtest_filter=JITBufferTest.*
```

---

## 7. Resolved Questions
1. **Custom trigger conditions** → a switch block registered through the buffer's `trigger` property. It performs a partial flush (2.4). Only this one mode exists for now. Triggers from other domains wait for the cross-domain mechanism.
2. **Mirrored storage for `ModeSliding`** → decided by the compiler from `capacity`, `frameSize` and `hop` (2.8). The platform binding can opt out later.
3. **Window tables** → compile-time generator functions evaluated into precomputed constant arrays (2.7).

### Still open
4. How far should self-hosting go: should `buffer` eventually be a library `module` written in Stride rather than a compiler built-in?
5. Is cross-domain buffering (producer and consumer in different `_domainDefinition`s, possibly on different threads) in scope for v1? It affects whether triple buffering and lock-free semantics are needed early. It also decides how triggers from other domains work.

---

## 8. Future Work (left out on purpose)

Trigger block:
* **Other flush modes.** Wait for a full frame (gate instead of flush), discard without firing, zero-pad the rest of the frame, or flush without emptying the buffer (keep history for sliding windows).
* **Trigger that replaces the built-in mode** instead of adding to it (event-only buffers).
* **Edge-triggered** switches (fire once on off → on) instead of level-sensitive ones.
* **Flush from the reaction side**: let a consumer acknowledge/consume a frame.
* **Triggers from other domains**, resolved together with cross-domain buffering.

Storage and views:
* `mirror: off` in the system binding, with mask/modulo indexing for memory-constrained targets.
* `firstHalf` / `secondHalf` views for ping-pong.
* Zero-filled storage on `_init` (today stale samples remain but are never part of a valid frame).
