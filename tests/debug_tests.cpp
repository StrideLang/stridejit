#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <iostream>

#include "stride/parser/ast.h"
#include "stride/stridejit/stridecompiler.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/stridejit/stridedebugemitter.hpp"
#include "stride/testing/functiontestrunner.hpp"
#include "stride/utils/astfunctions.h"

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IntrinsicInst.h"

namespace {

TEST(DebugTest, CompileUnitAndSubprogramMetadata) {
  strd::StrideEnvironment env;
  env.enableDebugInfo(true);
  env.setDebugInfoLevel(strd::DebugInfoLevel::Full);

  bool ok = env.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_variable.stride");
  ASSERT_TRUE(ok);

  llvm::Module *module = env.mStrideEnv.TheModule.get();
  ASSERT_NE(module, nullptr);

  // 1. Verify DICompileUnit exists
  auto *cu = module->getNamedMetadata("llvm.dbg.cu");
  ASSERT_NE(cu, nullptr);
  EXPECT_GT(cu->getNumOperands(), 0u);

  // 2. Verify subprogram on domain process function
  llvm::Function *procFn = module->getFunction("RootDomain_process");
  ASSERT_NE(procFn, nullptr);
  llvm::DISubprogram *sp = procFn->getSubprogram();
  ASSERT_NE(sp, nullptr);
  EXPECT_NE(sp->getName().str().find("RootDomain::process"), std::string::npos);

  // 3. Verify subprogram on domain init function
  llvm::Function *initFn = module->getFunction("RootDomain_init");
  ASSERT_NE(initFn, nullptr);
  llvm::DISubprogram *initSp = initFn->getSubprogram();
  ASSERT_NE(initSp, nullptr);
  EXPECT_NE(initSp->getName().str().find("RootDomain::init"), std::string::npos);
}

TEST(DebugTest, InstructionDebugLocationAndLineColumn) {
  strd::StrideEnvironment env;
  env.enableDebugInfo(true);
  env.setDebugInfoLevel(strd::DebugInfoLevel::Full);

  bool ok = env.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_variable.stride");
  ASSERT_TRUE(ok);

  llvm::Module *module = env.mStrideEnv.TheModule.get();
  ASSERT_NE(module, nullptr);

  // The streams of domain_variable.stride run in TestDomain
  llvm::Function *procFn = module->getFunction("TestDomain_process");
  ASSERT_NE(procFn, nullptr);

  bool foundValidDILocation = false;
  for (auto &bb : *procFn) {
    for (auto &instr : bb) {
      const llvm::DebugLoc &loc = instr.getDebugLoc();
      if (loc && loc.getLine() > 0) {
        foundValidDILocation = true;
        // Verify line and column are positive
        EXPECT_GT(loc.getLine(), 0u);
        EXPECT_GE(loc.getCol(), 0u);
        break;
      }
    }
    if (foundValidDILocation) break;
  }
  EXPECT_TRUE(foundValidDILocation);
}

TEST(DebugTest, DebugInfoDisabledDoesNotEmitCompileUnit) {
  strd::StrideEnvironment env;
  env.enableDebugInfo(false);
  env.setDebugInfoLevel(strd::DebugInfoLevel::None);

  bool ok = env.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_variable.stride");
  ASSERT_TRUE(ok);

  llvm::Module *module = env.mStrideEnv.TheModule.get();
  ASSERT_NE(module, nullptr);

  auto *cu = module->getNamedMetadata("llvm.dbg.cu");
  EXPECT_EQ(cu, nullptr);

  llvm::Function *procFn = module->getFunction("RootDomain_process");
  ASSERT_NE(procFn, nullptr);
  EXPECT_EQ(procFn->getSubprogram(), nullptr);
}

TEST(DebugTest, DomainStepperStepTick) {
  strd::StrideEnvironment env;
  bool ok = env.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_variable.stride");
  ASSERT_TRUE(ok);

  ok = env.compileInMemory();
  ASSERT_TRUE(ok);

  void *statePtr = env.allocateState("TestDomain");
  ASSERT_NE(statePtr, nullptr);

  // Execute single tick through stepper API
  int32_t ret = env.stepTick("TestDomain", statePtr);
  EXPECT_GE(ret, 0);

  // Execute a second tick
  ret = env.stepTick("TestDomain", statePtr);
  EXPECT_GE(ret, 0);

  env.deallocateState(statePtr);
}

TEST(DebugTest, BuildTypeAndOptimizationLevelMapping) {
  strd::StrideEnvironment env;

  // Debug preset: -O0 and full debug
  env.setBuildType(strd::BuildType::Debug);
  EXPECT_TRUE(env.isDebugInfoEnabled());
  EXPECT_EQ(env.getDebugInfoLevel(), strd::DebugInfoLevel::Full);
  EXPECT_EQ(env.getOptimizationLevel(), 0);

  // Release preset: -O3 and no debug
  env.setBuildType(strd::BuildType::Release);
  EXPECT_FALSE(env.isDebugInfoEnabled());
  EXPECT_EQ(env.getDebugInfoLevel(), strd::DebugInfoLevel::None);
  EXPECT_EQ(env.getOptimizationLevel(), 3);

  // RelWithDebInfo preset: -O2 and line tables
  env.setBuildType(strd::BuildType::RelWithDebInfo);
  EXPECT_TRUE(env.isDebugInfoEnabled());
  EXPECT_EQ(env.getDebugInfoLevel(), strd::DebugInfoLevel::LineTablesOnly);
  EXPECT_EQ(env.getOptimizationLevel(), 2);
}

} // namespace
