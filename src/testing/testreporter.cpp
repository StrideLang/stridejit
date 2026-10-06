#include "stride/testing/testreporter.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace strd::test {

void TestReporter::addResult(const FunctionTestSpec& spec, const TestRunResult& result) {
    m_reports.push_back({spec, result});
}

size_t TestReporter::totalTests() const {
    return m_reports.size();
}

size_t TestReporter::passedTests() const {
    return std::count_if(m_reports.begin(), m_reports.end(), [](const TestCaseReport& r) {
        return r.result.passed;
    });
}

size_t TestReporter::failedTests() const {
    return totalTests() - passedTests();
}

std::string TestReporter::formatConsoleSummary() const {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                           STRIDE TEST EXECUTION REPORT                         \n";
    ss << "================================================================================\n";
    ss << std::left << std::setw(10) << "STATUS"
       << std::setw(28) << "TEST NAME"
       << std::setw(8) << "TICKS"
       << std::setw(12) << "ALLOCS"
       << std::setw(14) << "MEAN (ns)"
       << std::setw(12) << "JITTER (CV)"
       << "\n";
    ss << "--------------------------------------------------------------------------------\n";

    for (const auto& r : m_reports) {
        std::string status = r.result.passed ? "[PASS]" : "[FAIL]";
        std::string testLabel = r.spec.testName;
        if (!r.spec.functionName.empty()) {
            testLabel += " (" + r.spec.functionName + ")";
        }
        if (testLabel.size() > 26) {
            testLabel = testLabel.substr(0, 23) + "...";
        }

        std::ostringstream meanStream;
        meanStream << std::fixed << std::setprecision(1) << r.result.perfMetrics.meanNs;

        std::ostringstream jitterStream;
        jitterStream << std::fixed << std::setprecision(2) << (r.result.perfMetrics.cv * 100.0) << "%";

        ss << std::left << std::setw(10) << status
           << std::setw(28) << testLabel
           << std::setw(8) << r.result.ticksExecuted
           << std::setw(12) << r.result.heapAllocations
           << std::setw(14) << meanStream.str()
           << std::setw(12) << jitterStream.str()
           << "\n";

        if (!r.result.passed && !r.result.errorMessage.empty()) {
            ss << "  >>> ERROR: " << r.result.errorMessage << "\n";
        }
    }

    ss << "================================================================================\n";
    ss << "Summary: " << passedTests() << " passed, " << failedTests() << " failed, "
       << totalTests() << " total.\n";
    ss << "================================================================================\n";

    return ss.str();
}

void TestReporter::printConsoleSummary(std::ostream& os) const {
    os << formatConsoleSummary();
}

static inline std::string escapeXml(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size());
    for (char c : data) {
        switch (c) {
            case '&':  buffer.append("&amp;");       break;
            case '\"': buffer.append("&quot;");      break;
            case '<':  buffer.append("&lt;");        break;
            case '>':  buffer.append("&gt;");        break;
            default:   buffer.push_back(c);          break;
        }
    }
    return buffer;
}

std::string TestReporter::generateJUnitXml(const std::string& suiteName) const {
    std::ostringstream ss;
    double totalTimeSec = 0.0;
    for (const auto& r : m_reports) {
        totalTimeSec += (r.result.perfMetrics.meanNs * r.result.perfMetrics.iterations) / 1e9;
    }

    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<testsuites>\n";
    ss << "  <testsuite name=\"" << escapeXml(suiteName) << "\" tests=\"" << totalTests()
       << "\" failures=\"" << failedTests() << "\" errors=\"0\" time=\""
       << std::fixed << std::setprecision(6) << totalTimeSec << "\">\n";

    for (const auto& r : m_reports) {
        double testTimeSec = (r.result.perfMetrics.meanNs * r.result.perfMetrics.iterations) / 1e9;
        ss << "    <testcase name=\"" << escapeXml(r.spec.testName) << "\" classname=\""
           << escapeXml(r.spec.functionName.empty() ? suiteName : r.spec.functionName)
           << "\" time=\"" << std::fixed << std::setprecision(6) << testTimeSec << "\">\n";

        if (!r.result.passed) {
            ss << "      <failure message=\"" << escapeXml(r.result.errorMessage)
               << "\" type=\"AssertionError\">" << escapeXml(r.result.errorMessage) << "</failure>\n";
        }
        ss << "    </testcase>\n";
    }

    ss << "  </testsuite>\n";
    ss << "</testsuites>\n";

    return ss.str();
}

} // namespace strd::test
