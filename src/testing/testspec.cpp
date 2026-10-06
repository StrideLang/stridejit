#include "stride/testing/testspec.hpp"
#include <cmath>
#include <sstream>

namespace strd::test {

size_t SignalStreamVector::size() const {
    return values.size();
}

bool SignalStreamVector::matches(const SignalScalarValue& actual, size_t tick) const {
    if (tick >= values.size()) return false;
    const auto& expected = values[tick];

    if (std::holds_alternative<double>(expected) && std::holds_alternative<double>(actual)) {
        return std::abs(std::get<double>(expected) - std::get<double>(actual)) <= epsilon;
    }
    if (std::holds_alternative<int32_t>(expected) && std::holds_alternative<int32_t>(actual)) {
        return std::get<int32_t>(expected) == std::get<int32_t>(actual);
    }
    if (std::holds_alternative<int64_t>(expected) && std::holds_alternative<int64_t>(actual)) {
        return std::get<int64_t>(expected) == std::get<int64_t>(actual);
    }
    if (std::holds_alternative<bool>(expected) && std::holds_alternative<bool>(actual)) {
        return std::get<bool>(expected) == std::get<bool>(actual);
    }
    return false;
}

std::string SignalStreamVector::formatValue(size_t tick) const {
    if (tick >= values.size()) return "<out-of-bounds>";
    std::ostringstream ss;
    std::visit([&](auto&& v) { ss << v; }, values[tick]);
    return ss.str();
}

} // namespace strd::test
