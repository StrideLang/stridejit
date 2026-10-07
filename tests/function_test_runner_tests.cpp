#include "gtest/gtest.h"

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/valuenode.h"

#include "stride/testing/functiontestrunner.hpp"
#include "stride/testing/memorytracker.hpp"
#include "stride/testing/perfanalyzer.hpp"
#include "stride/testing/specextractor.hpp"
#include "stride/testing/testreporter.hpp"
#include "stride/testing/testspec.hpp"

#include "stride/stridejit/strideenvironment.hpp"

namespace {

using namespace strd::test;

// Mock C function matching Stride domain calling convention: void(*)(double*
// in, double* out)
void mock_passthru_process(double *in, double *out) {
  if (in && out) {
    *out = *in;
  }
}

TEST(StrideTestSpec, SignalVectorMatching) {
  SignalStreamVector vec;
  vec.portName = "Out";
  vec.values = {1.0, 2.0, 3.0};
  vec.epsilon = 1e-4;

  EXPECT_TRUE(vec.matches(1.00001, 0));
  EXPECT_TRUE(vec.matches(2.0, 1));
  EXPECT_FALSE(vec.matches(3.1, 2));
  EXPECT_FALSE(vec.matches(1.0, 5)); // out of bounds
}

TEST(StrideTestSpec, AllAssertionKinds) {
  // ExpectNear
  SignalStreamVector nearVec;
  nearVec.kind = AssertionKind::Near;
  nearVec.epsilon = 0.05;
  nearVec.values = {10.0};
  EXPECT_TRUE(nearVec.matches(10.04, 0));
  EXPECT_FALSE(nearVec.matches(10.06, 0));

  // ExpectTrue / ExpectFalse
  SignalStreamVector trueVec;
  trueVec.kind = AssertionKind::True;
  EXPECT_TRUE(trueVec.matches(true, 0));
  EXPECT_TRUE(trueVec.matches(1.0, 0));
  EXPECT_FALSE(trueVec.matches(false, 0));
  EXPECT_FALSE(trueVec.matches(0.0, 0));

  SignalStreamVector falseVec;
  falseVec.kind = AssertionKind::False;
  EXPECT_TRUE(falseVec.matches(false, 0));
  EXPECT_TRUE(falseVec.matches(0.0, 0));
  EXPECT_FALSE(falseVec.matches(true, 0));

  // ExpectGt / ExpectGe
  SignalStreamVector gtVec;
  gtVec.kind = AssertionKind::GreaterThan;
  gtVec.values = {5.0};
  EXPECT_TRUE(gtVec.matches(5.1, 0));
  EXPECT_FALSE(gtVec.matches(5.0, 0));
  EXPECT_FALSE(gtVec.matches(4.9, 0));

  SignalStreamVector geVec;
  geVec.kind = AssertionKind::GreaterThanOrEqual;
  geVec.values = {5.0};
  EXPECT_TRUE(geVec.matches(5.0, 0));
  EXPECT_TRUE(geVec.matches(5.1, 0));
  EXPECT_FALSE(geVec.matches(4.9, 0));

  // ExpectLt / ExpectLe
  SignalStreamVector ltVec;
  ltVec.kind = AssertionKind::LessThan;
  ltVec.values = {5.0};
  EXPECT_TRUE(ltVec.matches(4.9, 0));
  EXPECT_FALSE(ltVec.matches(5.0, 0));

  SignalStreamVector leVec;
  leVec.kind = AssertionKind::LessThanOrEqual;
  leVec.values = {5.0};
  EXPECT_TRUE(leVec.matches(5.0, 0));
  EXPECT_TRUE(leVec.matches(4.9, 0));
  EXPECT_FALSE(leVec.matches(5.1, 0));
}

TEST(StrideTestPerf, AnalyzerStats) {
  auto stats = PerfAnalyzer::profile(10, 100, []() {
    volatile int x = 0;
    for (int i = 0; i < 50; ++i)
      x += i;
  });

  EXPECT_GT(stats.meanNs, 0.0);
  EXPECT_GE(stats.maxNs, stats.minNs);
  EXPECT_EQ(stats.iterations, 100);
  stats.recordToGTest("BenchmarkTest");
}

TEST(StrideTestMemory, NoHeapAllocGuard) {
  NoHeapAllocGuard guard;
  EXPECT_FALSE(guard.hasAllocated());
  EXPECT_EQ(guard.getDeltaAllocations(), 0);
}

TEST(StrideTestSpec, SpecExtractorSingle) {
  auto testNode = std::make_shared<strd::DeclarationNode>(
      "ImpulseResponse", "functionTest", nullptr, __FILE__, __LINE__);
  auto funcVal =
      std::make_shared<strd::ValueNode>("FirFilter", __FILE__, __LINE__);
  testNode->setPropertyValue("target", funcVal);

  auto spec = SpecExtractor::extractSingleTest(testNode);
  EXPECT_EQ(spec.testName, "ImpulseResponse");
  EXPECT_EQ(spec.functionName, "FirFilter");
}

TEST(StrideTestSpec, ExtractFromFile) {
  auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR
                                                   "PassthruTest.stride");
  ASSERT_EQ(specs.size(), 1);
  EXPECT_EQ(specs[0].testName, "PassthruTest");
  EXPECT_EQ(specs[0].functionName, "Passthru");
  EXPECT_EQ(specs[0].tickCount, 5);
  ASSERT_EQ(specs[0].prepareStreams.size(), 1);
  EXPECT_EQ(specs[0].prepareStreams[0].portName, "In");
  EXPECT_EQ(specs[0].prepareStreams[0].values.size(), 5);
  ASSERT_EQ(specs[0].validateStreams.size(), 1);
  EXPECT_EQ(specs[0].validateStreams[0].portName, "Out");
  EXPECT_EQ(specs[0].validateStreams[0].values.size(), 5);
  EXPECT_EQ(specs[0].validateStreams[0].kind, AssertionKind::Equal);
}

