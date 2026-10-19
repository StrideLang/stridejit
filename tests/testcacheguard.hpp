#pragma once

#include "gtest/gtest.h"
#include "stride/utils/cachemanager.h"
#include "stride/utils/logger.h"
#include <chrono>
#include <filesystem>

namespace strd::test {

class DisableCacheTestListener : public ::testing::EmptyTestEventListener {
public:
  void OnTestStart(const ::testing::TestInfo &testInfo) override {
    LOG_DEBUG() << "[TestCacheGuard] Test '" << testInfo.test_suite_name()
                << "." << testInfo.name() << "' start: ensuring cache disabled"
                << std::endl;
    resetCache();
  }

  void OnTestEnd(const ::testing::TestInfo &) override {
    resetCache();
  }

private:
  static void resetCache() {
    auto &cm = CacheManager::instance();
    cm.setEnabled(false);
    cm.setCacheDirectory({});
    cm.clearCompilerBuildHashOverride();
    cm.resetStats();
  }
};

class ScopedCacheEnable {
public:
  explicit ScopedCacheEnable(bool isolateTempDir = true) {
    auto &cm = CacheManager::instance();
    m_prevEnabled = cm.isEnabled();
    m_prevCacheDir = cm.getCacheDirectory();

    if (isolateTempDir) {
      m_tempDir = std::filesystem::temp_directory_path() /
                  ("stride_test_cache_" +
                   std::to_string(std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count()));
      std::filesystem::create_directories(m_tempDir);
      cm.setCacheDirectory(m_tempDir / ".strdcache");
    }

    cm.setEnabled(true);
    cm.resetStats();
  }

  ~ScopedCacheEnable() {
    auto &cm = CacheManager::instance();
    cm.clearCache();
    cm.setCacheDirectory(m_prevCacheDir);
    cm.setEnabled(m_prevEnabled);

    if (!m_tempDir.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(m_tempDir, ec);
    }
  }

  const std::filesystem::path &tempDir() const { return m_tempDir; }

private:
  bool m_prevEnabled{false};
  std::filesystem::path m_prevCacheDir;
  std::filesystem::path m_tempDir;
};

} // namespace strd::test
