#ifndef STRIDE_TESTING_TESTSPEC_HPP
#define STRIDE_TESTING_TESTSPEC_HPP

#include <string>
#include <vector>
#include <variant>
#include <cstdint>

namespace strd::test {

using SignalScalarValue = std::variant<double, int32_t, int64_t, bool>;

struct SignalStreamVector {
    std::string portName;
    std::vector<SignalScalarValue> values;
    double epsilon{1e-6};

    size_t size() const;
    bool matches(const SignalScalarValue& actual, size_t tick) const;
    std::string formatValue(size_t tick) const;
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
