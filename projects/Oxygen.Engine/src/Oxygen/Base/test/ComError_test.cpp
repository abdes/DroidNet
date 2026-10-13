//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <system_error>

#include "gtest/gtest.h"
#include <combaseapi.h>
#include <oaidl.h>
#include <objbase.h>
#include <oleauto.h>

#include <Oxygen/Base/Platforms.h>

#ifdef OXYGEN_WINDOWS

#  include <windows.h> // IWYU pragma: keep

#  include <atlcomcli.h>

#  include <Oxygen/Base/Windows/ComError.h>
#  include <Oxygen/Testing/GTest.h>

using oxygen::windows::ComError;

namespace oxygen::windows {

class ComErrorTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    // Setup code if needed
  }

  auto TearDown() -> void override
  {
    // Cleanup code if needed
  }
};

NOLINT_TEST_F(ComErrorTest, ComErrorThrowsWithMessage)
{
  const std::string msg { "Test COM error" };
  bool thrown = false;
  try {
    ComError::Throw(static_cast<ComErrorEnum>(E_FAIL), msg);
  } catch (const ComError& e) {
    thrown = true;
    EXPECT_EQ(e.code().value(), E_FAIL);
    EXPECT_THAT(e.what(), ::testing::StartsWith(msg));
  }

  EXPECT_TRUE(thrown) << "expected ComError::Throw to throw";
}

NOLINT_TEST_F(ComErrorTest, ComErrorThrowsWithoutMessage)
{
  bool thrown = false;
  try {
    ComError::Throw(static_cast<ComErrorEnum>(E_FAIL), "");
  } catch (const ComError& e) {
    thrown = true;
    EXPECT_EQ(e.code().value(), E_FAIL);
    EXPECT_THAT(e.what(), testing::HasSubstr("Unspecified error"));
  }

  EXPECT_TRUE(thrown) << "expected ComError::Throw to throw";
}

NOLINT_TEST_F(ComErrorTest, ThrowOnFailedThrowsComError)
{
  constexpr HRESULT hr = E_FAIL;
  try {
    ThrowOnFailed(hr, "Operation failed");
    FAIL() << "expected ThrowOnFailed to throw";
  } catch (const ComError& e) {
    EXPECT_EQ(e.code().value(), hr);
    // No description is available for the error, falls back to message from
    // category
    EXPECT_THAT(e.what(), testing::HasSubstr("Unspecified error"));
  }
}

NOLINT_TEST_F(ComErrorTest, ThrowOnFailedDoesNotThrowOnSuccess)
{
  constexpr HRESULT hr = S_OK;
  // NOLINTNEXTLINE
  EXPECT_NO_THROW(ThrowOnFailed(hr, "Operation succeeded"));
}

NOLINT_TEST_F(ComErrorTest, ComErrorWithIErrorInfo)
{
  // Initialize COM library
  HRESULT hr = CoInitialize(nullptr);
  ASSERT_TRUE(SUCCEEDED(hr));

  // Create a custom IErrorInfo
  CComPtr<ICreateErrorInfo> p_create_error_info;
  hr = CreateErrorInfo(&p_create_error_info);
  ASSERT_TRUE(SUCCEEDED(hr));

  CComPtr<IErrorInfo> p_error_info;
  hr = p_create_error_info->QueryInterface(IID_IErrorInfo,
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    reinterpret_cast<void**>(&p_error_info));
  ASSERT_TRUE(SUCCEEDED(hr));

  // Set error description
  const CComBSTR description(L"Custom COM error description");
  hr = p_create_error_info->SetDescription(description);
  ASSERT_TRUE(SUCCEEDED(hr));

  // Set the error info for the current thread
  hr = SetErrorInfo(0, p_error_info);
  ASSERT_TRUE(SUCCEEDED(hr));

  // Simulate a COM error
  hr = E_FAIL;

  // Check if the error is captured correctly
  try {
    ThrowOnFailed(hr, "Failed operation");
    FAIL() << "expected ThrowOnFailed to throw";
  } catch (const ComError& e) {
    EXPECT_EQ(e.code().value(), hr);
    EXPECT_THAT(e.what(), testing::HasSubstr("Custom COM error description"));
  }

  // Un-initialize COM library
  CoUninitialize();
}

