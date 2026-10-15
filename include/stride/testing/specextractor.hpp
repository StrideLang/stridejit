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
    static void extractStreams(const ASTNode& streamListNode, std::vector<SignalStreamVector>& streams);
};

} // namespace strd::test

#endif // STRIDE_TESTING_SPECEXTRACTOR_HPP
