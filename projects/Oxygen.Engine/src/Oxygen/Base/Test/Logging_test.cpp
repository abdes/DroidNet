//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include <Oxygen/Testing/GTest.h>

namespace {

#if LOGURU_USE_FMTLIB

class LoggingNoExceptTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    // Save verbosity to restore later
    saved_verbosity_ = loguru::g_stderr_verbosity;
  }

  auto TearDown() -> void override
  {
    // Restore verbosity to what was before
    loguru::g_stderr_verbosity = saved_verbosity_;
  }

private:
  // Set verbosity to a level that will not throw exceptions
  loguru::Verbosity saved_verbosity_ { loguru::Verbosity_OFF };
};

NOLINT_TEST_F(LoggingNoExceptTest, NoExcept)
{
  NOLINT_EXPECT_NO_THROW({ LOG_F(INFO, "Test log message"); });
}
NOLINT_TEST_F(LoggingNoExceptTest, NoExcept_NullFormatString)
{
  const char* null_fmt = nullptr;
  NOLINT_EXPECT_NO_THROW({ LOG_F(INFO, null_fmt); });
}
NOLINT_TEST_F(LoggingNoExceptTest, NoExcept_InvalidFormatSpecifier)
{
  NOLINT_EXPECT_NO_THROW({
    // fmt lib will throw on truly invalid format, but LOG_F should catch and
    // not propagate
    LOG_F(INFO, "Invalid format: {0} {1} {2} {bogus}", 42, 3.14, "test");
  });
}
NOLINT_TEST_F(LoggingNoExceptTest, NoExcept_LongMessage)
{
  NOLINT_EXPECT_NO_THROW({
    const std::string long_msg(10000, 'x');
    LOG_F(INFO, "{}", long_msg);
  });
}
NOLINT_TEST_F(LoggingNoExceptTest, NoExcept_NullptrArgument)
{
  NOLINT_EXPECT_NO_THROW({
    const char* ptr = nullptr;
    LOG_F(INFO, "Null pointer: {}", ptr);
  });
}
NOLINT_TEST_F(LoggingNoExceptTest, NoExcept_EmptyFormat)
{
  NOLINT_EXPECT_NO_THROW({ LOG_F(INFO, ""); });
}

#endif // LOGURU_USE_FMTLIB

} // namespace
