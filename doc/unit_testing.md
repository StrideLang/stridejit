# StrideJIT Unit Testing Framework (`StrideTesting` & `stridetest`)

The `StrideTesting` module and `stridetest` CLI provide a complete testing, verification, and micro-benchmarking solution for Stride code running in JIT (in-memory) or AOT (shared object / DLL) modes.

---

## 1. Quickstart Tutorial

### Step 1: Write a Stride Test File
Create a test file named `PassthruTest.stride` (standard assertions like `ExpectEqual` are automatically available without needing an explicit `import Assertions;`):

```stride
_domainDefinition TestDomain {
    framework: _JitFramework
    rate: -1
    inputs:  [ signal In  { domain: TestDomain type: _RealType } ]
    outputs: [ signal Out { domain: TestDomain type: _RealType } ]
}

In >> Out;

functionTest PassthruTest {
    target: TestDomain
    prepare: [
        [ 1.0, 2.0, 3.0, 4.0, 5.0 ] >> In;
    ]
    validate: [
        [ Out, [ 1.0, 2.0, 3.0, 4.0, 5.0 ] ] >> ExpectEqual();
    ]
}
```

### Step 2: Run Tests with the `stridetest` CLI
Execute `stridetest` directly against the test file:

```powershell
stridetest.exe PassthruTest.stride
```

**Output:**
```
================================================================================
                           STRIDE TEST EXECUTION REPORT                         
================================================================================
STATUS    TEST NAME                   TICKS   ALLOCS      MEAN (ns)     JITTER (CV) 
--------------------------------------------------------------------------------
[PASS]    PassthruTest (TestDomain)   5       0           23.4          1.85%       
================================================================================
Summary: 1 passed, 0 failed, 1 total.
================================================================================
```

### Step 3: Run Tests with CTest
All `.stride` test files can also be run seamlessly through CMake's `ctest`:

```powershell
# Run all tests with failure diagnostics
ctest --test-dir build --output-on-failure

# Run only Stride tests using regex matching
ctest --test-dir build --output-on-failure -R "stridetest\.PassthruTest"
```

---

## 2. Unit Testing a Module Directly

You can test any Stride `module` directly by specifying the module name in `target:`. You do not need to wrap the module in a `_domainDefinition` for unit testing.

### Example: Direct Module Testing
```stride
module AddTwo {
    ports: [
        mainInputPort InputPort {
            block: Input
        },
        mainOutputPort OutputPort {
            block: Output
        }
    ]
    blocks: [
        signal Input  { default: 0.0 }
        signal Output { default: 0.0 }
    ]
    streams: [
        Input + 2.0 >> Output;
    ]
}

functionTest AddTwoTest {
    target: AddTwo
    prepare: [
        [ 1.0, 2.0, 3.0, 4.0 ] >> Input;
    ]
    validate: [
        [ Output, [ 3.0, 4.0, 5.0, 6.0 ] ] >> ExpectEqual();
    ]
}
```

---

## 3. Testing Module Fields & Property Inputs

Modules frequently define configurable properties, parameters, and coefficients via `propertyInputPort` or property blocks. In `functionTest`, you can test static values or vary property fields across ticks by streaming value lists into them in the `prepare:` section.

### Example: Modulating Property Fields Over Time
```stride
module Amplifier {
    ports: [
        mainInputPort InPort { block: Input },
        mainOutputPort OutPort { block: Output },
        propertyInputPort FactorPort {
            name: 'gain'
            block: Gain
        }
    ]
    blocks: [
        signal Input { default: 0.0 }
        signal Output { default: 0.0 }
        propertyBlock Gain { default: 1.0 }
    ]
    streams: [
        Input * Gain >> Output;
    ]
}

functionTest DynamicGainTest {
    target: Amplifier
    prepare: [
        # Feed main audio input signal
        [ 1.0, 2.0, 3.0,  4.0 ] >> Input;

        # Modulate the gain property field across ticks
        [ 2.0, 0.5, 10.0, 0.0 ] >> Gain;
    ]
    validate: [
        # Output at each tick reflects (Input[t] * Gain[t])
        [ Output, [ 2.0, 1.0, 30.0, 0.0 ] ] >> ExpectEqual();
    ]
}
```

---

## 4. Testing Stateful Modules with Resets

For stateful modules (accumulators, filters, counters, state machines) containing persistent variables (`persistent: on`) and reset triggers:

```stride
module Accumulator {
    ports: [
        mainInputPort InPort { block: Input },
        mainOutputPort OutPort { block: Output },
        propertyInputPort ResetPort {
            name: 'reset'
            block: Reset
        }
    ]
    blocks: [
        signal Input { default: 0.0 }
        signal Output { default: 0.0 }
        signal Total {
            default: 0.0
            reset: Reset
            persistent: on
        }
        switch Reset { default: off }
    ]
    streams: [
        Total + Input >> Total;
        Total >> Output;
    ]
}

functionTest AccumulatorResetTest {
    target: Accumulator
    prepare: [
        [ 10.0, 20.0, 5.0,  15.0 ] >> Input;
        [ off,  off,  on,   off  ] >> Reset; # Reset fires on tick 2
    ]
    validate: [
        # Tick 0: 0 + 10 = 10
        # Tick 1: 10 + 20 = 30
        # Tick 2: Reset fires -> Total resets to 0 + 5 = 5
        # Tick 3: 5 + 15 = 20
        [ Output, [ 10.0, 30.0, 5.0, 20.0 ] ] >> ExpectEqual();
    ]
}
```

---

## 5. Domain Pipeline & Integration Testing

When testing multiple modules connected in a stream pipeline, or testing rate conversions and domain execution frameworks:

```stride
# Instantiate modules within a domain
_domainDefinition AudioDomain {
    framework: _JitFramework
    rate: 44100
    inputs:  [ signal In  { domain: AudioDomain type: _RealType } ]
    outputs: [ signal Out { domain: AudioDomain type: _RealType } ]
}

In >> Amplifier(gain: 3.0) >> Accumulator() >> Out;

functionTest DomainPipelineTest {
    target: AudioDomain
    prepare: [
        [ 1.0, 2.0, 3.0, 4.0 ] >> In;
    ]
    validate: [
        [ Out, [ 3.0, 9.0, 18.0, 30.0 ] ] >> ExpectEqual();
    ]
}
```

---

## 6. Assertion Library Reference

All assertion modules are built into Stride's standard library root and resolve automatically without imports. Assertions follow standard Stride streaming syntax where a 2-element list containing the source port and expected values list is streamed into the assertion module:

### Exact Equality (`ExpectEqual`)
Validates exact equality for integers/booleans and default precision ($10^{-6}$) for floating-point reals:
```stride
[ Out, [ 10, 20, 30 ] ] >> ExpectEqual();
```

### Tolerance Matching (`ExpectNear`)
Validates that output falls within a specific epsilon boundary ($|actual - expected| \le \epsilon$):
```stride
[ Out, [ 1.414, 1.732, 2.000 ] ] >> ExpectNear(epsilon: 0.001);
```

### Boolean & Switch Verification (`ExpectTrue` / `ExpectFalse`)
Asserts that switch outputs are active (`true` / `on` / `1`) or inactive (`false` / `off` / `0`):
```stride
IsReady >> ExpectTrue();
HasError >> ExpectFalse();
```

### Numerical Bounds (`ExpectGt`, `ExpectGe`, `ExpectLt`, `ExpectLe`)
Asserts inequality conditions per tick:
```stride
[ SignalOut, [ 0.0, 0.0, 0.0 ] ] >> ExpectGt();
[ SignalOut, [ 1.0, 1.0, 1.0 ] ] >> ExpectLe();
```

---

## 7. Multi-Tick Execution & Multi-Signal Pipelines

The test runner automatically coordinates inputs and outputs across multiple signals and discrete ticks:

```stride
functionTest BiquadFilterTest {
    target: BiquadFilterDomain
    prepare: [
        [ 1.0,   0.0,   0.0,   0.0,   0.0   ] >> AudioIn;
        [ 440.0, 440.0, 440.0, 440.0, 440.0 ] >> Cutoff;
        [ 0.707, 0.707, 0.707, 0.707, 0.707 ] >> Q;
    ]
    validate: [
        [ AudioOut, [ 0.25, 0.38, 0.12, -0.05, -0.02 ] ] >> ExpectNear(epsilon: 0.01);
        [ AudioOut, [ 1.0, 1.0, 1.0, 1.0, 1.0 ] ]        >> ExpectLe();
    ]
}
```

- **Input Injection:** At tick $t$, each prepare stream updates its respective input signal or property field.
- **Sequential Validation:** If any assertion fails at tick $t$, testing halts immediately and outputs detailed diagnostics:
  ```
  Assertion failed on port 'AudioOut' at tick 3
    Expected: approx == -0.05 (+/- 0.01)
    Actual:   -0.08
  ```

---

## 8. `stridetest` CLI Tool Reference

`stridetest` can be invoked in multiple modes:

### Single File (JIT Execution)
```powershell
stridetest path/to/MyTest.stride
```

### Directory Test Discovery
Recursively runs all `.stride` files containing `functionTest` blocks within a directory:
```powershell
stridetest tests/data/
```

### Ahead-Of-Time (AOT) Shared Object / DLL Testing
Runs test specifications against a precompiled shared library (`.dll` or `.so`):
```powershell
stridetest tests/data/PassthruTest.stride --lib build/libpassthru.so
```

### Generating JUnit XML Reports for CI/CD
Exports machine-readable XML test results:
```powershell
stridetest tests/data/ --junit reports/stride_results.xml
```

### Repeated Execution (Stress / Benchmarking)
Runs each test a specified number of times:
```powershell
stridetest path/to/MyTest.stride --repeat 10
stridetest path/to/MyTest.stride -r 5
```

### CLI Options
| Flag | Parameter | Description |
|---|---|---|
| `--repeat`, `-r`, `-n` | `<count>` | Run each test `<count>` times. |
| `--lib` | `<path>` | Path to compiled `.so` / `.dll` for dynamic testing. |
| `--junit` | `<path>` | File destination for JUnit XML report. |
| `--root` | `<path>` | Custom `STRIDEROOT` directory path. |
| `-v`, `--verbose` | | Enable detailed trace logging. |
| `-h`, `--help` | | Print usage information. |

---

## 9. CMake & CTest Integration

All `.stride` test files can be registered as standard CTest cases in CMake:

```cmake
file(GLOB STRIDE_TEST_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*Test.stride")

foreach(TEST_FILE ${STRIDE_TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_test(
        NAME "stridetest.${TEST_NAME}"
        COMMAND stridetest "${TEST_FILE}"
    )
    set_tests_properties("stridetest.${TEST_NAME}" PROPERTIES
        LABELS "StrideLanguage"
        TIMEOUT 30
    )
endforeach()
```

### Running with CTest
```powershell
# Run all tests
ctest --test-dir build --output-on-failure

# Filter only Stride language tests
ctest --test-dir build --output-on-failure -R "stridetest\."

# Run a specific test
ctest --test-dir build --output-on-failure -R "stridetest\.PassthruTest"
```

---

## 10. Performance & Memory Profiling

Every executed test automatically collects non-functional metrics:

- **Zero-Heap Allocation Invariant:** Utilizes [`NoHeapAllocGuard`](file:///c:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/include/stride/testing/memorytracker.hpp) to ensure no steady-state allocations occur during signal processing.
- **Latency & Jitter Analysis:** Utilizes [`PerfAnalyzer`](file:///c:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/include/stride/testing/perfanalyzer.hpp) with warmup iterations and multi-pass statistical sampling:
  - **Mean Execution Time ($\mu$):** Average execution time per tick in nanoseconds.
  - **Jitter Coefficient of Variation ($CV = \sigma / \mu$):** Measures latency consistency across ticks. Lower percentages indicate deterministic execution.

---

## 11. C++ API Reference (`StrideTesting` Library)

To embed Stride tests within custom C++ applications or test harnesses, link against `StrideTesting`:

```cpp
#include "stride/testing/specextractor.hpp"
#include "stride/testing/functiontestrunner.hpp"
#include "stride/testing/testreporter.hpp"
#include "stride/stridejit/strideenvironment.hpp"

using namespace strd;
using namespace strd::test;

void runMyStrideTest() {
    // 1. Extract test specifications from AST
    auto specs = SpecExtractor::extractTestsFromFile("MyTest.stride");

    // 2. Compile JIT in memory
    StrideEnvironment strenv;
    strenv.generateIr("MyTest.stride");
    strenv.initializeJIT();
    strenv.compileInMemory();

    auto entrySym = strenv.getFunction("GainDomain_process");
    void* fnPtr = reinterpret_cast<void*>(entrySym->getValue());

    // 3. Initialize Runner & State
    FunctionTestRunner runner(fnPtr);
    auto state = strenv.allocateSharedState("GainDomain");
    runner.setStatePointer(state.get());
    runner.setStateAccessor({
        [&](void* s, const std::string& name, double val) {
            strenv.setStateVar<double>(s, name, val);
        },
        [&](const void* s, const std::string& name) {
            return strenv.getStateVar<double>(s, name).value_or(0.0);
        }
    });

    // 4. Run Test & Report
    TestReporter reporter;
    for (const auto& spec : specs) {
        auto result = runner.runTest(spec);
        reporter.addResult(spec, result);
    }

    reporter.printConsoleSummary(std::cout);
}
```
