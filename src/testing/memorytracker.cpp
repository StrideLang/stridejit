#include "stride/testing/memorytracker.hpp"

namespace strd::test {

std::atomic<size_t> MemoryTracker::s_allocCount{0};

void MemoryTracker::resetAllocationCount() {
    s_allocCount.store(0, std::memory_order_relaxed);
}

size_t MemoryTracker::getAllocationCount() {
    return s_allocCount.load(std::memory_order_relaxed);
}

void MemoryTracker::recordAllocation(size_t /*bytes*/) {
    s_allocCount.fetch_add(1, std::memory_order_relaxed);
}

NoHeapAllocGuard::NoHeapAllocGuard() {
    m_startCount = MemoryTracker::getAllocationCount();
}

size_t NoHeapAllocGuard::getDeltaAllocations() const {
    return MemoryTracker::getAllocationCount() - m_startCount;
}

bool NoHeapAllocGuard::hasAllocated() const {
    return getDeltaAllocations() > 0;
}

} // namespace strd::test
