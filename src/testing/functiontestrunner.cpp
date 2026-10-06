#include "stride/testing/functiontestrunner.hpp"
#include "stride/testing/memorytracker.hpp"
#include <sstream>
#include <iostream>

namespace strd::test {

namespace {

inline void invokeProcessFunction(void* fnPtr, const std::vector<void*>& args) {
    if (!fnPtr) return;

    switch (args.size()) {
        case 0: {
            auto fn = reinterpret_cast<void(*)()>(fnPtr);
            fn();
            break;
        }
        case 1: {
            auto fn = reinterpret_cast<void(*)(void*)>(fnPtr);
            fn(args[0]);
            break;
        }
        case 2: {
            auto fn = reinterpret_cast<void(*)(void*, void*)>(fnPtr);
            fn(args[0], args[1]);
            break;
        }
        case 3: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2]);
            break;
        }
        case 4: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3]);
            break;
        }
        case 5: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4]);
            break;
        }
        case 6: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5]);
            break;
        }
        case 7: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
            break;
        }
        case 8: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
            break;
        }
        default:
            std::cerr << "Warning: invokeProcessFunction called with unsupported argument count: " << args.size() << std::endl;
            break;
    }
}

} // namespace

FunctionTestRunner::FunctionTestRunner(const std::string& sharedLibPath)
    : m_loader(std::make_unique<DynamicLoader>(sharedLibPath)) {}

FunctionTestRunner::FunctionTestRunner(void* functionPointer)
    : m_directFunctionPointer(functionPointer) {
    if (!m_directFunctionPointer) {
        m_directErrorMsg = "FunctionTestRunner initialized with null function pointer.";
    }
}

bool FunctionTestRunner::isLoaded() const {
    if (m_loader) {
        return m_loader->isLoaded();
    }
    return m_directFunctionPointer != nullptr;
}

const std::string& FunctionTestRunner::getErrorMessage() const {
    if (m_loader) {
        return m_loader->getErrorMessage();
    }
    return m_directErrorMsg;
}

TestRunResult FunctionTestRunner::runTest(const FunctionTestSpec& spec) {
    void* processFn = nullptr;
    if (m_loader) {
        if (!m_loader->isLoaded()) {
            TestRunResult result;
            result.passed = false;
            result.errorMessage = m_loader->getErrorMessage();
            return result;
        }

        std::string procName = spec.functionName + "_process";
        processFn = m_loader->getRawSymbol(procName);
        if (!processFn) {
            processFn = m_loader->getRawSymbol(spec.functionName);
        }

        if (!processFn) {
            TestRunResult result;
            result.passed = false;
            result.errorMessage = "Could not resolve process symbol: '" + procName + "' in shared library.";
            return result;
        }
    } else {
        processFn = m_directFunctionPointer;
    }

    return runTestWithFunction(spec, processFn);
}

TestRunResult FunctionTestRunner::runTestWithFunction(const FunctionTestSpec& spec, void* processFn) {
    TestRunResult result;
    if (!processFn) {
        result.passed = false;
        result.errorMessage = "Process function pointer is null.";
        return result;
    }

    // Allocate typed storage for inputs and outputs
    std::vector<double> inDoubles(spec.prepareStreams.size(), 0.0);
    std::vector<double> outDoubles(spec.validateStreams.size(), 0.0);

    // Build argument pointers list (state pointer if present, then all inputs followed by all outputs)
    std::vector<void*> argPtrs;
    argPtrs.reserve((m_statePtr ? 1 : 0) + inDoubles.size() + outDoubles.size());
    if (m_statePtr) {
        argPtrs.push_back(m_statePtr);
    }
    for (size_t i = 0; i < inDoubles.size(); ++i) {
        argPtrs.push_back(&inDoubles[i]);
    }
    for (size_t i = 0; i < outDoubles.size(); ++i) {
        argPtrs.push_back(&outDoubles[i]);
    }

    // Main multi-tick execution loop
    {
        NoHeapAllocGuard allocGuard;

        for (size_t t = 0; t < spec.tickCount; ++t) {
            // Populate input values for tick t
            for (size_t i = 0; i < spec.prepareStreams.size(); ++i) {
                const auto& prep = spec.prepareStreams[i];
                if (t < prep.values.size()) {
                    if (std::holds_alternative<double>(prep.values[t])) {
                        inDoubles[i] = std::get<double>(prep.values[t]);
                    } else if (std::holds_alternative<int32_t>(prep.values[t])) {
                        inDoubles[i] = static_cast<double>(std::get<int32_t>(prep.values[t]));
                    } else if (std::holds_alternative<int64_t>(prep.values[t])) {
                        inDoubles[i] = static_cast<double>(std::get<int64_t>(prep.values[t]));
                    } else if (std::holds_alternative<bool>(prep.values[t])) {
                        inDoubles[i] = std::get<bool>(prep.values[t]) ? 1.0 : 0.0;
                    }
                }
                if (m_stateAccessor.has_value() && m_statePtr) {
                    m_stateAccessor->setVar(m_statePtr, prep.portName, inDoubles[i]);
                }
            }

            // Execute the compiled domain process function
            invokeProcessFunction(processFn, argPtrs);

            // Validate outputs at tick t
            for (size_t i = 0; i < spec.validateStreams.size(); ++i) {
                const auto& valStream = spec.validateStreams[i];
                SignalScalarValue actualVal = outDoubles[i];
                if (m_stateAccessor.has_value() && m_statePtr) {
                    actualVal = m_stateAccessor->getVar(m_statePtr, valStream.portName);
                }
                if (!valStream.matches(actualVal, t)) {
                    result.passed = false;
                    std::ostringstream ss;
                    ss << "ExpectEqual failed on port '" << valStream.portName << "' at tick " << t << "\n";
                    if (t < valStream.values.size()) {
                        ss << "  Expected: " << valStream.formatValue(t);
                        ss << "\n  Actual:   ";
                        if (std::holds_alternative<double>(actualVal)) {
                            ss << std::get<double>(actualVal);
                        } else if (std::holds_alternative<int32_t>(actualVal)) {
                            ss << std::get<int32_t>(actualVal);
                        } else if (std::holds_alternative<int64_t>(actualVal)) {
                            ss << std::get<int64_t>(actualVal);
                        } else if (std::holds_alternative<bool>(actualVal)) {
                            ss << (std::get<bool>(actualVal) ? "true" : "false");
                        }
                        ss << " (epsilon: " << valStream.epsilon << ")";
                    }
                    result.errorMessage = ss.str();
                    result.ticksExecuted = t;
                    return result;
                }
            }
        }

        result.heapAllocations = allocGuard.getDeltaAllocations();
    }

    result.ticksExecuted = spec.tickCount;

    // Performance and jitter profiling
    result.perfMetrics = PerfAnalyzer::profile(50, 2000, [&]() {
        invokeProcessFunction(processFn, argPtrs);
    });

    return result;
}

} // namespace strd::test
