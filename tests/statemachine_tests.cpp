#include "gtest/gtest.h"

// stridejit
#include "stride/codegen/coderesolver.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridegenerator.hpp"

// stride
#include "stride/codegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"

// llvm
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

TEST(StateMachine, DomainInitialization) {
  strd::ASTNode tree;
  tree = strd::AST::parseFile(STRIDEJIT_TESTS_SOURCE_DIR "statemachines.stride");
  EXPECT_NE(tree, nullptr);

  strd::StrideEnvironment strenv;
  strd::ScopeStack scope;

  strenv.generateIr(tree);

  // We should verify that `__MyDomain_MyStateMachine_active_state_id` global exists
  // and is initialized correctly.
  bool foundVar = strenv.state.globalExists("__RootDomain_MyStateMachine_active_state_id");
  EXPECT_TRUE(foundVar);
}
