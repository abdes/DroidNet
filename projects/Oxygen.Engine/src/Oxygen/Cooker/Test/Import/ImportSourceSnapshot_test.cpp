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
#include <string_view>
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
#include <Oxygen/OxCo/Event.h>
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
    bool present { true };
    FileInfo metadata {};
    std::size_t content_reads = 0U;
    std::size_t metadata_reads = 0U;
    std::size_t presence_reads = 0U;
    co::ParkingLot blocked_reads {};
    co::Event operation_started {};

    auto ReadFile(const std::filesystem::path& path, const ReadOptions options)
      -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> override
    {
      ++content_reads;
      co_await WaitIfBlocked();
      if (denied) {
        co_return Err(FileErrorInfo {
          .code = FileError::kAccessDenied,
          .path = path,
          .system_error = std::make_error_code(std::errc::permission_denied),
          .message = "Read denied",
        });
      }
      if (!present) {
        co_return Err(MakeFileError(
          path, std::make_error_code(std::errc::no_such_file_or_directory)));
      }
      const auto bytes = std::as_bytes(std::span(content));
      const auto offset = std::min<std::size_t>(options.offset, bytes.size());
      const auto count = options.max_bytes == 0U
        ? bytes.size() - offset
        : std::min<std::size_t>(options.max_bytes, bytes.size() - offset);
      const auto selected = bytes.subspan(offset, count);
      co_return Ok(std::vector<std::byte>(selected.begin(), selected.end()));
    }

    auto GetFileInfo(const std::filesystem::path& path)
      -> co::Co<Result<FileInfo, FileErrorInfo>> override
    {
      ++metadata_reads;
      co_await WaitIfBlocked();
      if (denied) {
        co_return Err(
          MakeFileError(path, FileError::kAccessDenied, "Metadata denied"));
      }
      if (!present) {
        co_return Err(MakeFileError(
          path, std::make_error_code(std::errc::no_such_file_or_directory)));
      }
      auto info = metadata;
      info.size = content.size();
      co_return Ok(info);
    }

    auto Exists(const std::filesystem::path& path)
      -> co::Co<Result<bool, FileErrorInfo>> override
    {
      ++presence_reads;
      co_await WaitIfBlocked();
      if (denied) {
        co_return Err(
          MakeFileError(path, FileError::kAccessDenied, "Presence denied"));
      }
      co_return Ok(present);
    }

  private:
    auto WaitIfBlocked() -> co::Co<>
    {
      started = true;
      operation_started.Trigger();
      if (std::exchange(block, false)) {
        co_await blocked_reads.Park();
      }
    }
  };

  class ImportSourceSnapshotTest : public ::testing::Test {
  protected:
    ImportEventLoop loop_ {};
    co::ThreadPool pool_ { loop_, 1U };
    MutableSourceReader reader_ {};
    detail::ImportSourceSnapshot snapshot_ { reader_, pool_ };
    const std::filesystem::path source_ { "source.bin" };

    auto RejectIncompleteVerification() -> co::Co<>
    {
      co_await reader_.operation_started;
      EXPECT_TRUE(reader_.started);
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
      co_return;
    }
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
  NOLINT_TEST_F(ImportSourceSnapshotTest, EmptyPathsPreserveInvalidPathErrors)
  {
    co::Run(loop_, [&] -> co::Co<> {
      const std::filesystem::path empty {};
      const auto bytes = co_await snapshot_.ReadFile(empty);
      const auto info = co_await snapshot_.GetFileInfo(empty);
      const auto exists = co_await snapshot_.Exists(empty);
      if (bytes || info || exists) {
        ADD_FAILURE() << "Empty paths cannot supply source observations";
        co_return;
      }
      EXPECT_EQ(bytes.error().code, FileError::kInvalidPath);
      EXPECT_EQ(info.error().code, FileError::kInvalidPath);
      EXPECT_EQ(exists.error().code, FileError::kInvalidPath);
      EXPECT_EQ(reader_.content_reads, 0U);
      EXPECT_EQ(reader_.metadata_reads, 0U);
      EXPECT_EQ(reader_.presence_reads, 0U);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, MissingProbeDetectsFileAppearance)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.present = false;
      const auto exists = co_await snapshot_.Exists(source_);
      EXPECT_TRUE(exists);
      EXPECT_FALSE(exists.value());
      reader_.present = true;
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, MissingReadDetectsFileAppearance)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.present = false;
      const auto read = co_await snapshot_.ReadFile(source_);
      EXPECT_FALSE(read);
      EXPECT_EQ(read.error().code, FileError::kNotFound);
      reader_.present = true;
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, MissingMetadataDetectsFileAppearance)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.present = false;
      const auto info = co_await snapshot_.GetFileInfo(source_);
      EXPECT_FALSE(info);
      EXPECT_EQ(info.error().code, FileError::kNotFound);
      reader_.present = true;
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, PresenceOnlyDoesNotReadOrHashContent)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.Exists(source_));
      EXPECT_TRUE(co_await snapshot_.Exists(source_));
      reader_.content += "unobserved contents";
      co_await snapshot_.Verify();
      EXPECT_EQ(reader_.presence_reads, 3U);
      EXPECT_EQ(reader_.metadata_reads, 0U);
      EXPECT_EQ(reader_.content_reads, 0U);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, MetadataOnlyDoesNotReadOrHashContent)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.GetFileInfo(source_));
      reader_.content.at(0) = 'z';
      co_await snapshot_.Verify();
      EXPECT_EQ(reader_.metadata_reads, 2U);
      EXPECT_EQ(reader_.content_reads, 0U);
      EXPECT_EQ(reader_.presence_reads, 0U);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ConsumedContentSubsumesPresenceCheck)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.Exists(source_));
      EXPECT_TRUE(co_await snapshot_.ReadFile(source_));
      co_await snapshot_.Verify();
      EXPECT_EQ(reader_.presence_reads, 1U);
      EXPECT_EQ(reader_.content_reads, 2U);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, MetadataSizeMutationRejectsPublication)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.GetFileInfo(source_));
      reader_.content += "changed size";
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, MetadataTimestampMutationRejectsPublication)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.GetFileInfo(source_));
      reader_.metadata.last_modified
        += std::filesystem::file_time_type::duration(1);
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, MetadataTypeMutationRejectsPublication)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.GetFileInfo(source_));
      reader_.metadata.is_directory = true;
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, MetadataDoesNotReplaceContentVerification)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.GetFileInfo(source_));
      EXPECT_TRUE(co_await snapshot_.ReadFile(source_));
      reader_.content.at(0) = 'z';
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, ContradictoryAbsenceAndReadStayRejected)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.present = false;
      EXPECT_TRUE(co_await snapshot_.Exists(source_));
      reader_.present = true;
      EXPECT_FALSE(co_await snapshot_.ReadFile(source_));
      reader_.present = false;
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, ContradictoryMissingReadPreservesNotFound)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.Exists(source_));
      reader_.present = false;
      const auto result = co_await snapshot_.ReadFile(source_);
      EXPECT_FALSE(result);
      EXPECT_EQ(result.error().code, FileError::kNotFound);
      EXPECT_EQ(
        result.error().system_error, std::errc::no_such_file_or_directory);
      reader_.present = true;
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ContradictoryMetadataStaysRejected)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.GetFileInfo(source_));
      reader_.metadata.is_symlink = true;
      EXPECT_FALSE(co_await snapshot_.GetFileInfo(source_));
      reader_.metadata.is_symlink = false;
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, PresenceCancellationReleasesActiveProbe)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.block = true;
      static_cast<void>(
        co_await co::AnyOf(snapshot_.Exists(source_), co::kYield));
      EXPECT_TRUE(reader_.started);
      EXPECT_TRUE(co_await snapshot_.Exists(source_));
      co_await snapshot_.Verify();
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, MetadataCancellationReleasesActiveProbe)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.block = true;
      static_cast<void>(
        co_await co::AnyOf(snapshot_.GetFileInfo(source_), co::kYield));
      EXPECT_TRUE(reader_.started);
      EXPECT_TRUE(co_await snapshot_.GetFileInfo(source_));
      co_await snapshot_.Verify();
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, SuspendedPresenceProbePreventsSealing)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.block = true;
      static_cast<void>(co_await co::AnyOf(
        snapshot_.Exists(source_), RejectIncompleteVerification()));
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
      EXPECT_FALSE(co_await snapshot_.Exists(source_));
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, SuspendedMetadataProbePreventsSealing)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.block = true;
      static_cast<void>(co_await co::AnyOf(
        snapshot_.GetFileInfo(source_), RejectIncompleteVerification()));
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
      EXPECT_FALSE(co_await snapshot_.GetFileInfo(source_));
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, SealingRejectsAllLaterProbes)
  {
    co::Run(loop_, [&] -> co::Co<> {
      co_await snapshot_.Verify();
      EXPECT_FALSE(co_await snapshot_.Exists(source_));
      EXPECT_FALSE(co_await snapshot_.GetFileInfo(source_));
      EXPECT_EQ(reader_.presence_reads, 0U);
      EXPECT_EQ(reader_.metadata_reads, 0U);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ProbeFailuresPreserveAccessDenied)
  {
    co::Run(loop_, [&] -> co::Co<> {
      reader_.denied = true;
      const auto exists = co_await snapshot_.Exists(source_);
      const auto info = co_await snapshot_.GetFileInfo(source_);
      EXPECT_FALSE(exists);
      EXPECT_FALSE(info);
      EXPECT_EQ(exists.error().code, FileError::kAccessDenied);
      EXPECT_EQ(info.error().code, FileError::kAccessDenied);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, VerificationPreservesProbeFailureDetail)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.Exists(source_));
      reader_.denied = true;
      try {
        co_await snapshot_.Verify();
        ADD_FAILURE()
          << "A failed verification probe cannot permit publication";
      } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string_view(error.what()).find("Presence denied"),
          std::string_view::npos);
      }
    });
  }
} // namespace
} // namespace oxygen::content::import::test
