/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2017-2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Author: Andres Cabrera
*/

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
