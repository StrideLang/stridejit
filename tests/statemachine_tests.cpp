#include "gtest/gtest.h"

// stridejit
#include "stride/codegen/coderesolver.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// stride
#include "stride/codegen/codeanalysis.hpp"
#include "stride/codegen/codevalidator.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

// llvm
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

TEST(StateMachine, DomainInitialization) {
  strd::ASTNode tree;
  tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "statemachines.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strd::ScopeStack scope;

  strenv.prepareTree(tree);

  strd::CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());

  if (!validator.isValid()) {
    for (auto &error : validator.getErrors()) {
      std::cerr << error.getErrorText() << std::endl;
    }
  }

  strenv.generateIr(tree);

  bool foundVar = false;
  auto domainDecl =
      strd::ASTQuery::findDeclarationByName("RootDomain", scope, tree);
  if (domainDecl && strenv.state.dynamicDomainFields.find(domainDecl) !=
                        strenv.state.dynamicDomainFields.end()) {
    for (auto &f : strenv.state.dynamicDomainFields[domainDecl]) {
      if (f.name == "__RootDomain_MyStateMachine_active_state_id")
        foundVar = true;
    }
  }
  EXPECT_TRUE(foundVar);
}

TEST(StateMachine, Flattening) {
  strd::ASTNode tree;
  tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "statemachines.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strd::ScopeStack scope;

  strenv.prepareTree(tree);

  strd::CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());

  if (!validator.isValid()) {
    for (auto &error : validator.getErrors()) {
      std::cerr << error.getErrorText() << std::endl;
    }
  }

  auto domainDecl =
      strd::ASTQuery::findDeclarationByName("RootDomain", scope, tree);
  EXPECT_NE(domainDecl, nullptr);

  auto smContexts =
      strd::StateMachine::collectStateMachines(domainDecl, scope, tree);
  EXPECT_EQ(smContexts.size(), 1);
  auto &sm = smContexts[0];
  EXPECT_EQ(sm.name, "MyStateMachine");
  // Expected to contain MyStateMachine itself (as state) and State1, State2,
  // State3
  EXPECT_EQ(sm.flattenedStates.size(), 4);
  EXPECT_EQ(sm.initialStateId, 1);
  EXPECT_EQ(sm.activeStateVarName,
            "__RootDomain_MyStateMachine_active_state_id");

  // Verify flattened states
  EXPECT_EQ(sm.flattenedStates[0].id, 1);
  EXPECT_EQ(sm.flattenedStates[0].stateDecl->getName(), "MyStateMachine");

  EXPECT_EQ(sm.flattenedStates[1].id, 2);
  EXPECT_EQ(sm.flattenedStates[1].stateDecl->getName(), "State1");

  EXPECT_EQ(sm.flattenedStates[2].id, 3);
  EXPECT_EQ(sm.flattenedStates[2].stateDecl->getName(), "State2");

  EXPECT_EQ(sm.flattenedStates[3].id, 4);
  EXPECT_EQ(sm.flattenedStates[3].stateDecl->getName(), "State3");
}

TEST(StateMachine, StreamScoping) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_streams.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  bool success = strenv.generateIr(tree);

  // `processStateStreams` sets up the ScopeStack appropriately for each nested
  // state. If scoping was broken, `generateIr` would fail to resolve `Counter`
  // or `LocalSig` inside the streams.
  EXPECT_TRUE(success);
}

TEST(StateMachine, DomainInvokerGeneration) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_streams.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  // The domain should have its _init and _process invokers compiled and exposed
  // to the JIT. Before the fix, these functions were compiled to IR but never
  // exposed with invokers.
  auto initSym = strenv.getFunction("RootDomain_init_invoker");
  EXPECT_TRUE(static_cast<bool>(initSym))
      << "RootDomain_init_invoker not found in JIT";
  if (!initSym)
    llvm::consumeError(initSym.takeError());

  auto processSym = strenv.getFunction("RootDomain_process_invoker");
  EXPECT_TRUE(static_cast<bool>(processSym))
      << "RootDomain_process_invoker not found in JIT";
  if (!processSym)
    llvm::consumeError(processSym.takeError());
}

TEST(StateMachine, CodegenExecution) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_streams.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_init", args);

  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 2);

  auto *counter = strenv.getGlobal<int32_t>("Counter");
  ASSERT_NE(counter, nullptr);
  EXPECT_EQ(*counter, 0);

  // onEntry sets 1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*counter, 1);

  // onProcess adds 1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*counter, 2);

  // onProcess adds 1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*counter, 3);
}

