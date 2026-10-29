#include "gtest/gtest.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "stride/stridejit/strideenvironment.hpp"
#include "llvm/IR/Module.h"

namespace {

// A compiled buffer fixture. Domain inputs and outputs live in the domain
// state struct, so samples are written and results read through it.
class BufferDomain {
public:
  bool compile(const std::string &fixture) {
    if (!env.generateIr(std::string(STRIDEJIT_TESTS_SOURCE_DIR) + fixture)) {
      ADD_FAILURE() << "generateIr failed for " << fixture;
      return false;
    }
    // The module is handed to the JIT on compile
    for (const auto &global : env.mStrideEnv.TheModule->globals()) {
      if (global.getValueType()->isArrayTy()) {
        arraySizes[global.getName().str()] =
            global.getValueType()->getArrayNumElements();
      }
    }
    if (!env.compileInMemory()) {
      ADD_FAILURE() << "compileInMemory failed for " << fixture;
      return false;
    }
    state = env.allocateSharedState("BufferDom");
    if (!state) {
      ADD_FAILURE() << "Could not allocate BufferDom state";
      return false;
    }
    init();
    return true;
  }

  template <typename T> void set(const std::string &name, T value) {
    EXPECT_TRUE(env.setStateVar<T>(state.get(), name, value, std::nullopt,
                                   "BufferDom"))
        << "No state variable " << name;
  }
  template <typename T>
  T get(const std::string &name, std::optional<int> index = std::nullopt) {
    auto value = env.getStateVar<T>(state.get(), name, index, "BufferDom");
    EXPECT_TRUE(value.has_value()) << "No state variable " << name;
    return value.value_or(T{});
  }

  void process() { invoke("BufferDom_process"); }
  void init() { invoke("BufferDom_init"); }

  // Element count of a global array, e.g. a buffer's storage
  uint64_t arraySize(const std::string &global) const {
    auto it = arraySizes.find(global);
    return it != arraySizes.end() ? it->second : 0;
  }

private:
  void invoke(const std::string &function) {
    void *args[] = {state.get()};
    env.invoke(function, args);
  }

  strd::StrideEnvironment env;
  std::shared_ptr<void> state;
  std::map<std::string, uint64_t> arraySizes;
};

// Pushes `first..last` and checks Result after each sample. `expected` maps
// a sample value to the result the reaction produced on that tick; other
// ticks keep the previous result.
void pushAndExpect(BufferDomain &domain, int first, int last,
                   const std::vector<std::pair<int, double>> &expected,
                   double initialResult = 0.0) {
  double held = initialResult;
  for (int i = first; i <= last; ++i) {
    domain.set<double>("AudioIn", static_cast<double>(i));
    domain.process();
    for (const auto &e : expected) {
      if (e.first == i) {
        held = e.second;
      }
    }
    EXPECT_DOUBLE_EQ(domain.get<double>("Result"), held) << "At sample " << i;
  }
}

TEST(JITBufferTest, BufferTriggerReaction) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_trigger.stride"));
  // Capacity 4 fills on sample 4 -> InBuf[0] (1.0) + 10.0
  pushAndExpect(domain, 1, 4, {{4, 11.0}});
}

TEST(JITBufferTest, SlidingWindowHopReadsInOrderAcrossWrap) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_sliding_window_hop.stride"));
  // N=4, H=2, capacity 8: first frame once 4 samples are in, then every 2.
  // Result is first * 100 + last of the frame.
  pushAndExpect(domain, 1, 12,
                {{4, 104.0}, {6, 306.0}, {8, 508.0}, {10, 710.0},
                 {12, 912.0}});
}

TEST(JITBufferTest, InitRestartsBuffer) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_sliding_window_hop.stride"));
  pushAndExpect(domain, 1, 5, {{4, 104.0}});

  domain.init();
  // Empty again: the first frame needs 4 new samples. _init may also reset
  // Result, so hold whatever it left.
  pushAndExpect(domain, 21, 24, {{24, 2124.0}},
                domain.get<double>("Result"));
}

TEST(JITBufferTest, PeriodicHopNotDividingCapacity) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_periodic_wrap.stride"));
  // Capacity 6, hop 4: the frame at sample 8 straddles the end of the ring
  pushAndExpect(domain, 1, 12, {{4, 104.0}, {8, 508.0}, {12, 912.0}});
}

