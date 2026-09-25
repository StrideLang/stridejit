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
