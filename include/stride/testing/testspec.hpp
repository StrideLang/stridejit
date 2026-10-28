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

#ifndef STRIDE_TESTING_TESTSPEC_HPP
#define STRIDE_TESTING_TESTSPEC_HPP

#include <string>
#include <vector>
#include <variant>
#include <cstdint>

namespace strd::test {

using SignalScalarValue = std::variant<double, int32_t, int64_t, bool>;

enum class AssertionKind {
    Equal,
    Near,
    True,
    False,
    GreaterThan,
    GreaterThanOrEqual,
    LessThan,
    LessThanOrEqual,
    MaxError,
    Snr
};

struct SignalStreamVector {
    std::string portName;
    std::string baselinePort;
    std::vector<SignalScalarValue> values;
    std::vector<std::vector<SignalScalarValue>> tickValues;
    bool isArrayPort{false};
    double epsilon{1e-6};
    double maxError{0.0005};
    double minSnr{80.0};
    AssertionKind kind{AssertionKind::Equal};

    size_t size() const;
    bool matches(const SignalScalarValue& actual, size_t tick) const;
    bool matches(const SignalScalarValue& actual, const SignalScalarValue& expected, size_t tick) const;
    bool matches(const std::vector<SignalScalarValue>& actual, size_t tick) const;
    std::string formatValue(size_t tick) const;
    std::string formatCondition(size_t tick) const;
};

struct FunctionTestSpec {
    std::string testName;
    std::string functionName;
    std::vector<SignalStreamVector> prepareStreams;
    std::vector<SignalStreamVector> validateStreams;
    size_t tickCount{1};
    double timeoutSeconds{0.0};
};

} // namespace strd::test

#endif // STRIDE_TESTING_TESTSPEC_HPP
