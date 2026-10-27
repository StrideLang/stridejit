#include "gtest/gtest.h"
#include <cmath>

#include "stride/stridejit/strideenvironment.hpp"

namespace {

TEST(JITDomainTest, PassThru) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 1, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);
  double out = strenv
                   .getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                         "TestDomain")
                   .value_or(0);

  EXPECT_FLOAT_EQ(out, 1.0);
}

TEST(JITDomainTest, DomainVariable) {
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_variable.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 6, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);

  auto A = strenv.getStateVar<int32_t>(statePtr.get(), "A", std::nullopt,
                                       "TestDomain");
  EXPECT_TRUE(A.has_value());
  EXPECT_EQ(*A, 6);
}

TEST(JITDomainTest, BlockDefaults) {
  // Test defaults for signal declaration of domain variable
  strd::StrideEnvironment strenv;

  auto ret =
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "block_default.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("TestDomain_init", args);
  strenv.invoke("TestDomain_process", args);

  auto out = strenv.getStateVar<int32_t>(statePtr.get(), "Out", std::nullopt,
                                         "TestDomain");
  EXPECT_TRUE(out.has_value());
  EXPECT_EQ(out.value_or(0), 6);
}

TEST(JITDomainTest, DomainInit) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domain_init.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);
  auto In = strenv.getStateVar<int32_t>(statePtr.get(), "In", std::nullopt,
                                        "RootDomain");
  auto In2 = strenv.getStateVar<int32_t>(statePtr.get(), "In2", std::nullopt,
                                         "RootDomain");
  EXPECT_EQ(In.value_or(0), 3);
  EXPECT_EQ(In2.value_or(0), 10);

  strenv.invoke("RootDomain_process", args);
  In = strenv.getStateVar<int32_t>(statePtr.get(), "In", std::nullopt,
                                   "RootDomain");
  In2 = strenv.getStateVar<int32_t>(statePtr.get(), "In2", std::nullopt,
                                    "RootDomain");
  EXPECT_EQ(In.value_or(0), 5);
  EXPECT_EQ(In2.value_or(0), 11);

  strenv.invoke("RootDomain_init", args);
  In = strenv.getStateVar<int32_t>(statePtr.get(), "In", std::nullopt,
                                   "RootDomain");
  In2 = strenv.getStateVar<int32_t>(statePtr.get(), "In2", std::nullopt,
                                    "RootDomain");
  EXPECT_EQ(In.value_or(0), 3);
  EXPECT_EQ(In2.value_or(0), 10);
}

TEST(JITDomainTest, DomainFunctions) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "module.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  EXPECT_EQ(strenv.mStrideEnv.domainArgs.size(), 1);
  EXPECT_EQ(strenv.mStrideEnv.domainArgs["RootDomain"].size(), 1);
  EXPECT_EQ(strenv.mStrideEnv.domainArgs["RootDomain"][0].name, "Out");
  EXPECT_EQ(strenv.mStrideEnv.domainArgs["RootDomain"][0].type,
            strd::DataType::DOUBLE);
}

TEST(JITDomainTest, MultipleDomains) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "domains.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.setStateVar(statePtr.get(), "In", 1.0, std::nullopt, "TestDomain");
  strenv.invoke("TestDomain_process", args);
  auto out = strenv
                 .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                      "TestDomain")
                 .value_or(0.0);
  EXPECT_EQ(out, 1);
}

TEST(JITDomainTest, SignalReset) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "reset.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto statePtr = strenv.allocateSharedState("RootDomain");
  ASSERT_NE(statePtr.get(), nullptr);
  void *args[] = {statePtr.get()};

  strenv.invoke("RootDomain_init", args);

  strenv.invoke("RootDomain_process", args);
  auto In =
      strenv
          .getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(In, 5.0);

  strenv.invoke("RootDomain_process", args);
  In =
      strenv
          .getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(In, 7.0);

  strenv.invoke("RootDomain_process", args);
  In =
      strenv
          .getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(In, 9.0);

  strenv.invoke("RootDomain_process", args);
  In =
      strenv
          .getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(In, 11.0);

  strenv.invoke("RootDomain_process", args);
  In =
      strenv
          .getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(In, 13.0);

  strenv.invoke("RootDomain_process", args);
  In =
      strenv
          .getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(In, 5.0);

  strenv.invoke("RootDomain_process", args);
  In =
      strenv
          .getStateVar<double>(statePtr.get(), "In", std::nullopt, "RootDomain")
          .value_or(0.0);
  EXPECT_EQ(In, 7.0);
}

TEST(JITDomainTest, DomainIO) {
  strd::StrideEnvironment strenv;

  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "io.stride");
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
  EXPECT_EQ(out, 1.0);

  strenv.setStateVar(statePtr.get(), "In", 3.14159, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_FLOAT_EQ(out, cos(3.14159));

  strenv.setStateVar(statePtr.get(), "In", 1.0, std::nullopt, "RootDomain");
  strenv.invoke("RootDomain_process", args);
  out = strenv
            .getStateVar<double>(statePtr.get(), "Out", std::nullopt,
                                 "RootDomain")
            .value_or(0.0);
  EXPECT_FLOAT_EQ(out, cos(1.0));
}

} // namespace