TEST(StrideTestSpec, ExtractAssertionsFixture) {
  auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR
                                                   "AssertionsTest.stride");
  ASSERT_EQ(specs.size(), 2);

  // TestNear
  EXPECT_EQ(specs[0].testName, "TestNear");
  EXPECT_EQ(specs[0].functionName, "TestDomain");
  ASSERT_EQ(specs[0].validateStreams.size(), 1);
  EXPECT_EQ(specs[0].validateStreams[0].kind, AssertionKind::Near);
  EXPECT_DOUBLE_EQ(specs[0].validateStreams[0].epsilon, 0.01);

  // TestGtLt
  EXPECT_EQ(specs[1].testName, "TestGtLt");
  ASSERT_EQ(specs[1].validateStreams.size(), 2);
  EXPECT_EQ(specs[1].validateStreams[0].kind, AssertionKind::GreaterThan);
  EXPECT_EQ(specs[1].validateStreams[1].kind, AssertionKind::LessThan);
}

TEST(StrideTestRunner, DirectFunctionExecutionSuccess) {
  auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR
                                                   "PassthruTest.stride");
  ASSERT_EQ(specs.size(), 1);

  FunctionTestRunner runner(reinterpret_cast<void *>(&mock_passthru_process));
  ASSERT_TRUE(runner.isLoaded());

  auto result = runner.runTest(specs[0]);
  EXPECT_TRUE(result.passed) << result.errorMessage;
  EXPECT_EQ(result.ticksExecuted, 5);
  EXPECT_EQ(result.heapAllocations, 0);
  EXPECT_GT(result.perfMetrics.meanNs, 0.0);
}

