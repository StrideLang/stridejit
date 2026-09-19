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

---

## 3. Typecasting & Compiler Properties

- **Typecast Preprocessing**:
  - The AST preprocessing pass (`CodeResolver::resolveTypeCastForStream`) identifies heterogeneous lists and streams, tagging AST nodes with the `"typecast"` compiler property (e.g. `_RealType` or `_IntType`).
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
    determing if the reaction should run
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