TEST(JITBufferTest, PingPongAlternatesHalves) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_dma_pingpong.stride"));
  // Frames of half the capacity never cross the end of the ring
  EXPECT_EQ(domain.arraySize("InBuffer"), 8u);
  pushAndExpect(domain, 1, 12, {{4, 104.0}, {8, 508.0}, {12, 912.0}});
}

TEST(JITBufferTest, TriggerBlockFlushesPartialFrame) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_partial_flush.stride"));

  auto push = [&](int value, bool flushNow) {
    domain.set<bool>("FlushIn", flushNow);
    domain.set<double>("AudioIn", static_cast<double>(value));
    domain.process();
  };
  auto result = [&] { return domain.get<double>("Result"); };
  auto count = [&] { return domain.get<int32_t>("Count"); };

  push(1, false);
  push(2, false);
  EXPECT_DOUBLE_EQ(result(), 0.0);
  EXPECT_EQ(count(), 2);

  // Partial frame [1, 2, 3]: first * 100 + third
  push(3, true);
  EXPECT_DOUBLE_EQ(result(), 103.0);
  EXPECT_EQ(count(), 3);

  // The flush empties the buffer before the next sample is stored
  push(4, false);
  EXPECT_DOUBLE_EQ(result(), 103.0);
  EXPECT_EQ(count(), 1);
  push(5, false);
  push(6, false);
  EXPECT_DOUBLE_EQ(result(), 103.0);
  push(7, false);
  EXPECT_DOUBLE_EQ(result(), 406.0);
  EXPECT_EQ(count(), 4);
}

TEST(JITBufferTest, PortSizeIsFrameSize) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_port_size.stride"));
  // Frames of 4 out of a capacity of 8, every 2 samples: first * 100 + 4
  pushAndExpect(domain, 1, 10,
                {{4, 104.0}, {6, 304.0}, {8, 504.0}, {10, 704.0}});
  EXPECT_EQ(domain.get<int32_t>("Capacity"), 8);
}

TEST(JITBufferTest, FrameCopyIsOldestFirstPeek) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_frame_copy.stride"));
  for (int k = 1; k <= 10; ++k) {
    domain.set<double>("AudioIn", static_cast<double>(k));
    domain.process();
    // Peeking doesn't consume
    EXPECT_EQ(domain.get<int32_t>("Count"), std::min(k, 8)) << "At sample " << k;
    if (k >= 4) {
      // The last 4 samples, oldest first, also across the end of the ring
      for (int i = 0; i < 4; ++i) {
        EXPECT_DOUBLE_EQ(domain.get<double>("Snapshot", i),
                         static_cast<double>(k - 3 + i))
            << "At sample " << k << ", element " << i;
      }
    }
  }
}

TEST(JITBufferTest, FrameIntoLoop) {
  BufferDomain domain;
  ASSERT_TRUE(domain.compile("buffer_loop.stride"));
  double held = 0.0;
  for (int k = 1; k <= 10; ++k) {
    domain.set<double>("AudioIn", static_cast<double>(k));
    domain.process();
    // Every tick: sum of the samples in the frame so far (up to 4)
    double expected = k < 4 ? k * (k + 1) / 2.0 : 4.0 * k - 6.0;
    EXPECT_DOUBLE_EQ(domain.get<double>("Total"), expected) << "At sample " << k;
    // Only on ready (every 2 samples once the frame is full)
    if (k >= 4 && k % 2 == 0) {
      held = 4.0 * k - 6.0;
    }
    EXPECT_DOUBLE_EQ(domain.get<double>("Result"), held) << "At sample " << k;
  }
}

TEST(JITBufferTest, FrameCopySizeMismatchIsRejected) {
  strd::StrideEnvironment strenv;
  EXPECT_FALSE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                                 "buffer_frame_size_mismatch.stride"));
}

TEST(JITBufferTest, BareBufferReadIsRejected) {
  strd::StrideEnvironment strenv;
  EXPECT_FALSE(
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "buffer_bare_read.stride"));
}

TEST(JITBufferTest, InvalidSizesAreRejected) {
  strd::StrideEnvironment strenv;
  EXPECT_FALSE(
      strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "buffer_invalid.stride"));
}

TEST(JITBufferTest, PrivatePropertyIsRejected) {
  strd::StrideEnvironment strenv;
  EXPECT_FALSE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                                 "buffer_private_property.stride"));
}

TEST(JITBufferTest, ReadOnlyPropertyIsRejected) {
  strd::StrideEnvironment strenv;
  EXPECT_FALSE(strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                                 "buffer_readonly_property.stride"));
}

} // namespace
