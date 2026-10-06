#ifndef STRIDE_TESTING_TESTREPORTER_HPP
#define STRIDE_TESTING_TESTREPORTER_HPP

#include <string>
#include <vector>
#include <iostream>
#include "stride/testing/testspec.hpp"
#include "stride/testing/functiontestrunner.hpp"

namespace strd::test {

struct TestCaseReport {
    FunctionTestSpec spec;
    TestRunResult result;
};

class TestReporter {
public:
    void addResult(const FunctionTestSpec& spec, const TestRunResult& result);

    size_t totalTests() const;
    size_t passedTests() const;
    size_t failedTests() const;

    std::string formatConsoleSummary() const;
    void printConsoleSummary(std::ostream& os = std::cout) const;

    std::string generateJUnitXml(const std::string& suiteName = "StrideTestSuite") const;

    const std::vector<TestCaseReport>& getReports() const { return m_reports; }

private:
    std::vector<TestCaseReport> m_reports;
};

} // namespace strd::test

#endif // STRIDE_TESTING_TESTREPORTER_HPP
