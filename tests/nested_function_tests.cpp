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
