//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/Platforms.h"
#if defined(OXYGEN_WINDOWS)

#include <windows.h>

#include "Oxygen/Base/Windows/Exceptions.h"

#include <winerror.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using oxygen::windows::WindowsException;
using ::testing::StartsWith;

namespace oxygen::windows {

  TEST(WindowsExceptionTest, FromErrorCodeAndErrorCodeRetrieval) {
    constexpr DWORD error_code = ERROR_FILE_NOT_FOUND;
    const std::exception_ptr ex_ptr = WindowsException::FromErrorCode(error_code);
    try {
      std::rethrow_exception(ex_ptr);
    }
    catch (const WindowsException& ex) {
      EXPECT_EQ(ex.GetErrorCode(), error_code);
      EXPECT_EQ(ex.code().value(), static_cast<int>(error_code));
      EXPECT_EQ(ex.code().category(), std::system_category());
    }
    catch (...) {
      FAIL() << "Expected WindowsException";
    }
  }

  TEST(WindowsExceptionTest, WhatMethod) {
    constexpr DWORD error_code = ERROR_FILE_NOT_FOUND;
    const std::exception_ptr ex_ptr = WindowsException::FromErrorCode(error_code);
    try {
      std::rethrow_exception(ex_ptr);
    }
    catch (const WindowsException& ex) {
      EXPECT_THAT(ex.what(), StartsWith("2 : The system cannot find the file specified."));
    }
    catch (...) {
      FAIL() << "Expected WindowsException";
    }
  }

  TEST(WindowsExceptionTest, FromLastError) {
    ::SetLastError(ERROR_ACCESS_DENIED);
    const std::exception_ptr ex_ptr = WindowsException::FromLastError();
    try {
      std::rethrow_exception(ex_ptr);
    }
    catch (const WindowsException& ex) {
      EXPECT_EQ(ex.GetErrorCode(), ERROR_ACCESS_DENIED);
      EXPECT_EQ(ex.code().value(), static_cast<int>(ERROR_ACCESS_DENIED));
      EXPECT_EQ(ex.code().category(), std::system_category());
      EXPECT_THAT(ex.what(), StartsWith("5 : Access is denied."));
    }
    catch (...) {
      FAIL() << "Expected WindowsException";
    }
  }

  TEST(WindowsExceptionTest, FromErrorCode) {
    const std::exception_ptr ex_ptr = WindowsException::FromErrorCode(ERROR_INVALID_PARAMETER);
    try {
      std::rethrow_exception(ex_ptr);
    }
    catch (const WindowsException& ex) {
      EXPECT_EQ(ex.GetErrorCode(), ERROR_INVALID_PARAMETER);
      EXPECT_EQ(ex.code().value(), static_cast<int>(ERROR_INVALID_PARAMETER));
      EXPECT_EQ(ex.code().category(), std::system_category());
      EXPECT_THAT(ex.what(), StartsWith("87 : The parameter is incorrect."));
    }
    catch (...) {
      FAIL() << "Expected WindowsException";
    }
  }

  TEST(WindowsExceptionTest, ThrowFromLastError) {
    ::SetLastError(ERROR_ACCESS_DENIED);
    try {
      WindowsException::ThrowFromLastError();
      // ReSharper disable once CppUnreachableCode
      FAIL() << "Expected WindowsException";
    }
    catch (const WindowsException& ex) {
      EXPECT_EQ(ex.GetErrorCode(), ERROR_ACCESS_DENIED);
      EXPECT_EQ(ex.code().value(), static_cast<int>(ERROR_ACCESS_DENIED));
      EXPECT_EQ(ex.code().category(), std::system_category());
      EXPECT_THAT(ex.what(), StartsWith("5 : Access is denied."));
    }
    catch (...) {
      FAIL() << "Expected WindowsException";
    }
  }

  TEST(WindowsExceptionTest, ThrowFromErrorCode) {
    try {
      WindowsException::ThrowFromErrorCode(ERROR_INVALID_PARAMETER);
      // ReSharper disable once CppUnreachableCode
      FAIL() << "Expected WindowsException";
    }
    catch (const WindowsException& ex) {
      EXPECT_EQ(ex.GetErrorCode(), ERROR_INVALID_PARAMETER);
      EXPECT_EQ(ex.code().value(), static_cast<int>(ERROR_INVALID_PARAMETER));
      EXPECT_EQ(ex.code().category(), std::system_category());
      EXPECT_THAT(ex.what(), StartsWith("87 : The parameter is incorrect."));
    }
    catch (...) {
      FAIL() << "Expected WindowsException";
    }
  }

}  // namespace oxygen::windows

#endif // OXYGEN_WINDOWS
