//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace oxygen::testing {

//! Fixture owning one `ScopedTempDir` per test.
/*!
 Use it only where the code under test needs real files: anything that takes
 a file port, such as a Serio device, is tested in memory.

 ```cpp
 class MyTest : public oxygen::testing::TempDirTest { };

 NOLINT_TEST_F(MyTest, WritesTheFile)
 {
   const auto path = TempPath("out/data.bin");
   ...
 }
 ```
*/
class TempDirTest : public ::testing::Test {
protected:
  //! The test's directory.
  [[nodiscard]] auto TempDir() const noexcept -> const std::filesystem::path&
  {
    return dir_.Path();
  }

  //! `TempDir() / relative`, with its parent directories created.
  [[nodiscard]] auto TempPath(const std::string_view relative) const
    -> std::filesystem::path
  {
    return dir_.File(relative);
  }

private:
  ScopedTempDir dir_;
};

} // namespace oxygen::testing
