# Multi-Rate Processing Compilation in StrideJIT

This document details the architecture, semantic analysis, and LLVM code generation mechanisms for multi-rate signal processing within `stridejit`.

---

## 1. Overview and Core Concepts

In Stride, a domain coordinates execution timing with a declared base rate:
```stride
_domainDefinition AudioDom {
    rate: 48000
}
```

Signals and modules placed within a domain may process data at different rates:
- **Base Rate ($R_{chain} = R_{domain}$)**: Synchronous execution on each domain tick.
- **Downsampling ($R_{chain} < R_{domain}$)**: Decimated execution where operations run less frequently than the domain base rate.
- **Upsampling ($R_{chain} > R_{domain}$)**: Over-sampled execution where operations run multiple times per domain tick.

### One-Shot Domains
For one-shot domains (domains with `rate <= 0` such as event-driven or reactive one-shot coordinators), rate annotations have no physical timing meaning. During semantic analysis (`CodeValidator`), explicit signal rates in one-shot domains are ignored and a compiler warning (`LangError::IgnoredRateInOneShotDomain`) is emitted.

---

## 2. Rate Analysis and Propagation

Before code generation begins, semantic passes in `stridegen` (`CodeAnalysis` and `CodeResolver`) resolve signal rates:

1. **Explicit Rates**: Signals with declared `rate:` property (e.g. `signal S { rate: 100 }`).
2. **Forward Propagation**: In streams (`A >> B >> C`), downstream unrated signals adopt the rate of upstream producers.
3. **Reverse Propagation**: Upstream unrated signals adopt the rate of downstream consumers when the producer lacks an explicit rate.
4. **Domain Default Fallback**: Signals that remain unrated default to the domain's base rate ($R_{domain}$).

---

## 3. On-The-Fly Stream Segment Compilation

Stride preserves whole stream pipelines rather than decomposing or splitting them into isolated sub-graphs. During domain processing code generation (`StrideGenerator::createDomainProcessingFunction`):

1. The compiler traverses the stream AST and inspects the rate of each connected block.
2. Contiguous sub-segments sharing an identical rate specification ($R_{chain}$) are grouped into rate segments.
3. If $R_{chain} == R_{domain}$, operations in the segment are emitted directly into the domain execution block.
4. If $R_{chain} \neq R_{domain}$, a `RateScheduleSpec` is computed and wrapped on-the-fly into either a `DownsampleExprAST` or an `UpsampleExprAST` AST node:
   - Downsampling segments are guarded by decimation branches.
   - Upsampling segments are wrapped in execution loops.

Transitions between rates (e.g., `FastIn (400 Hz) >> FastMid >> SlowOut (25 Hz)`) within a single stream chain are handled cleanly across consecutive segments.

```
+-----------------------------------------------------------+
| Domain Tick (100 Hz)                                      |
|                                                           |
|   +---------------------------------------------------+   |
|   | Upsample Loop: FastIn >> FastMid (4x = 400 Hz)    |   |
|   +---------------------------------------------------+   |
|                             |                             |
|                             v                             |
|   +---------------------------------------------------+   |
|   | Decimation Check: (phase == 0) ? SlowOut (25 Hz)  |   |
|   +---------------------------------------------------+   |
+-----------------------------------------------------------+
```

---

## 4. Downsampling ($R_{chain} < R_{domain}$)

### Rate Ratio & Scheduling
Given domain rate $R_{domain}$ and target chain rate $R_{chain}$:
- **Integer Downsampling ($R_{domain} / R_{chain} = M \in \mathbb{Z}^+$)**:
  Decimation factor $M$. A counter $c \in [0, M - 1]$ increments each domain tick. The segment executes when $c == 0$.
- **Rational / Fractional Downsampling**:
  Reduced to integer ratio $P / Q$ via greatest common divisor (GCD). A Bresenham phase accumulator increments by $P$ on each tick; whenever the accumulator reaches or exceeds $Q$, the segment fires and the accumulator subtracts $Q$.

### Persistent Phase State
Because decimation spans multiple domain process calls, the phase counter must persist across domain ticks:
- **Zero Globals**: The phase counter is stored in the domain's caller-allocated stack `__state` struct.
- **Dynamic Field Registration**: During AST pre-scan, `state.addDynamicDomainField("__rate_phase_<Domain>_<idx>", ...)` allocates space in the state struct *before* `buildStateStructTypes` executes.
- **Initialization & Reset**: Domain initialization (`<Domain>_init`) resets all rate phase fields to 0.

### LLVM IR Generation (`DownsampleExprAST`)
The compiler generates a conditional branch:
```llvm
; Load phase accumulator from __state
%phase_ptr = getelementptr inbounds %RateDom_state, ptr %__state, i32 0, i32 0
%phase = load i32, ptr %phase_ptr, align 4

; Check condition (phase == 0)
%cond = icmp eq i32 %phase, 0
br i1 %cond, label %downsample.body, label %downsample.exit

downsample.body:
; ... execute decimated stream segment ...
br label %downsample.exit

downsample.exit:
; Advance phase accumulator modulo M
%next_phase = add i32 %phase, 1
%rem = srem i32 %next_phase, %M
store i32 %rem, ptr %phase_ptr, align 4
```

---

## 5. Upsampling ($R_{chain} > R_{domain}$)

### Rate Ratio & Loop Execution
When the target chain rate is faster than the domain rate:
- **Integer Upsampling ($R_{chain} / R_{domain} = N \in \mathbb{Z}^+$)**:
  The segment runs $N$ times within a single domain tick.
- **Stateless Operation**:
  Because all $N$ iterations complete inside the current domain tick, integer upsampling requires no persistent phase state across domain calls.

### LLVM IR Generation (`UpsampleExprAST`)
The compiler synthesizes a loop with an induction variable:
```llvm
br label %upsample.loop.body

upsample.loop.body:
%iter = phi i32 [ 0, %entry ], [ %iter.next, %upsample.loop.inc ]
; ... execute upsampled stream segment ...
br label %upsample.loop.inc

upsample.loop.inc:
%iter.next = add i32 %iter, 1
%done = icmp sge i32 %iter.next, %N
br i1 %done, label %upsample.loop.exit, label %upsample.loop.body

upsample.loop.exit:
; continue domain execution
```

---

## 6. State Struct Integration & Re-entrancy Invariants

1. **State Isolation**:
   Rate phase fields are allocated purely as members of the domain state struct. No global variables are emitted into the LLVM Module.
2. **Re-entrant Domain Execution**:
   Because all state resides in caller-provided memory (`__state`), multiple independent instances of a domain can run concurrently or sequentially without cross-talk.
3. **Compatibility with Nested Modules & Loops**:
   Nested functions (such as stateful modules inside loops or reactions) allocate their own sub-structs within the state hierarchy. Registering dynamic rate phase fields at the root domain level does not interfere with nested module state indices.

---

## 7. Future Extensions: Filter and Interpolation Hooks

The multi-rate architecture is designed to support DSP sample rate conversion modules:
- **Anti-Aliasing Downsampling**: Insertion of sliding-window decimation or finite impulse response (FIR) filters prior to decimation branches.
- **Interpolation for Upsampling**: Insertion of polynomial or linear interpolation between domain ticks rather than sample-and-hold repetition.
- **Arbitrary Fractional Rates**: Extension of `RateScheduleSpec` to support time-varying rational phase increments with polyphase filter banks.
