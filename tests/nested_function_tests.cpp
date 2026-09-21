#include "gtest/gtest.h"

// stridejit
#include "stride/stridejit/strideenvironment.hpp"

// llvm
#include "llvm/ExecutionEngine/JITSymbol.h"

TEST(NestedFunctions, ModuleInLoop) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_in_loop.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  Entry(list1, &out1);
  EXPECT_EQ(out1, 12);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  Entry(list2, &out2);
  EXPECT_EQ(out2, 66);
}

TEST(NestedFunctions, ReactionInLoop) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction_in_loop.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  Entry(list1, &out1);
  EXPECT_EQ(out1, 12);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  Entry(list2, &out2);
  EXPECT_EQ(out2, 66);
}

TEST(NestedFunctions, LoopInLoop) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop_in_loop.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  Entry(list1, &out1);
  EXPECT_EQ(out1, 12);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  Entry(list2, &out2);
  EXPECT_EQ(out2, 66);
}

TEST(NestedFunctions, ModuleInReaction) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_in_reaction.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(double *, double *)>();
  ASSERT_NE(Entry, nullptr);

  double in1 = 5.0;
  double out1 = 0.0;
  Entry(&in1, &out1);
  EXPECT_DOUBLE_EQ(out1, 7.0);

  double in2 = 10.5;
  double out2 = 0.0;
  Entry(&in2, &out2);
  EXPECT_DOUBLE_EQ(out2, 12.5);
}

TEST(NestedFunctions, ReactionInReaction) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "reaction_in_reaction.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(double *, double *)>();
  ASSERT_NE(Entry, nullptr);

  double in1 = 5.0;
  double out1 = 0.0;
  Entry(&in1, &out1);
  EXPECT_DOUBLE_EQ(out1, 7.0);

  double in2 = 10.5;
  double out2 = 0.0;
  Entry(&in2, &out2);
  EXPECT_DOUBLE_EQ(out2, 12.5);
}

TEST(NestedFunctions, LoopInReaction) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop_in_reaction.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t in1 = 1;
  int32_t out1 = 0;
  Entry(&in1, &out1);
  EXPECT_EQ(out1, 3);

  int32_t in2 = 10;
  int32_t out2 = 0;
  Entry(&in2, &out2);
  EXPECT_EQ(out2, 12);
}

TEST(NestedFunctions, ModuleInModule) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module_in_module.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(double *, double *)>();
  ASSERT_NE(Entry, nullptr);

  double in1 = 5.0;
  double out1 = 0.0;
  Entry(&in1, &out1);
  EXPECT_DOUBLE_EQ(out1, 10.0);

  double in2 = 10.5;
  double out2 = 0.0;
  Entry(&in2, &out2);
  EXPECT_DOUBLE_EQ(out2, 15.5);
}

TEST(NestedFunctions, ReactionInModule) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction_in_module.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(double *, double *)>();
  ASSERT_NE(Entry, nullptr);

  double in1 = 3.0;
  double out1 = 1.0;
  Entry(&in1, &out1);
  EXPECT_DOUBLE_EQ(out1, 1.0);

  double in2 = 6.0;
  double out2 = 1.0;
  Entry(&in2, &out2);
  EXPECT_DOUBLE_EQ(out2, 6.0);

  double in3 = 10.0;
  double out3 = 1.0;
  Entry(&in3, &out3);
  EXPECT_DOUBLE_EQ(out3, 10.0);
}

TEST(NestedFunctions, LoopInModule) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "loop_in_module.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  Entry(list1, &out1);
  EXPECT_EQ(out1, 6);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  Entry(list2, &out2);
  EXPECT_EQ(out2, 60);
}

TEST(NestedFunctions, StatefulModuleInLoop) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "module_state_in_loop.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  Entry(list1, &out1);
  EXPECT_EQ(out1, 6);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  Entry(list2, &out2);
  EXPECT_EQ(out2, 60);
}