TEST(StateMachine, TransitionLoading) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_transitions.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);

  strd::CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());

  strd::ScopeStack scope;
  auto domainDecl =
      strd::ASTQuery::findDeclarationByName("RootDomain", scope, tree);
  EXPECT_NE(domainDecl, nullptr);

  auto smContexts =
      strd::StateMachine::collectStateMachines(domainDecl, scope, tree);
  EXPECT_EQ(smContexts.size(), 1);
  auto &sm = smContexts[0];
  EXPECT_EQ(sm.name, "MyStateMachine");

  // State1 should have 1 transition
  auto &state1 = sm.flattenedStates[1]; // MyStateMachine is 0, State1 is 1
  EXPECT_EQ(state1.stateDecl->getName(), "State1");
  EXPECT_EQ(state1.transitions.size(), 1);

  auto &t1 = state1.transitions[0];
  EXPECT_EQ(t1.transitionDecl->getName(), "ToState2");
  EXPECT_EQ(t1.targetStateId,
            sm.flattenedStates[2].id); // State2 is ID 3, index 2

  // State2 should have 1 transition
  auto &state2 = sm.flattenedStates[2];
  EXPECT_EQ(state2.stateDecl->getName(), "State2");
  EXPECT_EQ(state2.transitions.size(), 1);

  auto &t2 = state2.transitions[0];
  EXPECT_EQ(t2.transitionDecl->getName(), "ToState3");
  EXPECT_EQ(t2.targetStateId,
            sm.flattenedStates[3].id); // State3 is ID 4, index 3

  // State3 should have 0 transitions
  auto &state3 = sm.flattenedStates[3];
  EXPECT_EQ(state3.stateDecl->getName(), "State3");
  EXPECT_EQ(state3.transitions.size(), 0);
}

TEST(StateMachine, TransitionExecution) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_transitions.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  void *args[] = {statePtr.get()};

  auto *counter = strenv.getGlobal<int32_t>("Counter");
  ASSERT_NE(counter, nullptr);

  strenv.invoke("RootDomain_init", args);

  EXPECT_EQ(strenv.getActiveStateId(statePtr), 2); // State1 is 2
  EXPECT_EQ(*counter, 0);

  // Tick the domain without a request, nothing should happen
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 2);
  EXPECT_EQ(*counter, 0);

  // Request transition 1 (ToState2)
  strenv.requestTransition(statePtr, 1);
  strenv.invoke("RootDomain_process", args);

  // activeState should be 3 (State2)
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 3);
  EXPECT_EQ(*counter, 1);
  EXPECT_EQ(strenv.getTransitionRequestId(statePtr), 0);

  // Request transition 2 (ToState3)
  strenv.requestTransition(statePtr, 2);
  strenv.invoke("RootDomain_process", args);

  EXPECT_EQ(strenv.getActiveStateId(statePtr), 4); // State3
  EXPECT_EQ(*counter, 2);
}

TEST(StateMachine, AdvancedFormalizations) {
  strd::ASTNode tree;
  tree =
      strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "statemachines.stride");
  EXPECT_NE(tree, nullptr);

  strd::ScopeStack scope;
  auto domainDecl =
      strd::ASTQuery::findDeclarationByName("RootDomain", scope, tree);
  EXPECT_NE(domainDecl, nullptr);

  auto smContexts =
      strd::StateMachine::collectStateMachines(domainDecl, scope, tree);
  EXPECT_EQ(smContexts.size(), 1);
  auto &sm = smContexts[0];

  // Verify FlattenedState Phase 5 property defaults
  for (const auto &fs : sm.flattenedStates) {
    EXPECT_FALSE(fs.isParallel);
    EXPECT_FALSE(fs.resumeLastState);
    EXPECT_FALSE(fs.isFinal);
  }
}

TEST(StateMachine, AdvancedFormalizationsExecution) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_advanced.stride");
  EXPECT_NE(tree, nullptr);

  strd::ScopeStack scope;
  auto domainDecl =
      strd::ASTQuery::findDeclarationByName("RootDomain", scope, tree);
  EXPECT_NE(domainDecl, nullptr);

  auto smContexts =
      strd::StateMachine::collectStateMachines(domainDecl, scope, tree);
  EXPECT_EQ(smContexts.size(), 1);
  auto &sm = smContexts[0];

  bool foundResumeLast = false;
  bool foundFinal = false;
  for (const auto &fs : sm.flattenedStates) {
    if (fs.stateDecl->getName() == "CompositeState") {
      EXPECT_TRUE(fs.resumeLastState);
      EXPECT_EQ(fs.historyStateVarName,
                "__MyStateMachine_CompositeState_history_state_id");
      foundResumeLast = true;
    }
    if (fs.stateDecl->getName() == "FinalState") {
      EXPECT_TRUE(fs.isFinal);
      foundFinal = true;
    }
  }
  EXPECT_TRUE(foundResumeLast);
  EXPECT_TRUE(foundFinal);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  void *args[] = {statePtr.get()};

  auto *counter = strenv.getGlobal<int32_t>("Counter");
  auto *entryCounter = strenv.getGlobal<int32_t>("EntryCounter");
  auto *exitCounter = strenv.getGlobal<int32_t>("ExitCounter");
  ASSERT_NE(counter, nullptr);
  ASSERT_NE(entryCounter, nullptr);
  ASSERT_NE(exitCounter, nullptr);

  strenv.invoke("RootDomain_init", args);

  // Tick domain while Child1 is active: onProcessCode runs (10 >> Counter)
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*counter, 10);

  // Fire transition to FinalState: onExitCode of Child1 runs (1 >> ExitCounter)
  strenv.requestTransition(statePtr, 1);

  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getTransitionRequestId(statePtr), 0);
  EXPECT_EQ(*exitCounter, 1);
}

