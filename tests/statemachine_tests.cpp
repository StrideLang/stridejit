#include "gtest/gtest.h"

// stridejit
#include "stride/codegen/coderesolver.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// stride
#include "stride/codegen/codeanalysis.hpp"
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

  strenv.generateIr(tree);

  bool foundVar = false;
  auto domainDecl = strd::ASTQuery::findDeclarationByName("RootDomain", scope, tree);
  if (domainDecl && strenv.state.dynamicDomainFields.find(domainDecl) != strenv.state.dynamicDomainFields.end()) {
      for (auto& f : strenv.state.dynamicDomainFields[domainDecl]) {
          if (f.name == "__RootDomain_MyStateMachine_active_state_id") foundVar = true;
      }
  }
  EXPECT_TRUE(foundVar);
}

TEST(StateMachine, Flattening) {
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

  struct RootDomainState {
      int32_t active_state_id;
      int32_t transition_request_id;
  };
  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  RootDomainState* ds = reinterpret_cast<RootDomainState*>(statePtr.get());
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_init", args);

  EXPECT_EQ(ds->active_state_id, 2);

  auto *counter = strenv.getGlobal<int32_t>("Counter");
  ASSERT_NE(counter, nullptr);
  EXPECT_EQ(*counter, 0);

  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*counter, 1);

  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(*counter, 2);
}

TEST(StateMachine, TransitionLoading) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR
                              "statemachines_transitions.stride");
  EXPECT_NE(tree, nullptr);

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

  struct RootDomainState {
      int32_t active_state_id;
      int32_t transition_request_id;
  };
  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr, nullptr);
  RootDomainState* ds = reinterpret_cast<RootDomainState*>(statePtr.get());
  void *args[] = {statePtr.get()};
  
  auto *counter = strenv.getGlobal<int32_t>("Counter");
  ASSERT_NE(counter, nullptr);

  strenv.invoke("RootDomain_init", args);

  EXPECT_EQ(ds->active_state_id, 2); // State1 is 2
  EXPECT_EQ(*counter, 0);

  // Tick the domain without a request, nothing should happen
  strenv.invoke("RootDomain_process", args);
  EXPECT_EQ(ds->active_state_id, 2);
  EXPECT_EQ(*counter, 0);

  // Request transition 1 (ToState2)
  ds->transition_request_id = 1;
  strenv.invoke("RootDomain_process", args);

  // activeState should be 3 (State2)
  EXPECT_EQ(ds->active_state_id, 3);
  EXPECT_EQ(*counter, 1);
  EXPECT_EQ(ds->transition_request_id, 0);

  // Request transition 2 (ToState3)
  ds->transition_request_id = 2;
  strenv.invoke("RootDomain_process", args);

  EXPECT_EQ(ds->active_state_id, 4); // State3
  EXPECT_EQ(*counter, 2);
}