template <typename /*T*/>
class ThrowOnFailedStringTypeTest : public testing::Test { };

using StringTypes = testing::Types<const char*, const char8_t*, const wchar_t*>;

TYPED_TEST_SUITE(ThrowOnFailedStringTypeTest, StringTypes);

// NOLINTNEXTLINE
TYPED_TEST(ThrowOnFailedStringTypeTest, HandlesDifferentStringTypes)
{
  HRESULT hr = E_FAIL;
  // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
  auto message = reinterpret_cast<TypeParam>("Operation failed");
  try {
    ThrowOnFailed(hr, message);
    FAIL() << "expected ThrowOnFailed to throw";
  } catch (const ComError& e) {
    EXPECT_EQ(e.code().value(), hr);
  }
}

//! Regression: a narrow (UTF-8) message must reach the log, not be dropped
//! by the nullable-detection branch.
NOLINT_TEST_F(ComErrorTest, ThrowOnFailedLogsNarrowMessage)
{
  testing::internal::CaptureStderr();
  bool thrown = false;
  try {
    ThrowOnFailed(E_FAIL, "narrow context message");
  } catch (const ComError& e) {
    thrown = true;
    EXPECT_EQ(e.code().value(), static_cast<int>(E_FAIL));
  }
  const auto captured = testing::internal::GetCapturedStderr();

  EXPECT_TRUE(thrown) << "expected ThrowOnFailed to throw";
  EXPECT_THAT(captured, testing::HasSubstr("narrow context message"));
}

//! Regression: a wide message is converted and reaches the log.
NOLINT_TEST_F(ComErrorTest, ThrowOnFailedLogsWideMessage)
{
  testing::internal::CaptureStderr();
  bool thrown = false;
  try {
    ThrowOnFailed(E_FAIL, L"wide context message");
  } catch (const ComError& e) {
    thrown = true;
    EXPECT_EQ(e.code().value(), static_cast<int>(E_FAIL));
  }
  const auto captured = testing::internal::GetCapturedStderr();

  EXPECT_TRUE(thrown) << "expected ThrowOnFailed to throw";
  EXPECT_THAT(captured, testing::HasSubstr("wide context message"));
}

//! HRESULTs from non-Win32 facilities must not masquerade as system errors.
NOLINT_TEST_F(ComErrorTest, NonWin32FacilityConditionStaysInComCategory)
{
  // RPC_E_CHANGED_MODE carries the RPC facility (1), not Win32 (7). Its
  // low-order code (258) would otherwise be looked up as a win32 error.
  const ComError error { static_cast<ComErrorEnum>(RPC_E_CHANGED_MODE) };
  const auto condition = error.code().default_error_condition();

  EXPECT_NE(&condition.category(), &std::system_category());
  EXPECT_EQ(&condition.category(), &ComCategory());
  EXPECT_EQ(condition.value(), static_cast<int>(RPC_E_CHANGED_MODE));
}

//! Win32-facility HRESULTs keep mapping onto their system error condition.
NOLINT_TEST_F(ComErrorTest, Win32FacilityConditionMapsToSystemCategory)
{
  constexpr HRESULT hr = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
  const ComError error { static_cast<ComErrorEnum>(hr) };
  const auto condition = error.code().default_error_condition();

  // system_category() translates a win32 code to its standardized generic
  // condition; the COM category must not swallow it.
  EXPECT_NE(&condition.category(), &ComCategory());
  EXPECT_EQ(condition, std::errc::permission_denied);
}

//! The is_error_code_enum trait specializes the real oxygen::windows type,
//! so implicit error_code construction from the enum works.
NOLINT_TEST_F(ComErrorTest, ComErrorEnumIsErrorCodeEnum)
{
  static_assert(std::is_error_code_enum_v<ComErrorEnum>);

  const std::error_code ec = static_cast<ComErrorEnum>(E_FAIL);
  EXPECT_EQ(ec.value(), static_cast<int>(E_FAIL));
  EXPECT_EQ(&ec.category(), &ComCategory());
}

} // namespace oxygen::windows

#endif // OXYGEN_WINDOWS
