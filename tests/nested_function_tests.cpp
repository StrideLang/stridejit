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

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reaction_in_reaction.stride");
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
