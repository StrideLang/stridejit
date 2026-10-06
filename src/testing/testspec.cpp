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
    return values.size();
}

bool SignalStreamVector::matches(const SignalScalarValue& actual, size_t tick) const {
    if (kind == AssertionKind::True) {
        return toBool(actual) == true;
    }
    if (kind == AssertionKind::False) {
        return toBool(actual) == false;
    }

    if (tick >= values.size()) return false;
    const auto& expected = values[tick];

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

std::string SignalStreamVector::formatValue(size_t tick) const {
    if (tick >= values.size()) return "<out-of-bounds>";
    std::ostringstream ss;
    std::visit([&](auto&& v) { ss << v; }, values[tick]);
    return ss.str();
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
