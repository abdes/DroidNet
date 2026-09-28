//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportSourceSnapshot.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  class MutableSourceReader final : public IAsyncFileReader {
  public:
    std::string content { "abcdef" };
    bool block { false };
    bool started { false };
    bool denied { false };
    co::ParkingLot blocked_reads {};

    auto ReadFile(const std::filesystem::path& path, const ReadOptions options)
      -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> override
    {
      started = true;
      if (std::exchange(block, false)) {
        co_await blocked_reads.Park();
      }
      if (denied) {
        co_return Err(FileErrorInfo {
          .code = FileError::kAccessDenied,
          .path = path,
          .system_error = std::make_error_code(std::errc::permission_denied),
          .message = "Read denied",
        });
      }
      const auto bytes = std::as_bytes(std::span(content));
      const auto offset = std::min<std::size_t>(options.offset, bytes.size());
      const auto count = options.max_bytes == 0U
        ? bytes.size() - offset
        : std::min<std::size_t>(options.max_bytes, bytes.size() - offset);
      const auto selected = bytes.subspan(offset, count);
      co_return Ok(std::vector<std::byte>(selected.begin(), selected.end()));
    }

    auto GetFileInfo(const std::filesystem::path&)
      -> co::Co<Result<FileInfo, FileErrorInfo>> override
    {
      co_return Ok(FileInfo {
        .size = content.size(),
        .last_modified = {},
        .is_directory = false,
        .is_symlink = false,
      });
    }
    auto Exists(const std::filesystem::path&)
      -> co::Co<Result<bool, FileErrorInfo>> override
    {
      co_return Ok(true);
    }
  };

  class ImportSourceSnapshotTest : public ::testing::Test {
  protected:
    ImportEventLoop loop_ {};
    co::ThreadPool pool_ { loop_, 1U };
    MutableSourceReader reader_ {};
    detail::ImportSourceSnapshot snapshot_ { reader_, pool_ };
    const std::filesystem::path source_ { "source.bin" };
  };

  NOLINT_TEST_F(ImportSourceSnapshotTest, MutationAfterReadRejectsPublication)
  {
    co::Run(loop_, [&] -> co::Co<> {
      const auto bytes = co_await snapshot_.ReadFile(source_);
      EXPECT_TRUE(bytes);
      reader_.content.at(0) = 'z';
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, VerificationUsesOnlyConsumedRange)
  {
    co::Run(loop_, [&] -> co::Co<> {
      const auto bytes = co_await snapshot_.ReadFile(source_,
        ReadOptions {
          .offset = 1U,
          .max_bytes = 3U,
          .size_hint = 0U,
          .alignment = 0U,
        });
      EXPECT_TRUE(bytes);
      reader_.content.at(0) = 'z';
      reader_.content += "not consumed";
      co_await snapshot_.Verify();
      const auto late = co_await snapshot_.ReadFile(source_);
      EXPECT_FALSE(late);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ParserConsumedRangeDetectsMutation)
  {
    const auto bytes
      = std::as_bytes(std::span(reader_.content)).subspan(1U, 3U);
    snapshot_.RecordConsumed(source_, bytes,
      ReadOptions {
        .offset = 1U,
        .max_bytes = 3U,
        .size_hint = 0U,
        .alignment = 0U,
      });
    reader_.content.at(2) = 'z';
    EXPECT_THROW(co::Run(loop_, snapshot_.Verify()), std::runtime_error);
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ConflictingRepeatedReadsStayRejected)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.ReadFile(source_));
      reader_.content.at(0) = 'z';
      EXPECT_FALSE(co_await snapshot_.ReadFile(source_));
      reader_.content.at(0) = 'a';
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, CancellationReleasesActiveRead)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.block = true;
      static_cast<void>(
        co_await co::AnyOf(snapshot_.ReadFile(source_), co::kYield));
      EXPECT_TRUE(reader_.started);
      EXPECT_TRUE(co_await snapshot_.ReadFile(source_));
      co_await snapshot_.Verify();
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ReadFailurePreservesOriginalError)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.denied = true;
      const auto result = co_await snapshot_.ReadFile(source_);
      if (result) {
        ADD_FAILURE() << "A failed source read cannot supply bytes";
        co_return;
      }
      EXPECT_EQ(result.error().code, FileError::kAccessDenied);
      EXPECT_EQ(result.error().system_error, std::errc::permission_denied);
      EXPECT_EQ(result.error().message, "Read denied");
    });
  }
} // namespace
} // namespace oxygen::content::import::test
