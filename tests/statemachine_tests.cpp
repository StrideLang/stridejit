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
  if (domainDecl && strenv.mStrideEnv.dynamicDomainFields.find(domainDecl) !=
                        strenv.mStrideEnv.dynamicDomainFields.end()) {
    std::string expectedVarName = strd::StateMachine::getVariableName(
        strd::StateMachineField::ActiveStateId, "RootDomain", "MyStateMachine");
    for (auto &f : strenv.mStrideEnv.dynamicDomainFields[domainDecl]) {
      if (f.name == expectedVarName)
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
  EXPECT_EQ(sm.activeStateVarName, strd::StateMachine::getVariableName(
                                       strd::StateMachineField::ActiveStateId,
                                       "RootDomain", "MyStateMachine"));

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
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_init", args);

  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 2);

  auto counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Counter", std::nullopt, "RootDomain").value_or(0);
  };
  EXPECT_EQ(counter(), 0);

  // onEntry sets 1, onProcess adds 1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(counter(), 2);

  // onProcess adds 1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(counter(), 3);

  // onProcess adds 1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(counter(), 4);
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
  void *args[] = {statePtr.get()};

  auto counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Counter", std::nullopt, "RootDomain").value_or(0);
  };

  auto state1Id = strenv.getStateId("State1", "", "RootDomain").value_or(-1);
  auto state2Id = strenv.getStateId("State2", "", "RootDomain").value_or(-1);
  auto state3Id = strenv.getStateId("State3", "", "RootDomain").value_or(-1);
  auto toState2Id = strenv.getTransitionId("ToState2", "", "RootDomain").value_or(-1);
  auto toState3Id = strenv.getTransitionId("ToState3", "", "RootDomain").value_or(-1);

  EXPECT_NE(state1Id, -1);
  EXPECT_NE(state2Id, -1);
  EXPECT_NE(state3Id, -1);
  EXPECT_NE(toState2Id, -1);
  EXPECT_NE(toState3Id, -1);

  strenv.invoke("RootDomain_init", args);

  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), state1Id);
  EXPECT_EQ(counter(), 0);

  // Tick the domain without a request, nothing should happen
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), state1Id);
  EXPECT_EQ(counter(), 0);

  // Request transition (ToState2)
  strenv.requestTransition(statePtr, toState2Id, "", "RootDomain");
  strenv.invoke("RootDomain_process", args);

  // activeState should be State2
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), state2Id);
  EXPECT_EQ(counter(), 1);
  EXPECT_EQ(strenv.getTransitionRequestId(statePtr, "", "RootDomain"), 0);

  // Request transition (ToState3)
  strenv.requestTransition(statePtr, toState3Id, "", "RootDomain");
  strenv.invoke("RootDomain_process", args);

  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), state3Id);
  EXPECT_EQ(counter(), 2);
}

TEST(StateMachine, AdvancedFormalizations) {
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

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  strenv.generateIr(tree);

  // 1. CompositeState with resumeLastState
  auto compositeState = sm.findState("CompositeState");
  ASSERT_NE(compositeState, nullptr);
  EXPECT_TRUE(compositeState->resumeLastState);
  EXPECT_FALSE(compositeState->isParallel);
  EXPECT_FALSE(compositeState->isFinal);
  EXPECT_EQ(compositeState->historyStateVarName,
            strd::StateMachine::getVariableName(
                strd::StateMachineField::HistoryStateId, "MyStateMachine",
                "CompositeState"));
  EXPECT_EQ(strenv.getStateId("CompositeState"), compositeState->id);

  // 2. ParallelState and its parallel sub-states
  auto parallelState = sm.findState("ParallelState");
  ASSERT_NE(parallelState, nullptr);
  EXPECT_TRUE(parallelState->isParallel);
  EXPECT_FALSE(parallelState->resumeLastState);
  EXPECT_FALSE(parallelState->isFinal);
  EXPECT_EQ(strenv.getStateId("ParallelState"), parallelState->id);

  auto parallelSub1 = sm.findState("ParallelSub1");
  ASSERT_NE(parallelSub1, nullptr);
  EXPECT_TRUE(parallelSub1->isParallel);
  EXPECT_FALSE(parallelSub1->resumeLastState);
  EXPECT_FALSE(parallelSub1->isFinal);
  EXPECT_EQ(parallelSub1->parentId, parallelState->id);
  EXPECT_EQ(strenv.getStateId("ParallelSub1"), parallelSub1->id);

  auto parallelSub2 = sm.findState("ParallelSub2");
  ASSERT_NE(parallelSub2, nullptr);
  EXPECT_TRUE(parallelSub2->isParallel);
  EXPECT_FALSE(parallelSub2->resumeLastState);
  EXPECT_FALSE(parallelSub2->isFinal);
  EXPECT_EQ(parallelSub2->parentId, parallelState->id);
  EXPECT_EQ(strenv.getStateId("ParallelSub2"), parallelSub2->id);

  // 3. FinalState with isFinal
  auto finalState = sm.findState("FinalState");
  ASSERT_NE(finalState, nullptr);
  EXPECT_TRUE(finalState->isFinal);
  EXPECT_FALSE(finalState->isParallel);
  EXPECT_FALSE(finalState->resumeLastState);
  EXPECT_EQ(strenv.getStateId("FinalState"), finalState->id);

  // 4. Default/standard states without advanced properties
  auto child1 = sm.findState("Child1");
  ASSERT_NE(child1, nullptr);
  EXPECT_FALSE(child1->isParallel);
  EXPECT_FALSE(child1->resumeLastState);
  EXPECT_FALSE(child1->isFinal);
  EXPECT_EQ(strenv.getStateId("Child1"), child1->id);

  auto child2 = sm.findState("Child2");
  ASSERT_NE(child2, nullptr);
  EXPECT_FALSE(child2->isParallel);
  EXPECT_FALSE(child2->resumeLastState);
  EXPECT_FALSE(child2->isFinal);
  EXPECT_EQ(strenv.getStateId("Child2"), child2->id);
}