TEST(NestedFunctions, StatefulModuleInReaction) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "module_state_in_reaction.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t in1 = 5;
  int32_t out1 = 0;
  Entry(&in1, &out1);
  EXPECT_EQ(out1, 5);

  int32_t in2 = 12;
  int32_t out2 = 0;
  Entry(&in2, &out2);
  EXPECT_EQ(out2, 12);
}

TEST(NestedFunctions, StatefulModuleInModule) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "module_state_in_module.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t in1 = 5;
  int32_t out1 = 0;
  Entry(&in1, &out1);
  EXPECT_EQ(out1, 15);

  int32_t in2 = 8;
  int32_t out2 = 0;
  Entry(&in2, &out2);
  EXPECT_EQ(out2, 18);
}

TEST(NestedFunctions, MultipleStatefulModulesInLoop) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "multiple_state_modules_in_loop.stride");
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("RootDomain_process");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<void (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  Entry(list1, &out1);
  EXPECT_EQ(out1, 18);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  Entry(list2, &out2);
  EXPECT_EQ(out2, 180);
}

TEST(NestedFunctions, StandaloneStatelessModuleInModule) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_in_module.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("TestModule", scope, tree);
  ASSERT_TRUE(ret);

  EXPECT_EQ(strenv.state.TheModule->getFunction("RootDomain_process"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("TestModule"), nullptr);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("TestModule");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<int32_t (*)(double *, double *)>();
  ASSERT_NE(Entry, nullptr);

  double in1 = 5.0;
  double out1 = 0.0;
  Entry(&out1, &in1);
  EXPECT_DOUBLE_EQ(out1, 10.0);

  double in2 = 10.5;
  double out2 = 0.0;
  Entry(&out2, &in2);
  EXPECT_DOUBLE_EQ(out2, 15.5);
}

TEST(NestedFunctions, StandaloneReactionInModule) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "reaction_in_module.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("Max", scope, tree);
  ASSERT_TRUE(ret);

  EXPECT_EQ(strenv.state.TheModule->getFunction("RootDomain_process"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("Max"), nullptr);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym = strenv.getFunction("Max");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<int32_t (*)(double *, double *)>();
  ASSERT_NE(Entry, nullptr);

  double in1 = 3.0;
  double out1 = 0.0;
  Entry(&out1, &in1);
  EXPECT_DOUBLE_EQ(out1, 0.0);

  double in2 = 8.0;
  double out2 = 0.0;
  Entry(&out2, &in2);
  EXPECT_DOUBLE_EQ(out2, 8.0);
}

