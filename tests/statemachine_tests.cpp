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

  // We should verify that `__MyDomain_MyStateMachine_active_state_id` global
  // exists and is initialized correctly.
  bool foundVar =
      strenv.state.globalExists("__RootDomain_MyStateMachine_active_state_id");
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

  // The domain should have its _init and _process invokers compiled and exposed to the JIT.
  // Before the fix, these functions were compiled to IR but never exposed with invokers.
  auto initSym = strenv.getFunction("RootDomain_init_invoker");
  EXPECT_TRUE(static_cast<bool>(initSym)) << "RootDomain_init_invoker not found in JIT";
  if (!initSym) llvm::consumeError(initSym.takeError());

  auto processSym = strenv.getFunction("RootDomain_process_invoker");
  EXPECT_TRUE(static_cast<bool>(processSym)) << "RootDomain_process_invoker not found in JIT";
  if (!processSym) llvm::consumeError(processSym.takeError());
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
  
  // We expect statemachines_streams.stride to have a counter that increments 
  // on every tick in the active state's onProcess stream.
  
  // Initialize the domain (this should reset activeState to State1's ID which is 2)
  strenv.invoke("RootDomain_init", nullptr);

  auto *activeState = strenv.getGlobal<int32_t>("__RootDomain_MyStateMachine_active_state_id");
  ASSERT_NE(activeState, nullptr);
  EXPECT_EQ(*activeState, 2);
  
  // Counter should start at 0
  auto *counter = strenv.getGlobal<int32_t>("Counter");
  ASSERT_NE(counter, nullptr);
  EXPECT_EQ(*counter, 0);

  // Tick the domain multiple times, since MyStateMachine is the active state 
  // and has an onProcess block that does `Counter = Counter + 1`, it should tick.
  strenv.invoke("RootDomain_process", nullptr);
  EXPECT_EQ(*counter, 1);
  
  strenv.invoke("RootDomain_process", nullptr);
  EXPECT_EQ(*counter, 2);
}
