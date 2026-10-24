# Stride Source-Level Debugger Architecture & Usage Guide

This document describes the architecture, internal implementation, and usage of source-level debugging in Stride and StrideJIT.

---

## 1. Architectural Overview

Stride compilation targets native machine code either via Ahead-Of-Time (AOT) object emission (`stridec`) or Just-In-Time (JIT) execution (`StrideJit` / LLVM ORC). The debugging subsystem enables developers to step through `.stride` source code, inspect signals and state variables, set breakpoints, and capture causal execution traces using standard debuggers (GDB, LLDB, Visual Studio, VS Code).

```
                      ┌──────────────────────────────────────────┐
                      │            .stride Source Code           │
                      └────────────────────┬─────────────────────┘
                                           │
                         Bison Parser (%locations, 1-based col)
                                           │
                                           ▼
                      ┌──────────────────────────────────────────┐
                      │    AST with Precise Source Locations     │
                      │       (File, Line, Column tracking)      │
                      └────────────────────┬─────────────────────┘
                                           │
                                           ├──────────────────────────────────────────┐
                                           │                                          │
                                           ▼                                          ▼
                      ┌──────────────────────────────────────────┐   ┌───────────────────────────────┐
                      │           StrideGenerator / IR           │   │      StrideDebugEmitter       │
                      │         (LLVM Module Generation)         │   │       (llvm::DIBuilder)       │
                      └────────────────────┬─────────────────────┘   └───────────────┬───────────────┘
                                           │                                         │
                                           │   Attaches DILocation to Instructions   │
                                           │   Emits DISubprogram & DILocalVariable  │
                                           │   Generates DICompositeType for State   │
                                           │◄────────────────────────────────────────┘
                                           │
                                           ▼
                      ┌──────────────────────────────────────────┐
                      │    LLVM Module with DWARF Metadata       │
                      │  (Debug Info Version 3, CodeView/DWARF)  │
                      └────────────────────┬─────────────────────┘
                                           │
                      ┌────────────────────┴─────────────────────┐
                      │                                          │
                      ▼ (AOT: stridec -g -O0)                    ▼ (JIT: StrideEnvironment)
       ┌──────────────────────────────┐          ┌──────────────────────────────┐
       │     Relocatable ELF/COFF     │          │    ORC JIT ExecutionEngine   │
       │     Object with DWARF (.o)   │          │  (GDB Registration Listener) │
       └──────────────┬───────────────┘          └──────────────┬───────────────┘
                      │                                         │
                      ▼                                         ▼
       ┌──────────────────────────────┐          ┌──────────────────────────────┐
       │   GDB / LLDB / Visual Studio │          │   In-Memory JIT Debugging    │
       │   Single-Step, Breakpoints,  │          │   GDB / LLDB / IDE Debugger  │
       │   Hover Variable Inspection  │          │   Native Frame Resolution    │
       └──────────────────────────────┘          └──────────────────────────────┘
```

---

## 2. Frontend Source Location Tracking

### 2.1 Bison Parser Integration
The Stride parser (`strideparser`) uses Bison `%locations` directives with 1-indexed column tracking.

- **Lexer Tracking**: `YY_USER_ACTION` updates `yylloc.first_line`, `yylloc.first_column`, `yylloc.last_line`, and `yylloc.last_column` on every matched token.
- **AST Nodes**: Every AST node (`AST::SourceLocation`) records:
  - `filename`: canonical source file path.
  - `startLine`, `startColumn`: 1-based start coordinates.
  - `endLine`, `endColumn`: 1-based end coordinates.
- **Location Propagation**: Compound constructs (stream pipelines, binary expressions, domain reactions, loop generators, state machine transitions) preserve the exact span of source code.
- **Serialization**: `zpp_ast_serializer` persists source locations across binary `.scache` tiers without data loss.

---

## 3. DWARF Metadata Generation (`StrideDebugEmitter`)

The `StrideDebugEmitter` class encapsulates `llvm::DIBuilder` and manages the lifecycle of DWARF debugging metadata during LLVM IR generation.

