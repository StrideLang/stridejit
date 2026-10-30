# StrideJIT Architecture & Development Guidelines

This document outlines critical architectural patterns, code generation invariants, and user constraints for developing and testing in `stridejit`.

## 1. User & Workflow Constraints

> [!IMPORTANT]
> **Never run build commands directly.**
> Always prompt the user to build and run unit tests with `ctest`:
> ```powershell
> cmake --build build --config Debug
> ctest --test-dir build --output-on-failure -R "<TestNameRegex>"
> ```

- **Link Formatting**: Always format file and code references as clickable markdown links with `file:///` and forward slashes.

---

## 2. Domain Function Calling Conventions & JIT Lookups

- **Parameter Ordering**:
  - Domain process functions (`<Domain>_process`) generated from `_domainDefinition` take **all declared inputs followed by all declared outputs** in their exact declaration order.
  - Every input and output argument is passed as a **pointer** (`ptr` in LLVM IR) to caller-allocated storage:
    ```cpp
    // For inputs: [ InInt, InReal ], outputs: [ Out ]
    auto *Entry = EntrySym->toPtr<void (*)(int32_t*, double*, bool*)>();
    Entry(&inInt, &inReal, &out);
    ```
- **Outputs vs. Globals**:
  - Declarations inside `outputs: [ ... ]` are parameters passed by pointer to `_process`. They are **not** global signals and cannot be looked up via `strenv.getGlobal<T>`.
  - Only signals/switches declared at the root/domain level outside of `inputs`/`outputs` are globals accessible via `strenv.getGlobal<T>("Name")`.
  - A `constant` with a `value` is a read-only internal global. A `constant` without one (`constant Table[8] { type: Int }`) is a **writable** internal global, written once at run time; stridegen's `CodeValidator::findConstantRewrites` rejects a second write.
  - A standalone function called from C++ (an action, no caller instance) takes no parameters for module globals; its body reads and writes them directly. Called from another function, the caller still passes them in.