TEST(StateMachine, AdvancedFormalizationsExecution) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_advanced.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  void *args[] = {statePtr.get()};

  auto counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Counter", std::nullopt, "RootDomain").value_or(0);
  };
  auto entryCounter = [&]() {
    return strenv.getStateVar(statePtr.get(), "EntryCounter", std::nullopt, "RootDomain").value_or(0);
  };
  auto exitCounter = [&]() {
    return strenv.getStateVar(statePtr.get(), "ExitCounter", std::nullopt, "RootDomain").value_or(0);
  };
  auto parallel1Counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Parallel1Counter", std::nullopt, "RootDomain").value_or(0);
  };
  auto parallel2Counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Parallel2Counter", std::nullopt, "RootDomain").value_or(0);
  };
  auto child2Counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Child2Counter", std::nullopt, "RootDomain").value_or(0);
  };

  auto child1Id = strenv.getStateId("Child1", "", "RootDomain").value_or(-1);
  auto child2Id = strenv.getStateId("Child2", "", "RootDomain").value_or(-1);
  auto parallelStateId = strenv.getStateId("ParallelState", "", "RootDomain").value_or(-1);
  auto finalStateId = strenv.getStateId("FinalState", "", "RootDomain").value_or(-1);

  auto toChild2TransId = strenv.getTransitionId("ToChild2", "", "RootDomain").value_or(-1);
  auto toParallelTransId = strenv.getTransitionId("ToParallel", "", "RootDomain").value_or(-1);
  auto toCompositeTransId = strenv.getTransitionId("ToComposite", "", "RootDomain").value_or(-1);
  auto toFinalTransId = strenv.getTransitionId("ToFinal", "", "RootDomain").value_or(-1);

  EXPECT_NE(child1Id, -1);
  EXPECT_NE(child2Id, -1);
  EXPECT_NE(parallelStateId, -1);
  EXPECT_NE(finalStateId, -1);
  EXPECT_NE(toChild2TransId, -1);
  EXPECT_NE(toParallelTransId, -1);
  EXPECT_NE(toCompositeTransId, -1);
  EXPECT_NE(toFinalTransId, -1);

  strenv.invoke("RootDomain_init", args);

  // 1. Initial state should be Child1 (inside CompositeState)
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), child1Id);

  // Verify default values
  EXPECT_EQ(counter(), 100);
  EXPECT_EQ(entryCounter(), 200);
  EXPECT_EQ(exitCounter(), 300);
  EXPECT_EQ(parallel1Counter(), 400);
  EXPECT_EQ(parallel2Counter(), 500);
  EXPECT_EQ(child2Counter(), 600);

  // Tick domain while Child1 is active: onEntryCode (1 >> EntryCounter) and
  // onProcessCode (10 >> Counter) run
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(entryCounter(), 1);
  EXPECT_EQ(counter(), 10);
  // Nothing else should change
  EXPECT_EQ(exitCounter(), 300);
  EXPECT_EQ(parallel1Counter(), 400);
  EXPECT_EQ(parallel2Counter(), 500);
  EXPECT_EQ(child2Counter(), 600);

  // 2. Transition from Child1 to Child2 within CompositeState
  strenv.requestTransition(statePtr, toChild2TransId, "", "RootDomain");
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), child2Id);
  EXPECT_EQ(exitCounter(), 1); // Child1 onExitCode fired
  // Old values should remain
  EXPECT_EQ(entryCounter(), 1);
  EXPECT_EQ(counter(), 10);
  EXPECT_EQ(parallel1Counter(), 400);
  EXPECT_EQ(parallel2Counter(), 500);
  EXPECT_EQ(child2Counter(), 600);

  // Tick domain while Child2 is active: Child2 onProcessCode runs (20 >>
  // Child2Counter)
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(child2Counter(), 20);
  // parallel counters should be untouched:
  EXPECT_EQ(parallel1Counter(), 400);
  EXPECT_EQ(parallel2Counter(), 500);

  // 3. Transition from Child2 to ParallelState (CompositeState saves history
  // state)
  strenv.requestTransition(statePtr, toParallelTransId, "", "RootDomain");
  strenv.invoke("RootDomain_process", args);

  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), parallelStateId);
  // Verify history state saved for CompositeState is child2Id
  EXPECT_EQ(strenv.getHistoryStateId(statePtr, "CompositeState", "", "RootDomain"), child2Id);

  // Tick domain while ParallelState is active: both parallel sub-states execute
  // their onProcessCode simultaneously
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(parallel1Counter(), 100);
  EXPECT_EQ(parallel2Counter(), 200);

  // 4. Transition back to CompositeState: should restore to Child2 (not default
  // Child1)
  strenv.requestTransition(statePtr, toCompositeTransId, "", "RootDomain");
  strenv.invoke("RootDomain_process", args);

  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), child2Id);

  // 5. Fire transition to FinalState
  strenv.requestTransition(statePtr, toFinalTransId, "", "RootDomain");
  strenv.invoke("RootDomain_process", args);

  EXPECT_EQ(strenv.getTransitionRequestId(statePtr, "", "RootDomain"), 0);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), finalStateId);
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
  void *args[] = {statePtr.get()};

  auto guardSignal = [&]() {
    return strenv.getStateVar(statePtr.get(), "GuardSignal", std::nullopt, "RootDomain").value_or(0);
  };
  auto counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Counter", std::nullopt, "RootDomain").value_or(0);
  };

  strenv.invoke("RootDomain_init", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 2); // State1 is 2
  EXPECT_EQ(guardSignal(), 0);
  EXPECT_EQ(counter(), 0);

  // Tick domain while GuardSignal == 0. Transition is guarded by GuardSignal
  // and should NOT fire!
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 2); // Still in State1
  EXPECT_EQ(counter(), 0);

  // Now set GuardSignal to 1 (true)
  strenv.setStateVar(statePtr.get(), "GuardSignal", 1, std::nullopt, "RootDomain");

  // Tick domain while GuardSignal == 1. Transition should fire!
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 3); // State2 is 3
  EXPECT_EQ(counter(), 1); // onTransition stream executed
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
  void *args[] = {statePtr.get()};

  auto counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Counter", std::nullopt, "RootDomain").value_or(0);
  };
  auto parentOrder = [&]() {
    return strenv.getStateVar(statePtr.get(), "ParentOrder", std::nullopt, "RootDomain").value_or(0);
  };
  auto childOrder = [&]() {
    return strenv.getStateVar(statePtr.get(), "ChildOrder", std::nullopt, "RootDomain").value_or(0);
  };

  strenv.invoke("RootDomain_init", args);
  EXPECT_EQ(counter(), 0);
  EXPECT_EQ(parentOrder(), 0);
  EXPECT_EQ(childOrder(), 0);

  // Tick domain. Parent updateGuard runs first (Counter=1 -> ParentOrder=1),
  // then Child updateGuard runs second (Counter=2 -> ChildOrder=2).
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(parentOrder(), 1);
  EXPECT_EQ(childOrder(), 2);
  EXPECT_EQ(counter(), 2);
}

