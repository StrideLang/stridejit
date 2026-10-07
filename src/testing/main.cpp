#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "stride/testing/testspec.hpp"
#include "stride/testing/specextractor.hpp"
#include "stride/testing/functiontestrunner.hpp"
#include "stride/testing/testreporter.hpp"
#include "stride/stridejit/strideenvironment.hpp"

namespace fs = std::filesystem;
using namespace strd;
using namespace strd::test;

void printUsage(const char* progName) {
    std::cout << "Stride Unit Test Runner (stridetest)\n\n"
              << "Usage: " << progName << " <test_file_or_dir> [options]\n\n"
              << "Options:\n"
              << "  --repeat, -r, -n <N> Run each test N times\n"
              << "  --lib <path>         Path to compiled shared library (.so / .dll) for AOT execution\n"
              << "  --junit <path>       Generate JUnit XML report at specified file path\n"
              << "  --root <path>        Explicit STRIDEROOT path for library definitions\n"
              << "  -v, --verbose        Enable verbose output\n"
              << "  -h, --help           Display this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string targetPath;
    std::string libPath;
    std::string junitPath;
    std::string strideRoot;
    int repeatCount = 1;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if ((arg == "--repeat" || arg == "-r" || arg == "-n") && i + 1 < argc) {
            try {
                repeatCount = std::stoi(argv[++i]);
                if (repeatCount < 1) {
                    std::cerr << "Error: Repeat count must be a positive integer.\n";
                    return 1;
                }
            } catch (...) {
                std::cerr << "Error: Invalid repeat count: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--lib" && i + 1 < argc) {
            libPath = argv[++i];
        } else if (arg == "--junit" && i + 1 < argc) {
            junitPath = argv[++i];
        } else if (arg == "--root" && i + 1 < argc) {
            strideRoot = argv[++i];
        } else if (arg[0] != '-') {
            targetPath = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (targetPath.empty()) {
        std::cerr << "Error: No test file or directory specified.\n";
        return 1;
    }

    if (!fs::exists(targetPath)) {
        std::cerr << "Error: Path does not exist: " << targetPath << "\n";
        return 1;
    }

    std::vector<std::string> testFiles;
    if (fs::is_directory(targetPath)) {
        for (const auto& entry : fs::recursive_directory_iterator(targetPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".stride") {
                testFiles.push_back(entry.path().string());
            }
        }
    } else {
        testFiles.push_back(targetPath);
    }

    if (testFiles.empty()) {
        std::cout << "No .stride test files found.\n";
        return 0;
    }

    TestReporter reporter;

    for (int rep = 0; rep < repeatCount; ++rep) {
        for (const auto& file : testFiles) {
            auto specs = SpecExtractor::extractTestsFromFile(file);
            if (specs.empty()) {
                if (verbose && rep == 0) {
                    std::cout << "Skipping " << file << " (no functionTest declarations found)\n";
                }
                continue;
            }

            if (!libPath.empty()) {
                // AOT shared library execution mode
                FunctionTestRunner runner(libPath);
                if (!runner.isLoaded()) {
                    std::cerr << "Error loading shared library: " << runner.getErrorMessage() << "\n";
                    return 1;
                }
                for (const auto& spec : specs) {
                    FunctionTestSpec runSpec = spec;
                    if (repeatCount > 1) {
                        runSpec.testName = spec.testName + " [run " + std::to_string(rep + 1) + "]";
                    }
                    auto res = runner.runTest(spec);
                    reporter.addResult(runSpec, res);
                }
            } else {
                // JIT in-memory execution mode
                StrideEnvironment strenv(strideRoot);
                if (!strenv.generateIr(file)) {
                    std::cerr << "Error compiling IR for: " << file << "\n";
                    for (const auto& spec : specs) {
                        FunctionTestSpec runSpec = spec;
                        if (repeatCount > 1) {
                            runSpec.testName = spec.testName + " [run " + std::to_string(rep + 1) + "]";
                        }
                        TestRunResult res;
                        res.passed = false;
                        res.errorMessage = "Failed to generate IR from Stride source";
                        reporter.addResult(runSpec, res);
                    }
                    continue;
                }

                strenv.initializeJIT();
                if (!strenv.compileInMemory()) {
                    std::cerr << "Error compiling JIT in memory for: " << file << "\n";
                    for (const auto& spec : specs) {
                        FunctionTestSpec runSpec = spec;
                        if (repeatCount > 1) {
                            runSpec.testName = spec.testName + " [run " + std::to_string(rep + 1) + "]";
                        }
                        TestRunResult res;
                        res.passed = false;
                        res.errorMessage = "Failed to compile JIT in memory";
                        reporter.addResult(runSpec, res);
                    }
                    continue;
                }

                for (const auto& spec : specs) {
                    std::string procName = spec.functionName + "_process";
                    auto entrySym = strenv.getFunction(procName);
                    if (!entrySym) {
                        entrySym = strenv.getFunction(spec.functionName);
                    }

                    FunctionTestSpec runSpec = spec;
                    if (repeatCount > 1) {
                        runSpec.testName = spec.testName + " [run " + std::to_string(rep + 1) + "]";
                    }

                    if (!entrySym) {
                        TestRunResult res;
                        res.passed = false;
                        res.errorMessage = "Could not find entry function: " + procName;
                        reporter.addResult(runSpec, res);
                        continue;
                    }

                    void* fnPtr = reinterpret_cast<void*>(entrySym->getValue());
                    FunctionTestRunner runner(fnPtr);

                    std::shared_ptr<void> state = strenv.allocateSharedState(spec.functionName);
                    if (state) {
                        runner.setStatePointer(state.get());
                        runner.setStateAccessor({
                            [&strenv, &spec](void* statePtr, const std::string& name, double val) {
                                strenv.setStateVar<double>(statePtr, name, val, std::nullopt, spec.functionName);
                            },
                            [&strenv, &spec](const void* statePtr, const std::string& name) {
                                return strenv.getStateVar<double>(statePtr, name, std::nullopt, spec.functionName).value_or(0.0);
                            }
                        });
                    }

                    auto res = runner.runTest(spec);
                    reporter.addResult(runSpec, res);
                }
            }
        }
    }

    reporter.printConsoleSummary(std::cout);

    if (!junitPath.empty()) {
        std::ofstream xmlFile(junitPath);
        if (xmlFile.is_open()) {
            xmlFile << reporter.generateJUnitXml("StrideUnitTests");
            if (verbose) {
                std::cout << "JUnit XML report generated at: " << junitPath << "\n";
            }
        } else {
            std::cerr << "Warning: Could not open JUnit output file: " << junitPath << "\n";
        }
    }

    return reporter.failedTests() > 0 ? 1 : 0;
}
