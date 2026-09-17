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
