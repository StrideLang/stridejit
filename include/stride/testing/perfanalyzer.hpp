#ifndef STRIDE_TESTING_PERFANALYZER_HPP
#define STRIDE_TESTING_PERFANALYZER_HPP

#include <chrono>
#include <vector>
#include <string>
#include <cstddef>
#include <functional>

namespace strd::test {

struct PerfMetrics {
    double minNs{0.0};
    double meanNs{0.0};
    double medianNs{0.0};
    double p99Ns{0.0};
    double maxNs{0.0};
    double stddevNs{0.0};
    double cv{0.0}; // Coefficient of variation: stddev / mean
    size_t iterations{0};

    void recordToGTest(const std::string& prefix = "") const;
};

class PerfAnalyzer {
public:
    static PerfMetrics profileFunction(size_t warmupRuns, size_t measureRuns, const std::function<void()>& func);

    template <typename Callable>
    static PerfMetrics profile(size_t warmupRuns, size_t measureRuns, Callable&& func) {
        return profileFunction(warmupRuns, measureRuns, std::function<void()>(std::forward<Callable>(func)));
    }
};

} // namespace strd::test

#endif // STRIDE_TESTING_PERFANALYZER_HPP
