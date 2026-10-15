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

#ifndef STRIDE_TESTING_MEMORYTRACKER_HPP
#define STRIDE_TESTING_MEMORYTRACKER_HPP

#include <cstddef>
#include <atomic>

namespace strd::test {

class MemoryTracker {
public:
    static void resetAllocationCount();
    static size_t getAllocationCount();
    static void recordAllocation(size_t bytes);

private:
    static std::atomic<size_t> s_allocCount;
};

// RAII Guard asserting zero heap allocations in the critical section
class NoHeapAllocGuard {
public:
    NoHeapAllocGuard();
    size_t getDeltaAllocations() const;
    bool hasAllocated() const;

private:
    size_t m_startCount{0};
};

} // namespace strd::test

#endif // STRIDE_TESTING_MEMORYTRACKER_HPP
