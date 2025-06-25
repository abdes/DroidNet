//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Platforms.h>

#if defined(OXYGEN_WINDOWS)

#  include <Oxygen/Base/Windows/ComError.h>
#  include <atlcomcli.h>
#  include <windows.h>

#  include <Oxygen/Testing/GTest.h>
#  include <gmock/gmock-matchers.h>

using oxygen::windows::ComError;
using oxygen::windows::ThrowOnFailed;

namespace oxygen::windows {

class ComErrorTest : public testing::Test {
protected:
  void SetUp() override
  {
    // Setup code if needed
  }

  void TearDown() override
  {
    // Cleanup code if needed
  }
};

NOLINT_TEST_F(ComErrorTest, ComErrorThrowsWithMessage)
{
  const std::string msg { "Test COM error" };
  try {
    ComError::Throw(static_cast<ComErrorEnum>(E_FAIL), msg);
  } catch (const ComError& e) {
    EXPECT_EQ(e.code().value(), E_FAIL);
    EXPECT_THAT(e.what(), ::testing::StartsWith(msg));
  }
}

NOLINT_TEST_F(ComErrorTest, ComErrorThrowsWithoutMessage)
{
  try {
    ComError::Throw(static_cast<ComErrorEnum>(E_FAIL), "");
  } catch (const ComError& e) {
    EXPECT_EQ(e.code().value(), E_FAIL);
    EXPECT_THAT(e.what(), testing::HasSubstr("Unspecified error"));
  }
}

NOLINT_TEST_F(ComErrorTest, ThrowOnFailedThrowsComError)
{
  constexpr HRESULT hr = E_FAIL;
  try {
    ThrowOnFailed(hr, "Operation failed");
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
    reinterpret_cast<void**>(
      &p_error_info)); // NOLINT(*-pro-type-reinterpret-cast)
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
  } catch (const ComError& e) {
    EXPECT_EQ(e.code().value(), hr);
    EXPECT_THAT(e.what(), testing::HasSubstr("Custom COM error description"));
  }

  // Un-initialize COM library
  CoUninitialize();
}

template <typename T>
class ThrowOnFailedStringTypeTest : public testing::Test { };

using StringTypes = testing::Types<const char*, const char8_t*, const wchar_t*>;

TYPED_TEST_SUITE(ThrowOnFailedStringTypeTest, StringTypes);

// NOLINTNEXTLINE
TYPED_TEST(ThrowOnFailedStringTypeTest, HandlesDifferentStringTypes)
{
  HRESULT hr = E_FAIL;
  auto message = reinterpret_cast<TypeParam>("Operation failed");
  try {
    ThrowOnFailed(hr, message);
  } catch (const ComError& e) {
    EXPECT_EQ(e.code().value(), hr);
  }
}

} // namespace oxygen::windows

#endif // OXYGEN_WINDOWS
