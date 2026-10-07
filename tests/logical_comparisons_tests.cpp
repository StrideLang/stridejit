#include "gtest/gtest.h"

// stridejit
#include "stride/stridejit/strideenvironment.hpp"

TEST(LogicalComparisonsTest, LogicalOperationsJIT) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "logical_comparisons.stride");
  ASSERT_TRUE(ret);
  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  // Test case 1: InA = true, InB = false
  {
    strenv.setStateVar(statePtr.get(), "InA", 1, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 0, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAnd", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOr", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNot", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutXor", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNand", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNor", std::nullopt, "RootDomain").value_or(1), 0);
  }

  // Test case 2: InA = true, InB = true
  {
    strenv.setStateVar(statePtr.get(), "InA", 1, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 1, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAnd", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOr", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNot", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutXor", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNand", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNor", std::nullopt, "RootDomain").value_or(1), 0);
  }

  // Test case 3: InA = false, InB = false
  {
    strenv.setStateVar(statePtr.get(), "InA", 0, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 0, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAnd", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOr", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNot", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutXor", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNand", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNor", std::nullopt, "RootDomain").value_or(0), 1);
  }

  // Test case 4: InA = false, InB = true
  {
    strenv.setStateVar(statePtr.get(), "InA", 0, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 1, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAnd", std::nullopt, "RootDomain").value_or(1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOr", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNot", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutXor", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNand", std::nullopt, "RootDomain").value_or(0), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNor", std::nullopt, "RootDomain").value_or(1), 0);
  }
}
