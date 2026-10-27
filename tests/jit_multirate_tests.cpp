#include "gtest/gtest.h"
#include <cstdint>
#include <vector>

#include "stride/stridejit/strideenvironment.hpp"

namespace {

TEST(JITMultiRateTest, MultiRateProcessing) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR
                               "multirate_processing.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto rateState = strenv.allocateSharedState("RateDom");
  ASSERT_NE(rateState.get(), nullptr);
  void *rateArgs[] = {rateState.get()};

  strenv.invoke("RateDom_init", rateArgs);

  auto getRateVar = [&](const std::string &name) -> int32_t {
    return strenv.getStateVar(rateState.get(), name, std::nullopt, "RateDom")
        .value_or(-1);
  };

  // Expected values across 9 domain ticks (ticks 0..8):
  // - DownSampledOut (07_downsample.stride pattern: BaseRate/2 -> Out): {1, 1, 2, 2, 3, 3, 4, 4, 5}
  // - UpSampledOut   (08_upsample.stride pattern: BaseRate*3 -> Out):   {3, 6, 9, 12, 15, 18, 21, 24, 27}
  // - DownIntCount (25 Hz in 100 Hz, 1:4): executes on ticks 0, 4, 8 -> {1, 1, 1, 1, 2, 2, 2, 2, 3}
  // - DownFracCount (40 Hz in 100 Hz, 2:5): executes on ticks 0, 3, 5, 8 -> {1, 1, 1, 2, 2, 3, 3, 3, 4}
  // - UpIntCount (400 Hz in 100 Hz, 4:1): +4 per tick -> {4, 8, 12, 16, 20, 24, 28, 32, 36}
  // - UpFracCount (250 Hz in 100 Hz, 5:2): +3, +2, +3, +2... -> {3, 5, 8, 10, 13, 15, 18, 20, 23}
  // - FastStage (400 Hz) >> SlowStage (25 Hz):
  //   FastStage is {4, 8, 12, 16, 20, 24, 28, 32, 36};
  //   SlowStage samples FastStage on ticks 0, 4, 8 -> {4, 4, 4, 4, 20, 20, 20, 20, 36}
  const std::vector<int32_t> expectedDownBy2  = {1, 1, 2, 2, 3, 3, 4, 4, 5};
  const std::vector<int32_t> expectedUpBy3    = {3, 6, 9, 12, 15, 18, 21, 24, 27};
  const std::vector<int32_t> expectedDownInt  = {1, 1, 1, 1, 2, 2, 2, 2, 3};
  const std::vector<int32_t> expectedDownFrac = {1, 1, 1, 2, 2, 3, 3, 3, 4};
  const std::vector<int32_t> expectedUpInt    = {4, 8, 12, 16, 20, 24, 28, 32, 36};
  const std::vector<int32_t> expectedUpFrac   = {3, 5, 8, 10, 13, 15, 18, 20, 23};
  const std::vector<int32_t> expectedSlowStage = {4, 4, 4, 4, 20, 20, 20, 20, 36};

  for (size_t tick = 0; tick < 9; ++tick) {
    strenv.invoke("RateDom_process", rateArgs);
    EXPECT_EQ(getRateVar("DownSampledOut"), expectedDownBy2[tick]) << "at tick " << tick;
    EXPECT_EQ(getRateVar("UpSampledOut"), expectedUpBy3[tick]) << "at tick " << tick;
    EXPECT_EQ(getRateVar("DownIntCount"), expectedDownInt[tick]) << "at tick " << tick;
    EXPECT_EQ(getRateVar("DownFracCount"), expectedDownFrac[tick]) << "at tick " << tick;
    EXPECT_EQ(getRateVar("UpIntCount"), expectedUpInt[tick]) << "at tick " << tick;
    EXPECT_EQ(getRateVar("UpFracCount"), expectedUpFrac[tick]) << "at tick " << tick;
    EXPECT_EQ(getRateVar("FastStage"), expectedUpInt[tick]) << "at tick " << tick;
    EXPECT_EQ(getRateVar("SlowStage"), expectedSlowStage[tick]) << "at tick " << tick;
  }

  // Verify RateDom_init resets both signal values and rate phase counters
  strenv.invoke("RateDom_init", rateArgs);
  strenv.invoke("RateDom_process", rateArgs);
  EXPECT_EQ(getRateVar("DownIntCount"), 1);
  EXPECT_EQ(getRateVar("DownFracCount"), 1);
  EXPECT_EQ(getRateVar("UpIntCount"), 4);
  EXPECT_EQ(getRateVar("UpFracCount"), 3);
  EXPECT_EQ(getRateVar("SlowStage"), 4);

  // Verify one-shot domain (rate: -1) ignores signal rates and executes once per call
  auto oneShotState = strenv.allocateSharedState("OneShotDom");
  ASSERT_NE(oneShotState.get(), nullptr);
  void *oneShotArgs[] = {oneShotState.get()};
  strenv.invoke("OneShotDom_init", oneShotArgs);
  strenv.invoke("OneShotDom_process", oneShotArgs);
  EXPECT_EQ(strenv
                .getStateVar(oneShotState.get(), "OneShotFast", std::nullopt,
                             "OneShotDom")
                .value_or(-1),
            1);
  EXPECT_EQ(strenv
                .getStateVar(oneShotState.get(), "OneShotSlow", std::nullopt,
                             "OneShotDom")
                .value_or(-1),
            1);
}

} // namespace
