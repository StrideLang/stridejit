#include "gtest/gtest.h"

// stridejit
#include "stride/stridejit/strideenvironment.hpp"

TEST(MultiInstanceFunctionTest, MultiInstanceStreamExpansionJIT) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "multi_instance_function.stride");
  ASSERT_TRUE(ret);
  ret = strenv.compileInMemory();
  ASSERT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  // --------------------------------------------------------------------------
  // Test Case 1: InA = [1, 1, 0], InB = [1, 0, 1], InScalar = 0
  //              InE1..3 = [1, 0, 1], InE4..6 = [1, 1, 0]
  // --------------------------------------------------------------------------
  {
    strenv.setStateVar(statePtr.get(), "InA", 1, 0, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InA", 1, 1, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InA", 0, 2, "RootDomain");

    strenv.setStateVar(statePtr.get(), "InB", 1, 0, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 0, 1, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InB", 1, 2, "RootDomain");

    strenv.setStateVar(statePtr.get(), "InScalar", 0, std::nullopt, "RootDomain");

    strenv.setStateVar(statePtr.get(), "InE1", 1, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InE2", 0, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InE3", 1, std::nullopt, "RootDomain");

    strenv.setStateVar(statePtr.get(), "InE4", 1, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InE5", 1, std::nullopt, "RootDomain");
    strenv.setStateVar(statePtr.get(), "InE6", 0, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    // 1. Bundle to Bundle: InA AND InB -> [1, 0, 0]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAndBundle", 0, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAndBundle", 1, "RootDomain").value_or(-1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAndBundle", 2, "RootDomain").value_or(-1), 0);

    // 2. Bundle with Scalar Broadcast (InScalar = 0): InA OR 0 -> [1, 1, 0]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 0, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 1, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 2, "RootDomain").value_or(-1), 0);

    // 3. Unary Bundle Function: NOT InA -> [0, 0, 1]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNotBundle", 0, "RootDomain").value_or(-1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNotBundle", 1, "RootDomain").value_or(-1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutNotBundle", 2, "RootDomain").value_or(-1), 1);

    // 4. List of Literals: [10, 20, 30] + [1, 2, 3] -> [11, 22, 33]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutLiteralAdd", 0, "RootDomain").value_or(-1), 11);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutLiteralAdd", 1, "RootDomain").value_or(-1), 22);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutLiteralAdd", 2, "RootDomain").value_or(-1), 33);

    // 5. List of Literals with Scalar Broadcast: [10, 20, 30] + 5 -> [15, 25, 35]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutLiteralBroadcast", 0, "RootDomain").value_or(-1), 15);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutLiteralBroadcast", 1, "RootDomain").value_or(-1), 25);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutLiteralBroadcast", 2, "RootDomain").value_or(-1), 35);

    // 6. List of Entities: InE1..3 AND InE4..6 -> [1, 0, 0]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutE1", std::nullopt, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutE2", std::nullopt, "RootDomain").value_or(-1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutE3", std::nullopt, "RootDomain").value_or(-1), 0);

    // 7. List of Entities with Scalar Broadcast (InScalar = 0): InE1..3 OR 0 -> [1, 0, 1]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar1", std::nullopt, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar2", std::nullopt, "RootDomain").value_or(-1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar3", std::nullopt, "RootDomain").value_or(-1), 1);
  }

  // --------------------------------------------------------------------------
  // Test Case 2: InScalar = 1 (Scalar broadcast test)
  // --------------------------------------------------------------------------
  {
    strenv.setStateVar(statePtr.get(), "InScalar", 1, std::nullopt, "RootDomain");

    strenv.invoke("RootDomain_process", args);

    // 2. Bundle with Scalar Broadcast (InScalar = 1): InA OR 1 -> [1, 1, 1]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 0, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 1, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 2, "RootDomain").value_or(-1), 1);

    // 7. List of Entities with Scalar Broadcast (InScalar = 1): InE1..3 OR 1 -> [1, 1, 1]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar1", std::nullopt, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar2", std::nullopt, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar3", std::nullopt, "RootDomain").value_or(-1), 1);
  }
}
