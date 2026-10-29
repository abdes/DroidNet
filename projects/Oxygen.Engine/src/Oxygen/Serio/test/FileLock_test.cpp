//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {
namespace {

  class FileLockTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      root_
        = std::filesystem::temp_directory_path() / Uuid::Generate().ToString();
      std::filesystem::create_directories(root_);
      marker_ = root_ / "lease.lock";
    }

    void TearDown() override
    {
      std::error_code error;
      std::filesystem::remove_all(root_, error);
    }

    std::filesystem::path root_;
    std::filesystem::path marker_;
  };

  NOLINT_TEST_F(FileLockTest, MissingExistingMarkerIsNotCreated)
  {
    const auto result = FileLock::TryAcquire(marker_, FileLockMode::kShared);
    EXPECT_FALSE(result);
    EXPECT_FALSE(std::filesystem::exists(marker_));
    EXPECT_NE(result.error(), std::errc::device_or_resource_busy);
  }

  NOLINT_TEST_F(FileLockTest, SharedReadersExcludeWriterUntilEveryReaderLeaves)
  {
    auto first = FileLock::TryAcquire(
      marker_, FileLockMode::kShared, FileLockOpenMode::kOpenOrCreate);
    ASSERT_TRUE(first);
    auto second = FileLock::TryAcquire(marker_, FileLockMode::kShared);
    ASSERT_TRUE(second);
    auto writer = FileLock::TryAcquire(marker_, FileLockMode::kExclusive);
    ASSERT_FALSE(writer);
    EXPECT_EQ(writer.error(), std::errc::device_or_resource_busy);
    // The lock moves with its owner: the reader keeps excluding the writer.
    auto moved = std::move(first).value();
    EXPECT_TRUE(moved.IsLocked());
    moved = FileLock {};
    EXPECT_FALSE(FileLock::TryAcquire(marker_, FileLockMode::kExclusive));
    second.value() = FileLock {};
    EXPECT_TRUE(FileLock::TryAcquire(marker_, FileLockMode::kExclusive));
  }

  //! A temporary lock's file exists only while it is held.
  NOLINT_TEST_F(FileLockTest, TemporaryLockRemovesItsFileOnRelease)
  {
    {
      auto owner = FileLock::TryAcquire(
        marker_, FileLockMode::kExclusive, FileLockOpenMode::kTemporary);
      ASSERT_TRUE(owner);
      EXPECT_TRUE(std::filesystem::exists(marker_));
      const auto contender = FileLock::TryAcquire(
        marker_, FileLockMode::kExclusive, FileLockOpenMode::kTemporary);
      ASSERT_FALSE(contender);
      EXPECT_EQ(contender.error(),
        std::make_error_code(std::errc::device_or_resource_busy));
    }
    EXPECT_FALSE(std::filesystem::exists(marker_));
    auto next = FileLock::TryAcquire(
      marker_, FileLockMode::kExclusive, FileLockOpenMode::kTemporary);
    EXPECT_TRUE(next);
  }

  //! A lock opened to create its file keeps the file after release.
  NOLINT_TEST_F(FileLockTest, OpenOrCreateLockKeepsItsFile)
  {
    {
      auto owner = FileLock::TryAcquire(
        marker_, FileLockMode::kExclusive, FileLockOpenMode::kOpenOrCreate);
      ASSERT_TRUE(owner);
    }
    EXPECT_TRUE(std::filesystem::exists(marker_));
  }

  NOLINT_TEST_F(FileLockTest, ExclusiveOwnerExcludesOtherReadersAndWriters)
  {
    auto owner = FileLock::TryAcquire(
      marker_, FileLockMode::kExclusive, FileLockOpenMode::kOpenOrCreate);
    ASSERT_TRUE(owner);
    EXPECT_FALSE(FileLock::TryAcquire(marker_, FileLockMode::kShared));
    EXPECT_FALSE(FileLock::TryAcquire(marker_, FileLockMode::kExclusive));
    owner.value() = FileLock {};
    EXPECT_TRUE(FileLock::TryAcquire(marker_, FileLockMode::kShared));
  }

} // namespace
} // namespace oxygen::serio::testing