TEST(StrideTestRunner, FailureDiagnostics) {
  auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR
                                                   "PassthruTest.stride");
  ASSERT_EQ(specs.size(), 1);

  // Modify expected value at tick 2 to intentionally trigger a failure
  specs[0].validateStreams[0].values[2] = 99.0;

  FunctionTestRunner runner(reinterpret_cast<void *>(&mock_passthru_process));
  auto result = runner.runTest(specs[0]);

  EXPECT_FALSE(result.passed);
  EXPECT_EQ(result.ticksExecuted, 2);
  EXPECT_NE(
      result.errorMessage.find("Assertion failed on port 'Out' at tick 2"),
      std::string::npos);
}

TEST(StrideTestRunner, JITCompiledPassthruExecution) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "passthru.stride");
  ASSERT_TRUE(ret);

  strenv.initializeJIT();
  auto compiled = strenv.compileInMemory();
  ASSERT_TRUE(compiled);

  auto entrySym = strenv.getFunction("TestDomain_process");
  ASSERT_TRUE(entrySym.operator bool());

  void *fnPtr = reinterpret_cast<void *>(entrySym->getValue());
  ASSERT_NE(fnPtr, nullptr);

  auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR
                                                   "PassthruTest.stride");
  ASSERT_EQ(specs.size(), 1);

  FunctionTestRunner runner(fnPtr);
  std::shared_ptr<void> state = strenv.allocateSharedState("TestDomain");
  ASSERT_NE(state, nullptr);
  runner.setStatePointer(state.get());
  runner.setStateAccessor(
      {[&strenv](void *statePtr, const std::string &name, double val) {
         strenv.setStateVar<double>(statePtr, name, val, std::nullopt, "TestDomain");
       },
       [&strenv](const void *statePtr, const std::string &name) {
         return strenv.getStateVar<double>(statePtr, name, std::nullopt, "TestDomain").value_or(0.0);
       }});

  auto result = runner.runTest(specs[0]);

  EXPECT_TRUE(result.passed) << result.errorMessage;
  EXPECT_EQ(result.ticksExecuted, 5);
  EXPECT_EQ(result.heapAllocations, 0);
  EXPECT_GT(result.perfMetrics.meanNs, 0.0);
}

TEST(StrideTestRunner, AssertionKindsExecution) {
  auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR
                                                   "AssertionsTest.stride");
  ASSERT_EQ(specs.size(), 2);

  FunctionTestRunner runner(reinterpret_cast<void *>(&mock_passthru_process));

  // TestNear
  auto resultNear = runner.runTest(specs[0]);
  EXPECT_TRUE(resultNear.passed) << resultNear.errorMessage;
  EXPECT_EQ(resultNear.ticksExecuted, 2);

  // TestGtLt
  auto resultGtLt = runner.runTest(specs[1]);
  EXPECT_TRUE(resultGtLt.passed) << resultGtLt.errorMessage;
  EXPECT_EQ(resultGtLt.ticksExecuted, 2);
}

TEST(StrideTestRunner, ModuleTestExecution) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "ModuleTest.stride");
  ASSERT_TRUE(ret);

  strenv.initializeJIT();
  auto compiled = strenv.compileInMemory();
  ASSERT_TRUE(compiled);

  auto entrySym = strenv.getFunction("GainDomain_process");
  ASSERT_TRUE(entrySym.operator bool());

  void *fnPtr = reinterpret_cast<void *>(entrySym->getValue());
  ASSERT_NE(fnPtr, nullptr);

  auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR
                                                   "ModuleTest.stride");
  ASSERT_EQ(specs.size(), 1);
  EXPECT_EQ(specs[0].testName, "GainModuleTest");
  EXPECT_EQ(specs[0].functionName, "GainDomain");

  FunctionTestRunner runner(fnPtr);
  std::shared_ptr<void> state = strenv.allocateSharedState("GainDomain");
  ASSERT_NE(state, nullptr);
  runner.setStatePointer(state.get());
  runner.setStateAccessor(
      {[&strenv](void *statePtr, const std::string &name, double val) {
         strenv.setStateVar<double>(statePtr, name, val, std::nullopt, "GainDomain");
       },
       [&strenv](const void *statePtr, const std::string &name) {
         return strenv.getStateVar<double>(statePtr, name, std::nullopt, "GainDomain").value_or(0.0);
       }});

  auto result = runner.runTest(specs[0]);

  EXPECT_TRUE(result.passed) << result.errorMessage;
  EXPECT_EQ(result.ticksExecuted, 4);
  EXPECT_EQ(result.heapAllocations, 0);
  EXPECT_GT(result.perfMetrics.meanNs, 0.0);
}

