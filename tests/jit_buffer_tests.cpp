#include "gtest/gtest.h"
#include <cstdint>
#include <vector>

#include "stride/stridejit/strideenvironment.hpp"

namespace {

TEST(JITBufferTest, BufferTriggerReaction) {
  strd::StrideEnvironment strenv;
  auto ret = strenv.generateIr(STRIDEJIT_TESTS_SOURCE_DIR "buffer_trigger.stride");
  EXPECT_TRUE(ret);
  ret = strenv.compileInMemory();
  EXPECT_TRUE(ret);

  auto entrySym = strenv.lookup("BufferDom_process");
  ASSERT_TRUE(entrySym);
  auto *entry = entrySym->toPtr<void (*)(double*, double*)>();
  ASSERT_NE(entry, nullptr);

  double inVal = 0.0;
  double outResult = 0.0;

  // Samples 1..3: buffer is not full, reaction does not run -> outResult stays 0.0
  for (int i = 1; i <= 3; ++i) {
    inVal = static_cast<double>(i);
    entry(&inVal, &outResult);
    EXPECT_DOUBLE_EQ(outResult, 0.0) << "At sample " << i;
  }

  // 4th sample fills capacity 4 -> ready switch fires -> reaction computes InBuf[0] (1.0) + 10.0 = 11.0
  inVal = 4.0;
  entry(&inVal, &outResult);
  EXPECT_DOUBLE_EQ(outResult, 11.0) << "At 4th sample (buffer full)";
}

} // namespace