TEST(NestedFunctions, StandaloneLoopInModule) {
  strd::StrideEnvironment strenv;

  auto tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "loop_in_module.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("TestModule", scope, tree);
  ASSERT_TRUE(ret);

  EXPECT_EQ(strenv.state.TheModule->getFunction("RootDomain_process"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("TestModule"), nullptr);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("TestModule");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  auto *Entry = EntrySym->toPtr<int32_t (*)(int32_t *, int32_t *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  Entry(&out1, list1);
  EXPECT_EQ(out1, 6);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  Entry(&out2, list2);
  EXPECT_EQ(out2, 60);
}

TEST(NestedFunctions, StandaloneStatefulAccumulator) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_module.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("InnerAccumulator", scope, tree);
  ASSERT_TRUE(ret);

  EXPECT_EQ(strenv.state.TheModule->getFunction("RootDomain_process"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("InnerAccumulator"), nullptr);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("InnerAccumulator");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  struct InnerState {
    int32_t Acc{10};
  };

  auto *Entry =
      EntrySym->toPtr<int32_t (*)(int32_t *, int32_t *, InnerState *)>();
  ASSERT_NE(Entry, nullptr);

  InnerState state{10};
  int32_t in1 = 5;
  int32_t out1 = 0;
  Entry(&out1, &in1, &state);
  EXPECT_EQ(out1, 15);
  EXPECT_EQ(state.Acc, 15);

  int32_t in2 = 8;
  int32_t out2 = 0;
  Entry(&out2, &in2, &state);
  EXPECT_EQ(out2, 23);
  EXPECT_EQ(state.Acc, 23);
}

TEST(NestedFunctions, StandaloneStatefulModuleInModule) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_module.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("OuterModule", scope, tree);
  ASSERT_TRUE(ret);

  EXPECT_EQ(strenv.state.TheModule->getFunction("RootDomain_process"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("OuterModule"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("InnerAccumulator"), nullptr);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("OuterModule");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  struct InnerState {
    int32_t Acc{10};
  };
  struct OuterState {
    InnerState inner;
  };

  auto *Entry =
      EntrySym->toPtr<int32_t (*)(int32_t *, int32_t *, OuterState *)>();
  ASSERT_NE(Entry, nullptr);

  OuterState state;
  state.inner.Acc = 10;
  int32_t in1 = 5;
  int32_t out1 = 0;
  Entry(&out1, &in1, &state);
  EXPECT_EQ(out1, 15);
  EXPECT_EQ(state.inner.Acc, 15);

  int32_t in2 = 8;
  int32_t out2 = 0;
  Entry(&out2, &in2, &state);
  EXPECT_EQ(out2, 23);
  EXPECT_EQ(state.inner.Acc, 23);
}

TEST(NestedFunctions, StandaloneStatefulModuleInReaction) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_reaction.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("TestReaction", scope, tree);
  ASSERT_TRUE(ret);

  EXPECT_EQ(strenv.state.TheModule->getFunction("RootDomain_process"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("TestReaction"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("Counter"), nullptr);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("TestReaction");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  struct CounterState {
    int32_t Count{0};
  };
  struct ReactionState {
    CounterState counter;
  };

  auto *Entry =
      EntrySym->toPtr<int32_t (*)(int32_t *, int32_t *, ReactionState *)>();
  ASSERT_NE(Entry, nullptr);

  ReactionState state;
  state.counter.Count = 0;
  int32_t in1 = 5;
  int32_t out1 = 0;
  Entry(&out1, &in1, &state);
  EXPECT_EQ(out1, 5);
  EXPECT_EQ(state.counter.Count, 5);

  int32_t in2 = 7;
  int32_t out2 = 0;
  Entry(&out2, &in2, &state);
  EXPECT_EQ(out2, 12);
  EXPECT_EQ(state.counter.Count, 12);
}

TEST(NestedFunctions, StandaloneStatefulModuleInLoop) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_loop.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("TestLoop", scope, tree);
  ASSERT_TRUE(ret);

  EXPECT_EQ(strenv.state.TheModule->getFunction("RootDomain_process"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("TestLoop"), nullptr);
  EXPECT_NE(strenv.state.TheModule->getFunction("Accumulator"), nullptr);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("TestLoop");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  struct AccumulatorState {
    int32_t Total{0};
  };
  struct LoopState {
    AccumulatorState accumulator;
  };

  auto *Entry =
      EntrySym
          ->toPtr<int32_t (*)(int32_t *, int32_t *, LoopState *, int32_t)>();
  ASSERT_NE(Entry, nullptr);

  LoopState state;
  state.accumulator.Total = 0;
  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  Entry(&out1, list1, &state, 3);
  EXPECT_EQ(out1, 6);
  EXPECT_EQ(state.accumulator.Total, 6);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  Entry(&out2, list2, &state, 3);
  EXPECT_EQ(out2, 66);
  EXPECT_EQ(state.accumulator.Total, 66);
}

TEST(NestedFunctions, StandaloneStatefulOpaqueAllocation) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_module.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("OuterModule", scope, tree);
  ASSERT_TRUE(ret);

  EXPECT_TRUE(strenv.hasState("OuterModule"));
  EXPECT_GT(strenv.getStateSize("OuterModule"), 0u);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  llvm::Expected<llvm::orc::ExecutorAddr> EntrySym =
      strenv.getFunction("OuterModule");
  ASSERT_TRUE(static_cast<bool>(EntrySym));

  // Users do not need to define InnerState or OuterState!
  void *state = strenv.allocateState("OuterModule");
  ASSERT_NE(state, nullptr);

  auto *Entry = EntrySym->toPtr<int32_t (*)(int32_t *, int32_t *, void *)>();
  ASSERT_NE(Entry, nullptr);

  int32_t in1 = 5;
  int32_t out1 = 0;
  Entry(&out1, &in1, state);
  EXPECT_EQ(out1, 15);

  int32_t in2 = 8;
  int32_t out2 = 0;
  Entry(&out2, &in2, state);
  EXPECT_EQ(out2, 23);

  strenv.deallocateState(state);
}

TEST(NestedFunctions, StandaloneGenericInvocation) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_module.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("OuterModule", scope, tree);
  ASSERT_TRUE(ret);

  // Programmatic inspection of arguments
  auto argsInfo = strenv.getFunctionArgs("OuterModule");
  ASSERT_EQ(argsInfo.size(), 3);
  EXPECT_EQ(argsInfo[0].name, "Output");
  EXPECT_EQ(argsInfo[0].role, strd::FunctionArgInfo::Role::Output);
  EXPECT_TRUE(argsInfo[0].isPointer);

  EXPECT_EQ(argsInfo[1].name, "Input");
  EXPECT_EQ(argsInfo[1].role, strd::FunctionArgInfo::Role::Input);
  EXPECT_TRUE(argsInfo[1].isPointer);

  EXPECT_EQ(argsInfo[2].name, "__state");
  EXPECT_EQ(argsInfo[2].role, strd::FunctionArgInfo::Role::State);
  EXPECT_TRUE(argsInfo[2].isPointer);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  void *state = strenv.allocateState("OuterModule");
  ASSERT_NE(state, nullptr);

  int32_t in1 = 5;
  int32_t out1 = 0;
  void *callArgs1[] = {&out1, &in1, state};
  strenv.invoke("OuterModule", callArgs1);
  EXPECT_EQ(out1, 15);

  int32_t in2 = 8;
  int32_t out2 = 0;
  void *callArgs2[] = {&out2, &in2, state};
  strenv.invoke("OuterModule", callArgs2);
  EXPECT_EQ(out2, 23);

  strenv.deallocateState(state);
}

TEST(NestedFunctions, StandaloneLoopGenericInvocation) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_loop.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("TestLoop", scope, tree);
  ASSERT_TRUE(ret);

  auto argsInfo = strenv.getFunctionArgs("TestLoop");
  ASSERT_EQ(argsInfo.size(), 4);
  EXPECT_EQ(argsInfo[0].role, strd::FunctionArgInfo::Role::Output);
  EXPECT_EQ(argsInfo[1].role, strd::FunctionArgInfo::Role::Input);
  EXPECT_EQ(argsInfo[2].role, strd::FunctionArgInfo::Role::State);
  EXPECT_EQ(argsInfo[3].role, strd::FunctionArgInfo::Role::PortProperty);
  EXPECT_FALSE(argsInfo[3].isPointer);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  void *state = strenv.allocateState("TestLoop");
  ASSERT_NE(state, nullptr);

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  int32_t size = 3;
  void *callArgs1[] = {&out1, list1, state, &size};
  strenv.invoke("TestLoop", callArgs1);
  EXPECT_EQ(out1, 6);

  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  void *callArgs2[] = {&out2, list2, state, &size};
  strenv.invoke("TestLoop", callArgs2);
  EXPECT_EQ(out2, 66);

  strenv.deallocateState(state);
}

TEST(NestedFunctions, StandaloneArgumentQueries) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_loop.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("TestLoop", scope, tree);
  ASSERT_TRUE(ret);

  // Total count
  EXPECT_EQ(strenv.getFunctionArgCount("TestLoop"), 4u);

  // Query by index
  auto arg0 = strenv.getFunctionArg("TestLoop", 0);
  ASSERT_TRUE(arg0.has_value());
  EXPECT_EQ(arg0->name, "Output");
  EXPECT_EQ(arg0->role, strd::FunctionArgInfo::Role::Output);
  EXPECT_EQ(arg0->type, strd::DataType::INT32);
  EXPECT_EQ(arg0->elementSize, 4u);
  EXPECT_TRUE(arg0->isPointer);

  // Query by name
  auto argInput = strenv.getFunctionArg("TestLoop", "Input");
  ASSERT_TRUE(argInput.has_value());
  EXPECT_EQ(argInput->role, strd::FunctionArgInfo::Role::Input);
  EXPECT_EQ(argInput->type, strd::DataType::INT32);
  EXPECT_EQ(argInput->elementSize, 4u);
  EXPECT_TRUE(argInput->isPointer);

  auto argNonExistent = strenv.getFunctionArg("TestLoop", "NonExistent");
  EXPECT_FALSE(argNonExistent.has_value());

  // Query index by name
  EXPECT_EQ(strenv.getFunctionArgIndex("TestLoop", "Output"), 0);
  EXPECT_EQ(strenv.getFunctionArgIndex("TestLoop", "Input"), 1);
  EXPECT_EQ(strenv.getFunctionArgIndex("TestLoop", "__state"), 2);
  EXPECT_EQ(strenv.getFunctionArgIndex("TestLoop", "InputPort_size"), 3);
  EXPECT_EQ(strenv.getFunctionArgIndex("TestLoop", "NonExistent"), -1);

  // Verify full ordered list
  auto args = strenv.getFunctionArgs("TestLoop");
  ASSERT_EQ(args.size(), 4u);
  EXPECT_EQ(args[0].name, "Output");
  EXPECT_EQ(args[0].role, strd::FunctionArgInfo::Role::Output);
  EXPECT_EQ(args[0].type, strd::DataType::INT32);
  EXPECT_TRUE(args[0].isPointer);

  EXPECT_EQ(args[1].name, "Input");
  EXPECT_EQ(args[1].role, strd::FunctionArgInfo::Role::Input);
  EXPECT_EQ(args[1].type, strd::DataType::INT32);
  EXPECT_TRUE(args[1].isPointer);

  EXPECT_EQ(args[2].name, "__state");
  EXPECT_EQ(args[2].role, strd::FunctionArgInfo::Role::State);
  EXPECT_EQ(args[2].type, strd::DataType::STATE);
  EXPECT_GT(args[2].elementSize, 0u);
  EXPECT_TRUE(args[2].isPointer);

  EXPECT_EQ(args[3].name, "InputPort_size");
  EXPECT_EQ(args[3].role, strd::FunctionArgInfo::Role::PortProperty);
  EXPECT_EQ(args[3].type, strd::DataType::INT32);
  EXPECT_FALSE(args[3].isPointer);
}

