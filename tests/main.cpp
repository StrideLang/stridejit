#include "gtest/gtest.h"
#include "stride/utils/logger.h"

class BufferedLogTestListener : public ::testing::EmptyTestEventListener {
public:
  void OnTestStart(const ::testing::TestInfo &) override {
    strd::Logger::startBuffering();
  }

  void OnTestEnd(const ::testing::TestInfo &testInfo) override {
    if (testInfo.result()->Failed()) {
      std::cerr << "\n[STRIDE LOGS FOR FAILED TEST: "
                << testInfo.test_suite_name() << "." << testInfo.name()
                << "]\n";
      strd::Logger::flushBuffer(&std::cerr);
      std::cerr << "[END STRIDE LOGS]\n";
    } else {
      strd::Logger::clearBuffer();
    }
    strd::Logger::stopBuffering();
  }
};

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::TestEventListeners &listeners =
      ::testing::UnitTest::GetInstance()->listeners();
  listeners.Append(new BufferedLogTestListener());
  return RUN_ALL_TESTS();
}

