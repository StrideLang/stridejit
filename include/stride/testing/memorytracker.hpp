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
