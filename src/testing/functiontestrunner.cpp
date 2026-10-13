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
        case 9: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]);
            break;
        }
        case 10: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
            break;
        }
        case 11: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]);
            break;
        }
        case 12: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]);
            break;
        }
        case 13: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]);
            break;
        }
        case 14: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]);
            break;
        }
        case 15: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]);
            break;
        }
        case 16: {
            auto fn = reinterpret_cast<void(*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*)>(fnPtr);
            fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]);
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
        std::string targetName = procName;
        if (!processFn) {
            processFn = m_loader->getRawSymbol(spec.functionName);
            targetName = spec.functionName;
        }

        if (!processFn) {
            TestRunResult result;
            result.passed = false;
            result.errorMessage = "Could not resolve process symbol: '" + procName + "' in shared library.";
            return result;
        }

        void* invokerFn = m_loader->getRawSymbol(targetName + "_invoker");
        if (!invokerFn) {
            invokerFn = m_loader->getRawSymbol(spec.functionName + "_invoker");
        }
        if (invokerFn) {
            m_invokerPointer = invokerFn;
        }
    } else {
        processFn = m_directFunctionPointer;
    }

    return runTestWithFunction(spec, processFn);
}

TestRunResult FunctionTestRunner::runTestWithFunction(const FunctionTestSpec& spec, void* processFn) {
    TestRunResult result;
    if (!processFn && !m_invokerPointer) {
        result.passed = false;
        result.errorMessage = "Process function pointer is null.";
        return result;
    }

    auto toDoubleVal = [](const SignalScalarValue& v) -> double {
        return std::visit([](auto&& arg) -> double {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, bool>) return arg ? 1.0 : 0.0;
            else return static_cast<double>(arg);
        }, v);
    };

    auto toInt32Val = [](const SignalScalarValue& v) -> int32_t {
        return std::visit([](auto&& arg) -> int32_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, bool>) return arg ? 1 : 0;
            else return static_cast<int32_t>(arg);
        }, v);
    };

    auto toBoolVal = [](const SignalScalarValue& v) -> bool {
        return std::visit([](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, bool>) return arg;
            else return arg != 0;
        }, v);
    };

    // If metadata (m_argInfos) is available, use typed argument binding
    if (!m_argInfos.empty()) {
        struct ArgBuffer {
            std::vector<double> doubleVec;
            std::vector<int32_t> int32Vec;
            std::vector<uint8_t> boolVec;
            double scalarDouble{0.0};
            int32_t scalarInt32{0};
            uint8_t scalarBool{0};
        };

        std::vector<ArgBuffer> argBuffers(m_argInfos.size());
        std::vector<void*> argPtrs(m_argInfos.size(), nullptr);

        auto populateArgsForTick = [&](size_t t) {
            for (size_t i = 0; i < m_argInfos.size(); ++i) {
                const auto& info = m_argInfos[i];
                if (info.role == FunctionArgInfo::Role::State) {
                    argPtrs[i] = m_statePtr;
                } else if (info.role == FunctionArgInfo::Role::MemberAccess) {
                    if (!argPtrs[i]) {
                        if (info.type == DataType::INT32) {
                            argBuffers[i].scalarInt32 = info.propertyValue;
                            argPtrs[i] = &argBuffers[i].scalarInt32;
                        } else {
                            argBuffers[i].scalarDouble = static_cast<double>(info.propertyValue);
                            argPtrs[i] = &argBuffers[i].scalarDouble;
                        }
                    }
                }
            }

            for (size_t i = 0; i < m_argInfos.size(); ++i) {
                const auto& info = m_argInfos[i];
                if (info.role == FunctionArgInfo::Role::State ||
                    info.role == FunctionArgInfo::Role::MemberAccess) {
                    continue;
                }

                const SignalStreamVector* prepStream = nullptr;
                for (const auto& st : spec.prepareStreams) {
                    if (st.portName == info.name) {
                        prepStream = &st;
                        break;
                    }
                }

                const SignalStreamVector* valStream = nullptr;
                for (const auto& st : spec.validateStreams) {
                    if (st.portName == info.name) {
                        valStream = &st;
                        break;
                    }
                }

                if (info.role == FunctionArgInfo::Role::Output ||
                    (info.role == FunctionArgInfo::Role::External && valStream != nullptr)) {
                    if (info.isArray() || (valStream && valStream->isArrayPort)) {
                        size_t arrSize = info.count > 0 ? info.count : 1;
                        if (valStream && t < valStream->tickValues.size() && !valStream->tickValues[t].empty()) {
                            arrSize = valStream->tickValues[t].size();
                        } else if (valStream && !valStream->values.empty()) {
                            arrSize = valStream->values.size();
                        }
                        argBuffers[i].doubleVec.assign(arrSize, 0.0);
                        argPtrs[i] = argBuffers[i].doubleVec.data();
                    } else {
                        argBuffers[i].scalarDouble = 0.0;
                        argBuffers[i].scalarInt32 = 0;
                        argBuffers[i].scalarBool = 0;
                        if (info.type == DataType::INT32) {
                            argPtrs[i] = &argBuffers[i].scalarInt32;
                        } else if (info.type == DataType::BOOL) {
                            argPtrs[i] = &argBuffers[i].scalarBool;
                        } else {
                            argPtrs[i] = &argBuffers[i].scalarDouble;
                        }
                    }
                } else if (info.role == FunctionArgInfo::Role::Input ||
                           info.role == FunctionArgInfo::Role::Property ||
                           info.role == FunctionArgInfo::Role::External) {
                    bool isArr = info.isArray() || (prepStream && prepStream->isArrayPort);
                    if (isArr) {
                        std::vector<SignalScalarValue> curVals;
                        if (prepStream) {
                            if (t < prepStream->tickValues.size()) {
                                curVals = prepStream->tickValues[t];
                            } else if (t < prepStream->values.size()) {
                                curVals = prepStream->values;
                            }
                        }
                        size_t arrSize = !curVals.empty() ? curVals.size() : (info.count > 0 ? info.count : 0);
                        if (info.type == DataType::INT32) {
                            argBuffers[i].int32Vec.resize(arrSize, 0);
                            for (size_t k = 0; k < curVals.size(); ++k) {
                                argBuffers[i].int32Vec[k] = toInt32Val(curVals[k]);
                            }
                            argPtrs[i] = argBuffers[i].int32Vec.data();
                        } else {
                            argBuffers[i].doubleVec.resize(arrSize, 0.0);
                            for (size_t k = 0; k < curVals.size(); ++k) {
                                argBuffers[i].doubleVec[k] = toDoubleVal(curVals[k]);
                            }
                            argPtrs[i] = argBuffers[i].doubleVec.data();
                        }

                        if (!info.sizeProperty.empty()) {
                            for (size_t s = 0; s < m_argInfos.size(); ++s) {
                                if (m_argInfos[s].name == info.sizeProperty) {
                                    argBuffers[s].scalarInt32 = static_cast<int32_t>(arrSize);
                                    argPtrs[s] = &argBuffers[s].scalarInt32;
                                    break;
                                }
                            }
                        }
                    } else {
                        SignalScalarValue val = 0.0;
                        if (prepStream) {
                            if (t < prepStream->tickValues.size() && !prepStream->tickValues[t].empty()) {
                                val = prepStream->tickValues[t][0];
                            } else if (t < prepStream->values.size()) {
                                val = prepStream->values[t];
                            }
                        }
                        argBuffers[i].scalarDouble = toDoubleVal(val);
                        argBuffers[i].scalarInt32 = toInt32Val(val);
                        argBuffers[i].scalarBool = toBoolVal(val) ? 1 : 0;
                        if (info.type == DataType::INT32) {
                            argPtrs[i] = &argBuffers[i].scalarInt32;
                        } else if (info.type == DataType::BOOL) {
                            argPtrs[i] = &argBuffers[i].scalarBool;
                        } else {
                            argPtrs[i] = &argBuffers[i].scalarDouble;
                        }

                        if (m_stateAccessor.has_value() && m_statePtr && prepStream) {
                            m_stateAccessor->setVar(m_statePtr, prepStream->portName, argBuffers[i].scalarDouble);
                        }
                    }
                }
            }
        };

        auto invokeStep = [&]() {
            if (m_invokerPointer) {
                auto invokerFn = reinterpret_cast<int32_t(*)(void**)>(m_invokerPointer);
                invokerFn(argPtrs.data());
            } else {
                invokeProcessFunction(processFn, argPtrs);
            }
        };

        {
            NoHeapAllocGuard allocGuard;

            for (size_t t = 0; t < spec.tickCount; ++t) {
                populateArgsForTick(t);
                invokeStep();

                // Validate outputs at tick t
                for (size_t v = 0; v < spec.validateStreams.size(); ++v) {
                    const auto& valStream = spec.validateStreams[v];
                    size_t argIdx = m_argInfos.size();
                    for (size_t a = 0; a < m_argInfos.size(); ++a) {
                        if (m_argInfos[a].name == valStream.portName) {
                            argIdx = a;
                            break;
                        }
                    }

                    bool matched = false;
                    SignalScalarValue actualScalar = 0.0;
                    if (argIdx < m_argInfos.size()) {
                        if (valStream.isArrayPort) {
                            std::vector<SignalScalarValue> actualVec;
                            actualVec.reserve(argBuffers[argIdx].doubleVec.size());
                            for (double d : argBuffers[argIdx].doubleVec) actualVec.push_back(d);
                            matched = valStream.matches(actualVec, t);
                        } else {
                            if (m_argInfos[argIdx].type == DataType::INT32) {
                                actualScalar = argBuffers[argIdx].scalarInt32;
                            } else if (m_argInfos[argIdx].type == DataType::BOOL) {
                                actualScalar = (argBuffers[argIdx].scalarBool != 0);
                            } else {
                                actualScalar = argBuffers[argIdx].scalarDouble;
                            }
                            if (m_stateAccessor.has_value() && m_statePtr) {
                                actualScalar = m_stateAccessor->getVar(m_statePtr, valStream.portName);
                            }
                            matched = valStream.matches(actualScalar, t);
                        }
                    } else if (m_stateAccessor.has_value() && m_statePtr) {
                        actualScalar = m_stateAccessor->getVar(m_statePtr, valStream.portName);
                        matched = valStream.matches(actualScalar, t);
                    }

                    if (!matched) {
                        result.passed = false;
                        std::ostringstream ss;
                        ss << "Assertion failed on port '" << valStream.portName << "' at tick " << t << "\n";
                        ss << "  Expected: " << valStream.formatCondition(t);
                        ss << "\n  Actual:   ";
                        if (valStream.isArrayPort && argIdx < m_argInfos.size()) {
                            ss << "[";
                            for (size_t k = 0; k < argBuffers[argIdx].doubleVec.size(); ++k) {
                                if (k > 0) ss << ", ";
                                ss << argBuffers[argIdx].doubleVec[k];
                            }
                            ss << "]";
                        } else {
                            if (std::holds_alternative<double>(actualScalar)) {
                                ss << std::get<double>(actualScalar);
                            } else if (std::holds_alternative<int32_t>(actualScalar)) {
                                ss << std::get<int32_t>(actualScalar);
                            } else if (std::holds_alternative<int64_t>(actualScalar)) {
                                ss << std::get<int64_t>(actualScalar);
                            } else if (std::holds_alternative<bool>(actualScalar)) {
                                ss << (std::get<bool>(actualScalar) ? "true" : "false");
                            }
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
        result.perfMetrics = PerfAnalyzer::profile(50, 2000, [&]() {
            invokeStep();
        });

        return result;
    }

    // Fallback path when m_argInfos is not provided (legacy mock tests)
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
                    ss << "Assertion failed on port '" << valStream.portName << "' at tick " << t << "\n";
                    ss << "  Expected: " << valStream.formatCondition(t);
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
