//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

#include "DummyIndex.h"

using oxygen::content::lc::ValidateRoot;

namespace {

class ValidationTest : public testing::Test {
protected:
  void SetUp() override
  {
    temp_dir_
      = std::filesystem::temp_directory_path() / "oxygen_validation_test";
    std::filesystem::create_directories(temp_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(temp_dir_); }

  auto TempDir() const -> const std::filesystem::path& { return temp_dir_; }

private:
  std::filesystem::path
    temp_dir_; // NOLINT(misc-non-private-member-variables-in-classes)
};

NOLINT_TEST_F(ValidationTest, ValidateRootValidSucceeds)
{
  oxygen::content::lc::testing::CreateDummyIndex(
    TempDir() / "container.index.bin", 1);
  EXPECT_NO_THROW({ ValidateRoot(TempDir()); });
}

NOLINT_TEST_F(ValidationTest, ValidateRootInvalidThrowsException)
{
  constexpr uint16_t kInvalidVersion = 999;
  oxygen::content::lc::testing::CreateDummyIndex(
    TempDir() / "container.index.bin", kInvalidVersion);
  EXPECT_THROW({ ValidateRoot(TempDir()); }, std::exception);
}

} // namespace
