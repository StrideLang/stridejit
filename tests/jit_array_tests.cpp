#include "gtest/gtest.h"
#include <cmath>

#include "stride/stridejit/strideenvironment.hpp"

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"

namespace {

TEST(JITArrayTest, BasicArrays) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "bundles.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.3, 1, "RootDomain");
  strenv.setStateVar(statePtr.get(), "In", 0.4, 2, "RootDomain");

  strenv.invoke("RootDomain_process", args);

  auto In1 = strenv.getStateVar<double>(statePtr.get(), "In", 1, "RootDomain")
                 .value_or(0.0);
  auto In2 = strenv.getStateVar<double>(statePtr.get(), "In", 2, "RootDomain")
                 .value_or(0.0);
  auto In3 = strenv.getStateVar<double>(statePtr.get(), "In", 3, "RootDomain")
                 .value_or(0.0);

  auto Out2 = strenv.getStateVar<double>(statePtr.get(), "Out", 2, "RootDomain")
                  .value_or(0.0);
  auto Out3 = strenv.getStateVar<double>(statePtr.get(), "Out", 3, "RootDomain")
                  .value_or(0.0);
  auto Out4 = strenv.getStateVar<double>(statePtr.get(), "Out", 4, "RootDomain")
                  .value_or(0.0);

  // Stride code: In[1] >> Out[2];
  EXPECT_DOUBLE_EQ(In1, Out2);
  // Stride code: In[2] + 1.2 >> Out[3];
  EXPECT_DOUBLE_EQ(Out3, 0.4 + 1.2);
  // Stride code : In[3] >> Cos() >> Out[4];
  EXPECT_DOUBLE_EQ(Out4, cos(In3));
}

TEST(JITArrayTest, DynamicVariableIndexing) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "bundle_variable.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 7.7, 1, "RootDomain");
  strenv.setStateVar(statePtr.get(), "In", 4.2, 2, "RootDomain");
  strenv.setStateVar(statePtr.get(), "Index", 2, std::nullopt, "RootDomain");

  strenv.invoke("RootDomain_process", args);

  auto Out2 = strenv.getStateVar<double>(statePtr.get(), "Out", 2, "RootDomain")
                  .value_or(0.0);
  auto Out3 = strenv.getStateVar<double>(statePtr.get(), "Out", 3, "RootDomain")
                  .value_or(0.0);
  auto Out4 = strenv.getStateVar<double>(statePtr.get(), "Out", 4, "RootDomain")
                  .value_or(0.0);

  // Stride code: In[Index] >> Out[3];
  EXPECT_DOUBLE_EQ(Out3, 4.2);
  // Stride code: In[Index] + 1.5 >> Out[4];
  EXPECT_DOUBLE_EQ(Out4, 4.2 + 1.5);
  // Stride code: In[1] >> Out[Index];
  EXPECT_DOUBLE_EQ(Out2, 7.7);

  // Test with another value for Index
  strenv.setStateVar(statePtr.get(), "Index", 5, std::nullopt, "RootDomain");
  strenv.setStateVar(statePtr.get(), "In", 9.1, 5, "RootDomain");
  for (int i = 0; i < 16; ++i) {
    strenv.setStateVar(statePtr.get(), "Out", 0.0, i, "RootDomain");
  }

  strenv.invoke("RootDomain_process", args);

  Out3 = strenv.getStateVar<double>(statePtr.get(), "Out", 3, "RootDomain")
             .value_or(0.0);
  Out4 = strenv.getStateVar<double>(statePtr.get(), "Out", 4, "RootDomain")
             .value_or(0.0);
  auto Out5 = strenv.getStateVar<double>(statePtr.get(), "Out", 5, "RootDomain")
                  .value_or(0.0);

  // Stride code: In[Index] >> Out[3];
  EXPECT_DOUBLE_EQ(Out3, 9.1);
  // Stride code: In[Index] + 1.5 >> Out[4];
  EXPECT_DOUBLE_EQ(Out4, 9.1 + 1.5);
  // Stride code: In[1] >> Out[Index];
  EXPECT_DOUBLE_EQ(Out5, 7.7);
}