TEST(StrideTestDynamicLoader, NonExistentLibrary) {
  DynamicLoader loader("non_existent_library_12345.dll");
  EXPECT_FALSE(loader.isLoaded());
  EXPECT_FALSE(loader.getErrorMessage().empty());
  EXPECT_EQ(loader.getRawSymbol("any_symbol"), nullptr);
}

TEST(StrideTestReporter, ConsoleAndJUnitXmlOutput) {
  TestReporter reporter;
  FunctionTestSpec spec1;
  spec1.testName = "UnitTest1";
  spec1.functionName = "Proc1";
  TestRunResult res1;
  res1.passed = true;
  res1.ticksExecuted = 10;
  res1.perfMetrics.meanNs = 45.2;
  res1.perfMetrics.cv = 0.05;
  res1.perfMetrics.iterations = 1000;
  reporter.addResult(spec1, res1);

  FunctionTestSpec spec2;
  spec2.testName = "UnitTest2";
  spec2.functionName = "Proc2";
  TestRunResult res2;
  res2.passed = false;
  res2.errorMessage = "Assertion failed on port 'Out'";
  res2.ticksExecuted = 3;
  res2.perfMetrics.meanNs = 30.1;
  res2.perfMetrics.iterations = 500;
  reporter.addResult(spec2, res2);

  EXPECT_EQ(reporter.totalTests(), 2);
  EXPECT_EQ(reporter.passedTests(), 1);
  EXPECT_EQ(reporter.failedTests(), 1);

  std::string consoleSummary = reporter.formatConsoleSummary();
  EXPECT_NE(consoleSummary.find("UnitTest1"), std::string::npos);
  EXPECT_NE(consoleSummary.find("UnitTest2"), std::string::npos);
  EXPECT_NE(consoleSummary.find("1 passed, 1 failed, 2 total"),
            std::string::npos);

  std::string xml = reporter.generateJUnitXml("StrideModuleTests");
  EXPECT_NE(
      xml.find(
          "<testsuite name=\"StrideModuleTests\" tests=\"2\" failures=\"1\""),
      std::string::npos);
  EXPECT_NE(xml.find("<testcase name=\"UnitTest1\""), std::string::npos);
  EXPECT_NE(xml.find("<failure message=\"Assertion failed on port 'Out'\""),
            std::string::npos);
}

TEST(StrideTestRunner, RepeatedExecution) {
  auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR "PassthruTest.stride");
  ASSERT_EQ(specs.size(), 1);

  FunctionTestRunner runner(reinterpret_cast<void*>(&mock_passthru_process));
  TestReporter reporter;
  const int repeatCount = 5;

  for (int rep = 0; rep < repeatCount; ++rep) {
    FunctionTestSpec runSpec = specs[0];
    runSpec.testName = specs[0].testName + " [run " + std::to_string(rep + 1) + "]";
    auto result = runner.runTest(specs[0]);
    EXPECT_TRUE(result.passed) << result.errorMessage;
    reporter.addResult(runSpec, result);
  }

  EXPECT_EQ(reporter.totalTests(), 5);
  EXPECT_EQ(reporter.passedTests(), 5);
  EXPECT_EQ(reporter.failedTests(), 0);
}

} // namespace