TEST(StateMachine, UpdateGuardOnTransitionEntry) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_transition_guard_entry.stride");
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
  void *args[] = {statePtr.get()};

  auto inputVal = [&]() {
    return strenv.getStateVar(statePtr.get(), "InputVal", std::nullopt, "RootDomain").value_or(0);
  };
  auto targetGuard = [&]() {
    return strenv.getStateVar(statePtr.get(), "TargetGuard", std::nullopt, "RootDomain").value_or(0);
  };
  auto targetEntryRan = [&]() {
    return strenv.getStateVar(statePtr.get(), "TargetEntryRan", std::nullopt, "RootDomain").value_or(0);
  };

  strenv.invoke("RootDomain_init", args);
  EXPECT_EQ(inputVal(), 42);
  EXPECT_EQ(targetGuard(), 0);
  EXPECT_EQ(targetEntryRan(), 0);

  auto toState2Id = strenv.getTransitionId("ToState2", "", "RootDomain").value_or(-1);
  EXPECT_NE(toState2Id, -1);

  // Request transition to State2 and tick once.
  // When transitioning to State2, its onEntry and updateGuard blocks must execute
  // immediately upon transition entry, updating TargetGuard without needing another tick.
  strenv.requestTransition(statePtr, toState2Id, "", "RootDomain");
  strenv.invoke("RootDomain_process", args);

  auto state2Id = strenv.getStateId("State2", "", "RootDomain").value_or(-1);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), state2Id);
  EXPECT_EQ(targetEntryRan(), 1);
  EXPECT_EQ(targetGuard(), 42);
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
  void *args[] = {statePtr.get()};

  auto parentCounter = [&]() {
    return strenv.getStateVar(statePtr.get(), "ParentCounter", std::nullopt, "RootDomain").value_or(0);
  };
  auto region1Counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Region1Counter", std::nullopt, "RootDomain").value_or(0);
  };
  auto region2Counter = [&]() {
    return strenv.getStateVar(statePtr.get(), "Region2Counter", std::nullopt, "RootDomain").value_or(0);
  };

  strenv.invoke("RootDomain_init", args);
  EXPECT_EQ(parentCounter(), 0);
  EXPECT_EQ(region1Counter(), 0);
  EXPECT_EQ(region2Counter(), 0);

  // Tick 1: Parallel parent process runs (+1), and child region process streams
  // run (+10, +20)
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(parentCounter(), 1);
  EXPECT_EQ(region1Counter(), 10);
  EXPECT_EQ(region2Counter(), 20);

  // Tick 2: All process streams execute again concurrently
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(parentCounter(), 2);
  EXPECT_EQ(region1Counter(), 20);
  EXPECT_EQ(region2Counter(), 40);
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
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);
  // Initial state resolves to SubState1 (id 3)
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 3);
  EXPECT_EQ(strenv.getHistoryStateId(statePtr, "CompositeState", "", "RootDomain"), 0);

  // Transition to SubState2 (id 4)
  strenv.requestTransition(statePtr, 2, "", "RootDomain"); // ToSubState2 is transition 2
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 4); // SubState2

  // Transition to OtherState (id 5)
  strenv.requestTransition(statePtr, 1, "", "RootDomain"); // ToOtherState is transition 1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 5); // OtherState
  EXPECT_EQ(strenv.getHistoryStateId(statePtr, "CompositeState", "", "RootDomain"),
            4); // Saved SubState2 in history

  // Transition back to CompositeState (id 2). Because resumeLastState: on,
  // it restores active_state_id to 4 (SubState2) instead of defaulting to 3
  // (SubState1)
  strenv.requestTransition(statePtr, 3, "", "RootDomain"); // ToComposite is transition 3
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(strenv.getActiveStateId(statePtr, "", "RootDomain"), 4); // Restored SubState2!
}