TEST(JITArrayTest, ArrayDefaults) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "bundle_default.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  auto A0 = strenv.getStateVar<double>(statePtr.get(), "A", 0, "RootDomain")
                .value_or(0.0);
  auto A1 = strenv.getStateVar<double>(statePtr.get(), "A", 1, "RootDomain")
                .value_or(0.0);

  EXPECT_EQ(A0, 5);
  EXPECT_EQ(A1, 2);

  strenv.invoke("RootDomain_process", args);

  auto out0 = strenv.getStateVar<double>(statePtr.get(), "Out", 0, "RootDomain")
                  .value_or(0.0);
  auto out1 = strenv.getStateVar<double>(statePtr.get(), "Out", 1, "RootDomain")
                  .value_or(0.0);
  EXPECT_EQ(out0, 10);
  EXPECT_EQ(out1, 13);
}

TEST(JITArrayTest, ListEvaluation) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "listinput.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 0.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  auto out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "RootDomain")
                 .value_or(0.0);
  EXPECT_EQ(out, 0.0);

  strenv.setStateVar(statePtr.get(), "In", 10.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_EQ(out, 1.0);
}

TEST(JITArrayTest, ConstantArray) {
  strd::StrideEnvironment strenv;
  ASSERT_TRUE(
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "constant_array.stride"));

  llvm::GlobalVariable *table = nullptr;
  for (auto &global : strenv.mStrideEnv.TheModule->globals()) {
    if (global.getName().ends_with("Table")) {
      table = &global;
    }
  }
  ASSERT_NE(table, nullptr);
  EXPECT_TRUE(table->isConstant());
  EXPECT_TRUE(table->hasInternalLinkage());
  auto *values =
      llvm::dyn_cast<llvm::ConstantDataArray>(table->getInitializer());
  ASSERT_NE(values, nullptr);
  ASSERT_EQ(values->getNumElements(), 6u);
  EXPECT_EQ(values->getElementAsInteger(0), 10u);
  EXPECT_EQ(values->getElementAsInteger(4), 14u);
  EXPECT_EQ(values->getElementAsInteger(5), 99u);

  ASSERT_TRUE(strenv.compileInMemory());
  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};
  strenv.invoke("RootDomain_process", args);

  EXPECT_EQ(strenv
                .getStateVar<int32_t>(statePtr.get(), "Sum", std::nullopt,
                                      "RootDomain")
                .value_or(0),
            10 + 11 + 12 + 13 + 14 + 99);
  EXPECT_EQ(strenv
                .getStateVar<int32_t>(statePtr.get(), "Third", std::nullopt,
                                      "RootDomain")
                .value_or(0),
            12);
}

TEST(JITArrayTest, PortPropertySize) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "port_property_size.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_process", args);
  auto Out0 = strenv.getStateVar<double>(statePtr.get(), "Out", 0, "RootDomain")
                  .value_or(0.0);
  auto Out1 = strenv.getStateVar<double>(statePtr.get(), "Out", 1, "RootDomain")
                  .value_or(0.0);

  EXPECT_EQ(Out0, 32);
  EXPECT_EQ(Out1, 8);
}

TEST(JITArrayTest, MultiInstanceStreamExpansion) {
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

    // 1. Array to Array: InA AND InB -> [1, 0, 0]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAndBundle", 0, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAndBundle", 1, "RootDomain").value_or(-1), 0);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutAndBundle", 2, "RootDomain").value_or(-1), 0);

    // 2. Array with Scalar Broadcast (InScalar = 0): InA OR 0 -> [1, 1, 0]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 0, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 1, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 2, "RootDomain").value_or(-1), 0);

    // 3. Unary Array Function: NOT InA -> [0, 0, 1]
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

    // 2. Array with Scalar Broadcast (InScalar = 1): InA OR 1 -> [1, 1, 1]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 0, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 1, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutOrScalarBroadcast", 2, "RootDomain").value_or(-1), 1);

    // 7. List of Entities with Scalar Broadcast (InScalar = 1): InE1..3 OR 1 -> [1, 1, 1]
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar1", std::nullopt, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar2", std::nullopt, "RootDomain").value_or(-1), 1);
    EXPECT_EQ(strenv.getStateVar(statePtr.get(), "OutEScalar3", std::nullopt, "RootDomain").value_or(-1), 1);
  }
}

} // namespace