TEST(StateMachine, TransitionGuardBlocking) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_guard.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  void *args[] = {statePtr.get()};

  auto *guardSignal = strenv.getGlobal<int32_t>("GuardSignal");
  auto *counter = strenv.getGlobal<int32_t>("Counter");
  ASSERT_NE(guardSignal, nullptr);
  ASSERT_NE(counter, nullptr);

  strenv.invoke("RootDomain_init", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 2); // State1 is 2
  EXPECT_EQ(*guardSignal, 0);
  EXPECT_EQ(*counter, 0);

  // Tick domain while GuardSignal == 0. Transition is guarded by GuardSignal
  // and should NOT fire!
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 2); // Still in State1
  EXPECT_EQ(*counter, 0);

  // Now set GuardSignal to 1 (true)
  *guardSignal = 1;

  // Tick domain while GuardSignal == 1. Transition should fire!
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 3); // State2 is 3
  EXPECT_EQ(*counter, 1); // onTransition stream executed
}

TEST(StateMachine, UpdateGuardParentBeforeChild) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_updateguard.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);

  strd::CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());

  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  void *args[] = {statePtr.get()};

  auto *counter = strenv.getGlobal<int32_t>("Counter");
  auto *parentOrder = strenv.getGlobal<int32_t>("ParentOrder");
  auto *childOrder = strenv.getGlobal<int32_t>("ChildOrder");
  ASSERT_NE(counter, nullptr);
  ASSERT_NE(parentOrder, nullptr);
  ASSERT_NE(childOrder, nullptr);

  strenv.invoke("RootDomain_init", args);
  EXPECT_EQ(*counter, 0);
  EXPECT_EQ(*parentOrder, 0);
  EXPECT_EQ(*childOrder, 0);

  // Tick domain. Parent updateGuard runs first (Counter=1 -> ParentOrder=1),
  // then Child updateGuard runs second (Counter=2 -> ChildOrder=2).
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*parentOrder, 1);
  EXPECT_EQ(*childOrder, 2);
  EXPECT_EQ(*counter, 2);
}

TEST(StateMachine, ParallelStateExecution) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_parallel.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);

  strd::CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());

  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  void *args[] = {statePtr.get()};

  auto *parentCounter = strenv.getGlobal<int32_t>("ParentCounter");
  auto *region1Counter = strenv.getGlobal<int32_t>("Region1Counter");
  auto *region2Counter = strenv.getGlobal<int32_t>("Region2Counter");
  ASSERT_NE(parentCounter, nullptr);
  ASSERT_NE(region1Counter, nullptr);
  ASSERT_NE(region2Counter, nullptr);

  strenv.invoke("RootDomain_init", args);
  EXPECT_EQ(*parentCounter, 0);
  EXPECT_EQ(*region1Counter, 0);
  EXPECT_EQ(*region2Counter, 0);

  // Tick 1: Parallel parent process runs (+1), and child region process streams
  // run (+10, +20)
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*parentCounter, 1);
  EXPECT_EQ(*region1Counter, 10);
  EXPECT_EQ(*region2Counter, 20);

  // Tick 2: All process streams execute again concurrently
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*parentCounter, 2);
  EXPECT_EQ(*region1Counter, 20);
  EXPECT_EQ(*region2Counter, 40);
}

TEST(StateMachine, ResumeLastStateExecution) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_resumelast.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);

  strd::CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());

  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);
  // Initial state resolves to SubState1 (id 3)
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 3);
  EXPECT_EQ(strenv.getHistoryStateId(statePtr, "CompositeState"), 0);

  // Transition to SubState2 (id 4)
  strenv.requestTransition(statePtr, 2); // ToSubState2 is transition 2
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 4); // SubState2

  // Transition to OtherState (id 5)
  strenv.requestTransition(statePtr, 1); // ToOtherState is transition 1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 5); // OtherState
  EXPECT_EQ(strenv.getHistoryStateId(statePtr, "CompositeState"),
            4); // Saved SubState2 in history

  // Transition back to CompositeState (id 2). Because resumeLastState: on,
  // it restores active_state_id to 4 (SubState2) instead of defaulting to 3
  // (SubState1)
  strenv.requestTransition(statePtr, 3); // ToComposite is transition 3
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr), 4); // Restored SubState2!
}
