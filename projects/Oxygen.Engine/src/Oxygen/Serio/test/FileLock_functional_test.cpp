//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/FileLock.h, Serio/FileLock.cpp

#include <atomic>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <thread>
#include <utility>

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <sddl.h>
#endif

namespace oxygen::serio::testing {
namespace {

  class FileLockTest : public ::testing::Test {
  protected:
    oxygen::testing::ScopedTempDir dir_;
    const std::filesystem::path marker_ = dir_.Path() / "lease.lock";
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

  //! A shared holder that releases leaves the lock to the holders that
  //! remain: an exclusive contender stays excluded until the last holder
  //! releases, which removes the file. While a holder remains, the file may
  //! not be visible (Windows marks it for deletion), so only exclusion is
  //! checked then.
  NOLINT_TEST_F(FileLockTest, TemporarySharedLockStaysWhileAnotherHolderRemains)
  {
    auto first = FileLock::TryAcquire(
      marker_, FileLockMode::kShared, FileLockOpenMode::kTemporary);
    ASSERT_HAS_VALUE(first);
    auto second = FileLock::TryAcquire(
      marker_, FileLockMode::kShared, FileLockOpenMode::kTemporary);
    ASSERT_HAS_VALUE(second);

    *first = FileLock {};
    const auto contender = FileLock::TryAcquire(
      marker_, FileLockMode::kExclusive, FileLockOpenMode::kTemporary);
    *second = FileLock {};
    const auto removed = !std::filesystem::exists(marker_);

    ASSERT_FALSE(contender);
    EXPECT_EQ(contender.error(),
      std::make_error_code(std::errc::device_or_resource_busy));
    EXPECT_TRUE(removed);
    EXPECT_TRUE(FileLock::TryAcquire(
      marker_, FileLockMode::kExclusive, FileLockOpenMode::kTemporary));
  }

  //! Denies the caller every access to `path`, then gives it back when the
  //! test ends, so the folder can be removed.
  class DeniedAccess final {
  public:
    explicit DeniedAccess(std::filesystem::path path)
      : path_(std::move(path))
    {
      Set(kDenyAll);
    }
    ~DeniedAccess() { Set(kAllowAll); }
    DeniedAccess(const DeniedAccess&) = delete;
    DeniedAccess(DeniedAccess&&) = delete;
    auto operator=(const DeniedAccess&) -> DeniedAccess& = delete;
    auto operator=(DeniedAccess&&) -> DeniedAccess& = delete;

  private:
#ifdef OXYGEN_WINDOWS
    static constexpr auto kDenyAll = L"D:P(D;;FA;;;WD)";
    static constexpr auto kAllowAll = L"D:P(A;;FA;;;WD)";

    auto Set(const wchar_t* sddl) const noexcept -> void
    {
      PSECURITY_DESCRIPTOR descriptor = nullptr;
      if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &descriptor, nullptr)
        != FALSE) {
        static_cast<void>(SetFileSecurityW(
          path_.c_str(), DACL_SECURITY_INFORMATION, descriptor));
        LocalFree(descriptor);
      }
    }
#else
    static constexpr auto kDenyAll = std::filesystem::perms::none;
    static constexpr auto kAllowAll = std::filesystem::perms::owner_all;

    auto Set(const std::filesystem::perms perms) const noexcept -> void
    {
      auto ignored = std::error_code {};
      std::filesystem::permissions(path_, perms, ignored);
    }
#endif

    std::filesystem::path path_;
  };

  //! A temporary lock whose file the caller may not open fails with the
  //! denial, never with contention a caller would retry.
  NOLINT_TEST_F(FileLockTest, TemporaryLockOnADeniedFileIsAccessDenied)
  {
    {
      auto creator = FileLock::TryAcquire(
        marker_, FileLockMode::kExclusive, FileLockOpenMode::kOpenOrCreate);
      ASSERT_HAS_VALUE(creator);
    }
    const auto denied = DeniedAccess(marker_);

    const auto lock = FileLock::TryAcquire(
      marker_, FileLockMode::kShared, FileLockOpenMode::kTemporary);

    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error(), Error::kAccessDenied);
    EXPECT_NE(lock.error(), std::errc::device_or_resource_busy);
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

  //! Acquire waits: a second exclusive acquisition completes only after the
  //! first lock is released.
  NOLINT_TEST_F(FileLockTest, AcquireWaitsUntilTheHolderReleases)
  {
    using namespace std::chrono_literals;
    auto holder = FileLock::Acquire(
      marker_, FileLockMode::kExclusive, FileLockOpenMode::kOpenOrCreate);
    ASSERT_TRUE(holder);
    std::atomic<bool> acquired { false };
    auto waiter_result = Result<FileLock> { FileLock {} };

    auto waiter = std::thread([&] {
      waiter_result = FileLock::Acquire(marker_, FileLockMode::kExclusive);
      acquired.store(true);
    });
    std::this_thread::sleep_for(100ms);
    const auto acquired_while_held = acquired.load();
    holder.value() = FileLock {};
    waiter.join();

    EXPECT_FALSE(acquired_while_held);
    EXPECT_TRUE(acquired.load());
    ASSERT_TRUE(waiter_result);
    EXPECT_TRUE(waiter_result->IsLocked());
  }

  //! Each acquisition opens its own handle, so a lock taken by Acquire
  //! excludes a TryAcquire of the same process.
  NOLINT_TEST_F(FileLockTest, TryAcquireAfterAcquireInTheSameProcessIsBusy)
  {
    auto holder = FileLock::Acquire(
      marker_, FileLockMode::kExclusive, FileLockOpenMode::kOpenOrCreate);
    ASSERT_TRUE(holder);

    const auto contender
      = FileLock::TryAcquire(marker_, FileLockMode::kExclusive);

    ASSERT_FALSE(contender);
    EXPECT_EQ(contender.error(),
      std::make_error_code(std::errc::device_or_resource_busy));
  }

  //! A temporary lock file cannot be waited for.
  NOLINT_TEST_F(FileLockTest, AcquireRefusesATemporaryLock)
  {
    const auto lock = FileLock::Acquire(
      marker_, FileLockMode::kExclusive, FileLockOpenMode::kTemporary);

    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error(), std::errc::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(marker_));
  }

  //! Shared acquisitions do not wait for each other.
  NOLINT_TEST_F(FileLockTest, SharedAcquisitionsCoexist)
  {
    auto first = FileLock::Acquire(
      marker_, FileLockMode::kShared, FileLockOpenMode::kOpenOrCreate);
    ASSERT_TRUE(first);

    const auto second = FileLock::Acquire(marker_, FileLockMode::kShared);

    ASSERT_TRUE(second);
    EXPECT_TRUE(first->IsLocked());
    EXPECT_TRUE(second->IsLocked());
  }

} // namespace
} // namespace oxygen::serio::testing
