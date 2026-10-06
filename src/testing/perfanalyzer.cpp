#include "stride/testing/perfanalyzer.hpp"
#include <numeric>
#include <algorithm>
#include <cmath>
#include <chrono>

#include "gtest/gtest.h"

namespace strd::test {

void PerfMetrics::recordToGTest(const std::string& prefix) const {
    std::string p = prefix.empty() ? "" : (prefix + "_");
    ::testing::Test::RecordProperty(p + "Min_ns", std::to_string(minNs));
    ::testing::Test::RecordProperty(p + "Mean_ns", std::to_string(meanNs));
    ::testing::Test::RecordProperty(p + "Median_ns", std::to_string(medianNs));
    ::testing::Test::RecordProperty(p + "P99_ns", std::to_string(p99Ns));
    ::testing::Test::RecordProperty(p + "Max_ns", std::to_string(maxNs));
    ::testing::Test::RecordProperty(p + "StdDev_ns", std::to_string(stddevNs));
    ::testing::Test::RecordProperty(p + "CV_Jitter", std::to_string(cv));
    ::testing::Test::RecordProperty(p + "Iterations", std::to_string(iterations));
}

PerfMetrics PerfAnalyzer::profileFunction(size_t warmupRuns, size_t measureRuns, const std::function<void()>& func) {
    // 1. Warm-up
    for (size_t i = 0; i < warmupRuns; ++i) {
        func();
    }

    // 2. Measure
    std::vector<double> samples;
    samples.reserve(measureRuns);

    for (size_t i = 0; i < measureRuns; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        double durationNs = std::chrono::duration<double, std::nano>(end - start).count();
        samples.push_back(durationNs);
    }

    if (samples.empty()) return {};

    std::sort(samples.begin(), samples.end());

    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    double mean = sum / samples.size();

    double sq_sum = 0.0;
    for (double val : samples) {
        double diff = val - mean;
        sq_sum += diff * diff;
    }
    double stddev = std::sqrt(sq_sum / samples.size());
    double cv = (mean > 0.0) ? (stddev / mean) : 0.0;

    PerfMetrics metrics;
    metrics.minNs = samples.front();
    metrics.maxNs = samples.back();
    metrics.meanNs = mean;
    metrics.medianNs = samples[samples.size() / 2];
    metrics.p99Ns = samples[static_cast<size_t>(samples.size() * 0.99)];
    metrics.stddevNs = stddev;
    metrics.cv = cv;
    metrics.iterations = measureRuns;

    return metrics;
}

} // namespace strd::test
