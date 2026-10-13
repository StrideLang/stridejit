#include "stride/testing/testspec.hpp"
#include <cmath>
#include <sstream>
#include <type_traits>

namespace strd::test {

namespace {

inline double toDouble(const SignalScalarValue& v) {
    return std::visit([](auto&& arg) -> double {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            return arg ? 1.0 : 0.0;
        } else {
            return static_cast<double>(arg);
        }
    }, v);
}

inline bool toBool(const SignalScalarValue& v) {
    return std::visit([](auto&& arg) -> bool {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            return arg;
        } else {
            return arg != 0;
        }
    }, v);
}

} // namespace

size_t SignalStreamVector::size() const {
    if (!tickValues.empty()) {
        return tickValues.size();
    }
    return values.size();
}

bool SignalStreamVector::matches(const SignalScalarValue& actual, size_t tick) const {
    if (kind == AssertionKind::True) {
        return toBool(actual) == true;
    }
    if (kind == AssertionKind::False) {
        return toBool(actual) == false;
    }

    SignalScalarValue expected;
    if (!isArrayPort && tick < values.size()) {
        expected = values[tick];
    } else if (tick < tickValues.size() && !tickValues[tick].empty()) {
        expected = tickValues[tick][0];
    } else if (tick < values.size()) {
        expected = values[tick];
    } else {
        return false;
    }

    switch (kind) {
        case AssertionKind::Equal:
            if (std::holds_alternative<bool>(expected) || std::holds_alternative<bool>(actual)) {
                return toBool(actual) == toBool(expected);
            }
            if ((std::holds_alternative<int32_t>(expected) || std::holds_alternative<int64_t>(expected)) &&
                (std::holds_alternative<int32_t>(actual) || std::holds_alternative<int64_t>(actual))) {
                int64_t a = std::holds_alternative<int32_t>(actual) ? std::get<int32_t>(actual) : std::get<int64_t>(actual);
                int64_t e = std::holds_alternative<int32_t>(expected) ? std::get<int32_t>(expected) : std::get<int64_t>(expected);
                return a == e;
            }
            return std::abs(toDouble(actual) - toDouble(expected)) <= epsilon;

        case AssertionKind::Near:
            return std::abs(toDouble(actual) - toDouble(expected)) <= epsilon;

        case AssertionKind::GreaterThan:
            return toDouble(actual) > toDouble(expected);

        case AssertionKind::GreaterThanOrEqual:
            return toDouble(actual) >= (toDouble(expected) - epsilon);

        case AssertionKind::LessThan:
            return toDouble(actual) < toDouble(expected);

        case AssertionKind::LessThanOrEqual:
            return toDouble(actual) <= (toDouble(expected) + epsilon);

        default:
            return false;
    }
}

bool SignalStreamVector::matches(const std::vector<SignalScalarValue>& actual, size_t tick) const {
    if (tick >= tickValues.size()) return false;
    const auto& expected = tickValues[tick];
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i) {
        SignalStreamVector elemStream;
        elemStream.kind = kind;
        elemStream.epsilon = epsilon;
        elemStream.values = {expected[i]};
        if (!elemStream.matches(actual[i], 0)) return false;
    }
    return true;
}

std::string SignalStreamVector::formatValue(size_t tick) const {
    if (tick >= size()) return "<out-of-bounds>";
    if (tick < tickValues.size() && tickValues[tick].size() > 1) {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < tickValues[tick].size(); ++i) {
            if (i > 0) ss << ", ";
            std::visit([&](auto&& v) { ss << v; }, tickValues[tick][i]);
        }
        ss << "]";
        return ss.str();
    }
    if (!isArrayPort && tick < values.size()) {
        std::ostringstream ss;
        std::visit([&](auto&& v) { ss << v; }, values[tick]);
        return ss.str();
    }
    if (tick < tickValues.size() && !tickValues[tick].empty()) {
        std::ostringstream ss;
        std::visit([&](auto&& v) { ss << v; }, tickValues[tick][0]);
        return ss.str();
    }
    if (tick < values.size()) {
        std::ostringstream ss;
        std::visit([&](auto&& v) { ss << v; }, values[tick]);
        return ss.str();
    }
    return "<out-of-bounds>";
}

std::string SignalStreamVector::formatCondition(size_t tick) const {
    std::ostringstream ss;
    switch (kind) {
        case AssertionKind::Equal:
            ss << "== " << formatValue(tick);
            break;
        case AssertionKind::Near:
            ss << "approx == " << formatValue(tick) << " (+/- " << epsilon << ")";
            break;
        case AssertionKind::True:
            ss << "== true";
            break;
        case AssertionKind::False:
            ss << "== false";
            break;
        case AssertionKind::GreaterThan:
            ss << "> " << formatValue(tick);
            break;
        case AssertionKind::GreaterThanOrEqual:
            ss << ">= " << formatValue(tick);
            break;
        case AssertionKind::LessThan:
            ss << "< " << formatValue(tick);
            break;
        case AssertionKind::LessThanOrEqual:
            ss << "<= " << formatValue(tick);
            break;
    }
    return ss.str();
}

} // namespace strd::test