### 3.1 Compile Unit and File Descriptors
- **`DICompileUnit`**: Emitted once per LLVM module. Configured with:
  - `Language`: `llvm::dwarf::DW_LANG_C_plus_plus_14` (or `DW_LANG_Rust` depending on host tooling compatibility).
  - `Producer`: `Stride Compiler (stridec / stridejit)`.
  - `EmissionKind`: `llvm::DICompileUnit::FullDebugInfo` or `LineTablesOnly`.
- **`DIFile`**: Created per source file with absolute path and parent directory resolution.

### 3.2 Subprogram Scopes (`DISubprogram`)
Debug functions are created for each executable code block:
- **Domain Processors**: `<Domain>_process` mapped to the domain definition block.
- **Domain Initializers**: `<Domain>_init` mapped to domain property initializers.
- **Reaction Handlers**: Attached to discrete `on` reaction triggers.
- **Actions and State Handlers**: Attached to state machine entry, exit, and transition actions.

### 3.3 Prologue and Step-Into (F11) Cleanliness
To prevent the debugger from stepping into low-level runtime setup or trampoline code, compiler-generated entry points have their debug location cleared (`builder.SetCurrentDebugLocation(llvm::DebugLoc())` / line 0). The first actual `DILocation` is set on the first user-written statement in the body, guaranteeing that **Step Into (F11)** directly highlights the first source line.

### 3.4 Variable Scopes and Composite State Structs
Variables are registered via `llvm::DILocalVariable` and `llvm::DIBuilder::insertDeclare`:
- **Signals & Ports**: Local signals and function parameters receive DWARF local variable descriptors referencing their stack alloca or incoming register.
- **Domain State Struct (`__state`)**: Domain state members are exposed via `DICompositeType` (DWARF struct). Each member signal in the state is mapped with its explicit byte offset, allowing debuggers to expand `__state` in the variable watch window.
- **Loop Induction Variables**: Loop generators (`for`, `generator`) push an `ElementContext` onto the debug scope stack, binding the induction variable (e.g., `Index`) to the active inner lexical block (`DILexicalBlock`).

---

## 4. Runtime & JIT Debugger Interfacing

### 4.1 JIT Breakpoints via GDB Registration Listener
When executing JIT-compiled code in-memory, LLVM's `RTDyldObjectLinkingLayer` registers `llvm::JITEventListener::createGDBRegistrationListener()`. 

Whenever an object file is linked in memory:
1. LLVM emits a synthetic GDB JIT descriptor.
2. It calls the `__jit_debug_register_code` hook.
3. GDB and LLDB automatically intercept this hook and load the in-memory DWARF symbol tables.
4. Breakpoints set on Stride source files (e.g., `b SensorDSP.stride:25`) resolve to the JIT-compiled memory address.

### 4.2 Build Types and Optimization Levels
Debugging fidelity is tightly coupled to compiler optimization levels:

| Flag | Build Type | Opt Level | Inlining | DWARF Section Output | Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `-g` / `-g -O0` | `Debug` | `None` (`O0`) | Disabled | Full (`.debug_info`, `.debug_line`, `.debug_ranges`, `.debug_str`) | Interactive debugging, full variable inspection |
| `-gline-tables-only` | `RelWithDebInfo` | `O2` | Enabled | Line tables only (`.debug_line`) | Production profiling, crash backtraces |
| `-O2` / `-O3` | `Release` | `O2` / `O3` | Aggressive | None | Optimized release binaries |
| `-g0` | `Release` | Any | Configurable | Explicitly stripped | Minimal code footprint |

---

## 5. Declarative Debugging & Domain Stepping

In addition to traditional line breakpoints, Stride provides declarative debugging primitives within the language and C++ runtime API.

### 5.1 Programmatic Domain Stepping (`stepTick`)
The `StrideEnvironment::stepTick()` and `DomainStepper` APIs provide fine-grained cycle stepping:

```cpp
#include <stride/stridejit/strideenvironment.hpp>

strd::StrideEnvironment env;
env.setBuildType(strd::BuildType::Debug);
env.setDebugInfoLevel(strd::DebugInfoLevel::Full);
env.loadSource("GameSystem.stride");

// Single-step discrete ticks through domain execution
for (int tick = 0; tick < 10; ++tick) {
    env.stepTick("GameDomain");
    // Query intermediate state or verify invariants
}
```

### 5.2 Declarative Probe and Watch Blocks
Available in `Debug.stride` (`strideroot/library/1.0/Debug.stride`):

- **`Probe[Signal, "Label"]`**: Transparently logs the value of a signal on every cycle without mutating the stream pipeline.
- **`Watch[Signal, Condition]`**: Triggers a diagnostic assertion or breakpoint whenever `Condition` evaluates to true.

### 5.3 Automated Causal Trace on Test Failure
When using `FunctionTestRunner` with `-g`, assertion failures print a causal dependency slice of signal history leading up to the failed cycle, drastically accelerating debugging of complex state pipelines.

---

## 6. Practical Usage Guide

### 6.1 Compiling with Debug Symbols (`stridec`)

To compile a `.stride` file into a native object file with full DWARF debug symbols:

```bash
# Debug build (-g, -O0)
stridec -g -O0 MySystem.stride -o MySystem.o

# Inspect generated DWARF sections (Linux/macOS)
llvm-objdump -h MySystem.o | grep debug

# Inspect generated DWARF sections (Windows)
llvm-obj2yaml MySystem.o
```

Expected DWARF sections:
- `.debug_info`: Subprograms, variables, and type descriptors.
- `.debug_line`: Source file, line, and column matrix.
- `.debug_ranges`: Lexical scope address intervals.
- `.debug_str`: String pool for identifiers and filenames.

---

### 6.2 Debugging with LLDB

Launch LLDB with your host executable or test harness:

```bash
lldb ./build/bin/stridelang_tests
```

Set breakpoints using Stride source locations or generated function names:

```text
(lldb) breakpoint set --file SensorDSP.stride --line 25
(lldb) breakpoint set --name SensorDSP_process
(lldb) run
```

When hitting the breakpoint:

```text
* thread #1, stop reason = breakpoint 1.1
    frame #0: 0x00007ff6a24101e0 `SensorDSP_process(__state=0x000000c283aff630, RawSensorValue=105.2, AlertThreshold=100.0, ...) at SensorDSP.stride:25:5
   22   [RawSensorValue, AlertThreshold] >> SensorDSP >> [FilteredValue, AlertTriggered];
   23
   24   domain SensorDSP {
-> 25       FilteredValue = LowPassFilter(RawSensorValue, 0.1);
   26       AlertTriggered = FilteredValue > AlertThreshold;
   27   }
```

Inspect variables:

```text
(lldb) frame variable
(double) RawSensorValue = 105.2
(double) AlertThreshold = 100.0
(double) FilteredValue = 94.6
(bool) AlertTriggered = false

(lldb) print *__state
(SensorDSP_State) $0 = {
  LowPassFilter_000_PrevFiltered = 93.4
}
```

Single-step execution:
- `n` (`next`): Step to next line within domain.
- `s` (`step`): Step into sub-function or reaction handler.
- `c` (`continue`): Resume execution until next breakpoint or watchpoint.

---

### 6.3 Debugging in VS Code (`launch.json`)

To debug Stride programs in Visual Studio Code using the `CodeLLDB` or C/C++ extension:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug Stride Program",
            "type": "lldb",
            "request": "launch",
            "program": "${workspaceFolder}/build/bin/stridetest.exe",
            "args": ["${file}", "--root", "${workspaceFolder}/strideroot"],
            "cwd": "${workspaceFolder}",
            "stopOnEntry": false,
            "sourceMap": {
                ".": "${workspaceFolder}"
            }
        }
    ]
}
```

With this configuration:
1. Open any `.stride` file in VS Code.
2. Click in the gutter to place a red breakpoint dot on a line.
3. Press **F5** to start debugging.
4. VS Code halts on the breakpoint, displays variables in the **Run & Debug** side panel, and supports **F10 (Step Over)** and **F11 (Step Into)**.