TEST(NestedFunctions, StandaloneInvokerParameterList) {
  strd::StrideEnvironment strenv;

  auto tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                                   "module_state_in_loop.stride");
  ASSERT_TRUE(tree);
  strenv.prepareTree(tree);

  strd::ScopeStack scope;
  auto ret = strenv.generateStandaloneFunction("TestLoop", scope, tree);
  ASSERT_TRUE(ret);

  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  void *state = strenv.allocateState("TestLoop");
  ASSERT_NE(state, nullptr);

  // Create parameter list helper
  auto params = strenv.createInvokerParamList("TestLoop");
  EXPECT_EQ(params.size(), 4u);
  EXPECT_FALSE(params.isComplete());

  int32_t list1[3] = {1, 2, 3};
  int32_t out1 = 0;
  int32_t size = 3;

  // Populate parameter list by name
  EXPECT_TRUE(params.setArg("Output", &out1));
  EXPECT_TRUE(params.setArrayArg("Input", list1, size));
  EXPECT_TRUE(params.setState(state));

  EXPECT_TRUE(params.isComplete());

  // Invoke dynamically via InvokerParameterList
  int32_t retCode = strenv.invoke("TestLoop", params);
  EXPECT_EQ(retCode, 0);
  EXPECT_EQ(out1, 6);

  // Re-use parameter list with new input
  int32_t list2[3] = {10, 20, 30};
  int32_t out2 = 0;
  EXPECT_TRUE(params.setArg("Output", &out2));
  EXPECT_TRUE(params.setArrayArg("Input", list2, 3));

  retCode = strenv.invoke("TestLoop", params);
  EXPECT_EQ(retCode, 0);
  EXPECT_EQ(out2, 66); // Cumulative accumulation with state

  strenv.deallocateState(state);
}