- **Output port sizes**: `signal Output[OutputPort.size]` in a loop or module takes the size of what the call writes to (the call's `outputBlock`), as `InputPort.size` takes the size of what feeds it. An array is written in place; a slice or bundle (`>> T[2:4]`) goes through a temporary array copied to each member after the call (`CallExprAST::codegen`, `scatterOuts`). Array outputs are never prefilled with their `default`. A sized port with nothing to size it is an `UnsizedPort` error from stridegen's `CodeValidator::findUnsizedPorts`. Fixtures: `loop_output_size.stride`, `loop_output_cases.stride`, `loop_output_chained.stride`, `loop_output_unwritten.stride`, `loop_input_unfed.stride`, `constant_fill.stride`. The language-level description is strideroot's `library/1.0/doc/CodeGenerators.md`.

---

## 3. Typecasting & Compiler Properties

- **Typecast Preprocessing**:
  - The AST preprocessing pass (`CodeResolver::resolveTypeCastForStream`) identifies heterogeneous lists and streams, tagging AST nodes with the `"typecast"` compiler property (e.g. `Real` or `Int`).
- **Cached Out DataType Caveat**:
  - `CodeAnalysis::resolveNodeOutDataType` caches type names under `resolvedOutDataType_` *before* typecasting resolution finishes.
  - **Always inspect `node->getCompilerProperty("typecast")` first** before querying `resolveNodeOutDataType`:
    ```cpp
    std::string typeStr;
    auto typecastNode = node->getCompilerProperty("typecast");
    if (typecastNode && typecastNode->getNodeType() == AST::String) {
      typeStr = std::static_pointer_cast<ValueNode>(typecastNode)->getStringValue();
    } else {
      typeStr = CodeAnalysis::resolveNodeOutDataType(node, scope, tree);
    }
    ```
- **List Consistency (`ListExprAST`)**:
  - Heterogeneous lists whose elements have been marked with `"typecast"` must be treated as `IMMUTABLE_CONSISTENT` (for literals) or `MUTABLE_CONSISTENT` (for variables), not `UNSUPPORTED`.
  - In `ListExprAST::codegen`, inspect `"typecast"` on elements to convert integer literals to double (`values.push_back(double(val))`) or float literals to integer when constructing constant array IR.
  - In `StrideGenerator::collectInputArgs`, inspect element `"typecast"` properties so that function arguments reflect cast types for overload resolution.

---

## 4. Bundles & Dynamic Variable Indexing

- When indexing bundles dynamically using a variable (`Bundle[Index]`):
  - Check whether the index `llvm::Value*` is a pointer.
  - If it is an alloca/stack pointer, emit `builder->CreateLoad` to obtain the integer scalar *before* passing it to `builder->CreateGEP`.
  - Passing an un-dereferenced pointer directly to GEP produces LLVM verification failures or C++ compiler errors (`illegal indirection`).

---

## 5. Reactions, Loops, and `isCodeGenerator`

- When checking whether a function or block is inline/generates code, always query `ASTQuery::findTypeDeclaration(scope, tree, ...)`.
- Internal signals in reactions and loops must remain local PHI or stack variables and must not leak into the domain function's external parameter list.
- `createStreamCode` groups the code of a stream by domain. Only top-level domain streams may be split between domains; a stream inside a module, loop or reaction goes entirely into the domain of its first member. Split, a value written into a block of another domain (`Found >> Output[Index]` where the resolver gave `Found` the caller's domain) is looked up in the wrong group and the write is silently dropped (`loop_output_reads_domain.stride`).

---

## 6. Unit Testing Practices

- **Fixture Files**: Located under `tests/data/` and included using `STRIDEJIT_TESTS_SOURCE_DIR "<fixture>.stride"`.
- **Isolate Concerns**:
  - In polymorphism tests (e.g. `Equal@Int_Bool` vs `Equal@Double_Bool`), use exact literal types (`0` vs `0.0`) so that overload resolution does not depend on typecasting.
  - Test typecasting in dedicated fixtures/tests (e.g. `typecast_list.stride`, `typecast_stream.stride`).
- **Assert Values**: In addition to validating non-null pointers (`EXPECT_TRUE(ptr)`), always dereference and check actual values (`EXPECT_TRUE(*ptr)`, `EXPECT_FALSE(*ptr)`).

---

## 7. Native LLVM Command Templates & Instruction Formats

- **Strict `llvm::` Prefix Requirement**:
  - Native LLVM IR instructions specified in `platformModule` `processing:` strings MUST use the explicit `llvm::` prefix:
    ```stride
    processing: "%%outtokens:0%% = llvm::icmp sgt %%intokens:0%%, %%intokens:1%%"
    processing: "%%outtokens:0%% = llvm::fcmp ogt %%intokens:0%%, %%intokens:1%%"
    ```
  - **Never** add ad-hoc fallback branches or omit the `llvm::` prefix in `LLVMCommandAST::codegen`. When non-standard syntax is encountered, update the `.stride` fixture rather than adding parser heuristics.
- **Valid Condition Codes**:
  - LLVM `icmp` instructions strictly require explicit signedness: `sgt`, `sge`, `slt`, `sle`, `ugt`, `uge`, `ult`, `ule`, `eq`, `ne`.
  - Do not use non-standard shorthand like `gt`. Use `sgt` for signed comparisons or `ugt` for unsigned comparisons.

---

## 8. Nested Scope Resolution & Shadowing Invariants

- **Check Innermost Scope First**:
  - In `CodeAnalysis::determineNodeRole`, always query the local scope (`scope.back()`) *before* searching `outerScope`.
  - Inner declarations shadow outer declarations. Checking `outerScope` first incorrectly flags local loop/reaction variables (like `Done`, `Step`, or `Index`) as `External`, replacing local stack/PHI variables with pointer arguments passed down from caller functions.

---

## 9. Bundled Arguments in Module & Loop Calls

- **Offset by `outArgCount`**:
  - When bundling multiple input arguments into an intermediate array (`CallArgs.size() > outArgCount + 1`), the inputs in `CallArgs` start at index `outArgCount + i`. Never index directly from `0`, which points to output arguments.
- **Pass Alloca Pointer Directly**:
  - Pass the alloca pointer itself (`arrayAlloc`) directly as the argument to the callee. Never call `builder->CreateLoad` on the buffer memory.
- **Single-Offset GEP**:
  - For dynamic array allocas (`CreateAlloca(elemType, size)`), compute element addresses using single-index GEP: `builder->CreateInBoundsGEP(elemType, arrayAlloc, index)`. Double indexing `{0, index}` is only valid for fixed-size LLVM array types.

---

## 10. Reaction Calling Conventions & Conditional Execution

- **Reactions with Declared Input Ports**:
  - Reactions cannot declare mainInputPorts, they can only have propertyInputPorts, an input to a reaction is a trigger/switch to
    determing if the reaction should run. A switch converts to a trigger automatically (passing its value). Actions
    follow the same rule (they share `CallableType::Reaction`). stridegen's `CodeValidator::findReactionMainInputs`
    reports a main input declared by a reaction or an action as a `ReactionMainInput` error, and
    `JITControlFlowTest.NoReactionDeclaresAMainInput` keeps the fixtures free of them. `CallExprAST::codegen` has no
    input path for them: what comes before a reaction call is always its trigger.
  - A call's properties are passed in the order the callee declares its property ports, matched by name
    (`collectPropertyArgs`), so `F(b: 1 a: 10)` and `F(a: 10 b: 1)` are the same call (`property_order.stride`).
- **Switch-Triggered Reactions (0 Input Ports)**:
  - Reactions without declared input ports that are fed by a stream switch/condition (`expectedInArgs == 0 && !InArgs.empty()`) generate a conditional branch (`CreateCondBr`) around the reaction call.

---

## 11. Nested Function Calls & State Struct Management

- **Callable State Ownership (`doesNodeNeedState`)**:
  - **Loops and Reactions do NOT have state of their own** across domain calls. Their internal signals remain local stack allocas or loop PHI nodes.
  - **Modules only have state if they declare persistent variables** (`!node->persistent.empty()`). Not all modules need state.
  - **Inherited State**: A callable (module, reaction, or loop) needs a state struct if and only if it has persistent variables of its own OR any nested child callable requires state.
  - **Zero-Overhead for Stateless Combinations**: Stateless combinations (e.g. `reaction_in_loop`, `loop_in_loop`, `reaction_in_reaction`, stateless modules) must NOT generate or pass a state struct.
  - **No Domain State Struct**: Domains are execution coordinators, not modules; state is never wrapped in a root domain struct.

- **Stack Allocation (No LLVM Globals for State)**:
  - Persistent module state must never be stored in global LLVM variables.
  - Top-level caller functions (domain process functions) allocate the state struct on the stack in the entry block using `state.CreateEntryBlockAlloca`.
  - Nested callers slice child state sub-structs using `builder->CreateStructGEP`.

- **Recursive `defaultConstant` Initialization**:
  - State structs allocated on the stack must **not** be zero-initialized with `Constant::getNullValue`. Zero-initialization clobbers non-zero signal defaults (e.g. `signal Acc { default: 10 reset: Reset }`).
  - `buildStateStructTypes` builds a compile-time `llvm::ConstantStruct` (`defaultConstant`) recursively from the bottom up, embedding primitive default values and child sub-struct default constants.
  - In `CallExprAST::codegen`, stack allocas are initialized via `builder->CreateStore(calleeInfo->defaultConstant, statePtrVal)`.
  - Persistent variables must **never** be re-initialized at the start of a module function body (`allocateInternalVariables`), as that would wipe accumulated state across calls.

- **Independent Variable Resets**:
  - When an independent reset is triggered on a persistent variable, look up its compile-time default constant using `StateStructInfo::getDefaultValue(varName)` (via `defaultConstant->getOperand(varIndices[varName])`) and store it back into the variable's GEP pointer.

- **MSVC Smart Pointer Ternary Operator**:
  - When assigning smart pointers of different derived AST types (e.g., `shared_ptr<FunctionNode>` vs `shared_ptr<DeclarationNode>`) to `ASTNode` via ternary operator `? :`, MSVC does not deduce the base type automatically. Explicitly cast both arms:
    ```cpp
    newfunc->funcInstance = funcInstance ? ASTNode(funcInstance) : ASTNode(funcDecl);
    ```

---

## 12. Multi-File Compilation & Public C Header Emission

- **Closed Scope per File**:
  - In `StrideEnvironment::generateIr(const std::vector<std::string> &paths, ...)`:
  - Each `.stride` file is parsed and preprocessed with its own fresh `globalScope` (containing only framework/platform modules).
  - Never share local module/signal declarations across un-imported files.

- **AST Tree Retention (`m_trees`)**:
  - Retain all parsed root ASTs in `m_trees` across multi-file inputs.
  - Querying `mStrideEnv.m_tree` only inspects the last compiled file; always search across all `m_trees` (and fallback to `FunctionProtos` and `TheModule`) when discovering domain definitions and port outputs.

- **C Header ABI Parameter Ordering**:
  - Standalone and domain function prototypes in emitted C headers MUST match the exact parameter ordering generated by `PrototypeAST::codegen`:
    1. `OutArgs`: `typeName* name`
    2. `InArgs`: `const typeName* name`
    3. `PropertyArgs`: `const typeName* name`
    4. `InternalPersistentArgs` (state struct): `void* name`
    5. `ExternalArgs`: `typeName* name`
    6. `UsedPortProperties`: `typeName name` (passed by value)


