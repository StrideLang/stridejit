#ifndef STRIDE_TESTING_SPECEXTRACTOR_HPP
#define STRIDE_TESTING_SPECEXTRACTOR_HPP

#include <vector>
#include <string>
#include "stride/parser/ast.h"
#include "stride/testing/testspec.hpp"

namespace strd::test {

class SpecExtractor {
public:
    static std::vector<FunctionTestSpec> extractTests(const ASTNode& tree);
    static std::vector<FunctionTestSpec> extractTestsFromFile(const std::string& filePath);
    static FunctionTestSpec extractSingleTest(const ASTNode& testNode);

private:
    static void extractStreams(const ASTNode& blockNode, std::vector<SignalStreamVector>& streams);
};

} // namespace strd::test

#endif // STRIDE_TESTING_SPECEXTRACTOR_HPP
