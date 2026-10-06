#ifndef STRIDE_TESTING_FUNCTIONTESTRUNNER_HPP
#define STRIDE_TESTING_FUNCTIONTESTRUNNER_HPP

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>

#include "stride/testing/testspec.hpp"
#include "stride/testing/dynamicloader.hpp"
#include "stride/testing/perfanalyzer.hpp"

namespace strd::test {

struct TestRunResult {
    bool passed{true};
    std::string errorMessage;
    size_t ticksExecuted{0};
    size_t heapAllocations{0};
    PerfMetrics perfMetrics;
};

struct StateAccessor {
    std::function<void(void* state, const std::string& name, double val)> setVar;
    std::function<double(const void* state, const std::string& name)> getVar;
};

class FunctionTestRunner {
public:
    explicit FunctionTestRunner(const std::string& sharedLibPath);
    explicit FunctionTestRunner(void* functionPointer);

    bool isLoaded() const;
    const std::string& getErrorMessage() const;

    void setStatePointer(void* statePtr) { m_statePtr = statePtr; }
    void* getStatePointer() const { return m_statePtr; }

    void setStateAccessor(StateAccessor accessor) { m_stateAccessor = std::move(accessor); }

    TestRunResult runTest(const FunctionTestSpec& spec);
    TestRunResult runTestWithFunction(const FunctionTestSpec& spec, void* processFn);

private:
    std::unique_ptr<DynamicLoader> m_loader;
    void* m_directFunctionPointer{nullptr};
    void* m_statePtr{nullptr};
    std::optional<StateAccessor> m_stateAccessor;
    std::string m_directErrorMsg;
};

} // namespace strd::test

#endif // STRIDE_TESTING_FUNCTIONTESTRUNNER_HPP
