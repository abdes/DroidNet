//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <system_error>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Host/PersistenceLock.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::host::PersistenceLock;

class PersistenceLockTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    folder_ = std::filesystem::temp_directory_path()
      / ("oxygen-persistence-" + oxygen::Uuid::Generate().ToString());
  }

  auto TearDown() -> void override
  {
    auto error = std::error_code {};
    std::filesystem::remove_all(folder_, error);
  }

  std::filesystem::path folder_;
};

//! The first claim owns the folder; another fails while the owner holds it,
//! and succeeds once the owner releases it.
NOLINT_TEST_F(PersistenceLockTest, OneOwnerAtATime)
{
  auto owner = std::optional<PersistenceLock> {};
  owner.emplace(folder_);
  ASSERT_TRUE(owner->IsOwner());
  EXPECT_FALSE(PersistenceLock(folder_).IsOwner());

  owner.reset();
  EXPECT_TRUE(PersistenceLock(folder_).IsOwner());
}

//! The claim's lock file exists only while the claim is held.
NOLINT_TEST_F(PersistenceLockTest, ReleasingTheClaimRemovesItsFile)
{
  {
    const auto owner = PersistenceLock(folder_);
    ASSERT_TRUE(owner.IsOwner());
    EXPECT_FALSE(std::filesystem::is_empty(folder_));
  }
  EXPECT_TRUE(std::filesystem::is_empty(folder_));
}

} // namespace
