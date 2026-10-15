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

#ifndef STRIDE_TESTING_DYNAMICLOADER_HPP
#define STRIDE_TESTING_DYNAMICLOADER_HPP

#include <string>

namespace strd::test {

class DynamicLoader {
public:
    explicit DynamicLoader(const std::string& libraryPath);
    ~DynamicLoader();

    // Non-copyable, movable
    DynamicLoader(const DynamicLoader&) = delete;
    DynamicLoader& operator=(const DynamicLoader&) = delete;

    DynamicLoader(DynamicLoader&& other) noexcept;
    DynamicLoader& operator=(DynamicLoader&& other) noexcept;

    bool isLoaded() const;
    const std::string& getErrorMessage() const;

    void* getRawSymbol(const std::string& symbolName) const;

    template <typename FuncPtr>
    FuncPtr getSymbol(const std::string& symbolName) const {
        return reinterpret_cast<FuncPtr>(getRawSymbol(symbolName));
    }

private:
    void* m_handle{nullptr};
    std::string m_errorMsg;
};

} // namespace strd::test

#endif // STRIDE_TESTING_DYNAMICLOADER_HPP
