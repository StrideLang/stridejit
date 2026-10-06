#include "gtest/gtest.h"

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/valuenode.h"

#include "stride/testing/testspec.hpp"
#include "stride/testing/perfanalyzer.hpp"
#include "stride/testing/memorytracker.hpp"
#include "stride/testing/specextractor.hpp"
#include "stride/testing/functiontestrunner.hpp"

#include "stride/stridejit/strideenvironment.hpp"

namespace {

using namespace strd::test;

// Mock C function matching Stride domain calling convention: void(*)(double* in, double* out)
void mock_passthru_process(double* in, double* out) {
    if (in && out) {
        *out = *in;
    }
}

TEST(StrideTestSpec, SignalVectorMatching) {
    SignalStreamVector vec;
    vec.portName = "Out";
    vec.values = { 1.0, 2.0, 3.0 };
    vec.epsilon = 1e-4;

    EXPECT_TRUE(vec.matches(1.00001, 0));
    EXPECT_TRUE(vec.matches(2.0, 1));
    EXPECT_FALSE(vec.matches(3.1, 2));
    EXPECT_FALSE(vec.matches(1.0, 5)); // out of bounds
}

TEST(StrideTestPerf, AnalyzerStats) {
    auto stats = PerfAnalyzer::profile(10, 100, []() {
        volatile int x = 0;
        for (int i = 0; i < 50; ++i) x += i;
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
    auto testNode = std::make_shared<strd::DeclarationNode>("ImpulseResponse", "functionTest", nullptr, __FILE__, __LINE__);
    auto funcVal = std::make_shared<strd::ValueNode>("FirFilter", __FILE__, __LINE__);
    testNode->setPropertyValue("function", funcVal);

    auto spec = SpecExtractor::extractSingleTest(testNode);
    EXPECT_EQ(spec.testName, "ImpulseResponse");
    EXPECT_EQ(spec.functionName, "FirFilter");
}

TEST(StrideTestSpec, ExtractFromFile) {
    auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR "PassthruTest.stride");
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
}

TEST(StrideTestRunner, DirectFunctionExecutionSuccess) {
    auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR "PassthruTest.stride");
    ASSERT_EQ(specs.size(), 1);

    FunctionTestRunner runner(reinterpret_cast<void*>(&mock_passthru_process));
    ASSERT_TRUE(runner.isLoaded());

    auto result = runner.runTest(specs[0]);
    EXPECT_TRUE(result.passed) << result.errorMessage;
    EXPECT_EQ(result.ticksExecuted, 5);
    EXPECT_EQ(result.heapAllocations, 0);
    EXPECT_GT(result.perfMetrics.meanNs, 0.0);
}

TEST(StrideTestRunner, FailureDiagnostics) {
    auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR "PassthruTest.stride");
    ASSERT_EQ(specs.size(), 1);

    // Modify expected value at tick 2 to intentionally trigger a failure
    specs[0].validateStreams[0].values[2] = 99.0;

    FunctionTestRunner runner(reinterpret_cast<void*>(&mock_passthru_process));
    auto result = runner.runTest(specs[0]);

    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.ticksExecuted, 2);
    EXPECT_NE(result.errorMessage.find("ExpectEqual failed on port 'Out' at tick 2"), std::string::npos);
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

    void* fnPtr = reinterpret_cast<void*>(entrySym->getValue());
    ASSERT_NE(fnPtr, nullptr);

    auto specs = SpecExtractor::extractTestsFromFile(STRIDEJIT_TESTS_SOURCE_DIR "PassthruTest.stride");
    ASSERT_EQ(specs.size(), 1);

    FunctionTestRunner runner(fnPtr);
    std::shared_ptr<void> state = strenv.allocateSharedState("TestDomain");
    ASSERT_NE(state, nullptr);
    runner.setStatePointer(state.get());
    runner.setStateAccessor({
        [&strenv](void* statePtr, const std::string& name, double val) {
            strenv.setStateVar<double>(statePtr, name, val);
        },
        [&strenv](const void* statePtr, const std::string& name) {
            return strenv.getStateVar<double>(statePtr, name).value_or(0.0);
        }
    });

    auto result = runner.runTest(specs[0]);

    EXPECT_TRUE(result.passed) << result.errorMessage;
    EXPECT_EQ(result.ticksExecuted, 5);
    EXPECT_EQ(result.heapAllocations, 0);
    EXPECT_GT(result.perfMetrics.meanNs, 0.0);
}

} // namespace