TEST(StateMachine, DomainSwitches) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_switches.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strenv.prepareTree(tree);
  bool success = strenv.generateIr(tree);
  EXPECT_TRUE(success);
  success = strenv.compileInMemory();
  EXPECT_TRUE(success);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  void *args[] = {statePtr.get()};

  auto switchEntry = [&]() {
    return strenv.getStateVar(statePtr.get(), "SwitchEntry", std::nullopt, "RootDomain").value_or(0);
  };
  auto switchProcess = [&]() {
    return strenv.getStateVar(statePtr.get(), "SwitchProcess", std::nullopt, "RootDomain").value_or(0);
  };
  auto switchExit = [&]() {
    return strenv.getStateVar(statePtr.get(), "SwitchExit", std::nullopt, "RootDomain").value_or(0);
  };

  strenv.invoke("RootDomain_init", args);
  EXPECT_EQ(switchEntry(), 0);
  EXPECT_EQ(switchProcess(), 0);
  EXPECT_EQ(switchExit(), 0);

  // Tick the domain: should run onEntry and then onProcess of Child1
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(switchEntry(), 1);
  EXPECT_EQ(switchProcess(), 1);
  EXPECT_EQ(switchExit(), 0);

  // Request transition to FinalState
  strenv.requestTransition(statePtr, 1, "", "RootDomain");
  strenv.invoke("RootDomain_process", args);

  EXPECT_EQ(switchExit(), 1);
}
