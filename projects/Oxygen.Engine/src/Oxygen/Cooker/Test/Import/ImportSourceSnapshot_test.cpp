//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportSourceSnapshot.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
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
    bool metadata_denied { false };
    std::size_t deny_read_number = 0U;
    bool present { true };
    FileInfo metadata {};
    std::size_t content_reads = 0U;
    std::size_t metadata_reads = 0U;
    std::size_t presence_reads = 0U;
    std::filesystem::path last_read_path;
    std::filesystem::path last_info_path;
    co::Event release_reads {};
    co::Event operation_started {};

    auto ReadFile(const std::filesystem::path& path, const ReadOptions options)
      -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> override
    {
      last_read_path = path;
      ++content_reads;
      co_await WaitIfBlocked();
      if (denied || content_reads == deny_read_number) {
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
      last_info_path = path;
      ++metadata_reads;
      co_await WaitIfBlocked();
      if (denied || metadata_denied) {
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
        co_await release_reads;
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

    auto CapturedFile(const std::string_view content = "abcdef") const
      -> CapturedInput
    {
      return CapturedInput {
        .logical_path = std::filesystem::absolute(source_).lexically_normal(),
        .exists = true,
        .metadata = FileInfo { .size = content.size(), .last_modified = {},
          .is_directory = false, .is_symlink = false },
        .file = CapturedInputFile {
          .path = std::filesystem::absolute("captured.bin").lexically_normal(),
          .size = content.size(), .digest = base::ComputeSha256(std::as_bytes(std::span(content))),
        },
      };
    }

    static auto CaptureMap(const CapturedInput& input)
      -> std::shared_ptr<const CapturedInputSet>
    {
      return std::make_shared<const CapturedInputSet>(std::span(&input, 1));
    }

    auto RejectFirstVerification() -> co::Co<>
    {
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    }

    auto RejectOverlappingVerification() -> co::Co<>
    {
      co_await reader_.operation_started;
      EXPECT_THROW(
        static_cast<void>(snapshot_.Observations()), std::logic_error);
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
      reader_.release_reads.Trigger();
    }

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
  NOLINT_TEST_F(ImportSourceSnapshotTest, ExportsOnlyVerifiedConsumedRanges)
  {
    EXPECT_THROW(static_cast<void>(snapshot_.Observations()), std::logic_error);
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.ReadFile(source_,
        ReadOptions {
          .offset = 1,
          .max_bytes = 3,
          .size_hint = 0,
          .alignment = 0,
        }));
      EXPECT_THROW(
        static_cast<void>(snapshot_.Observations()), std::logic_error);
      co_await snapshot_.Verify();
      const auto observations = snapshot_.Observations();
      EXPECT_EQ(observations.size(), 1U);
      if (observations.size() != 1U) {
        co_return;
      }
      const auto& observation = observations.front();
      EXPECT_EQ(observation.path,
        std::filesystem::absolute(source_).lexically_normal());
      EXPECT_TRUE(observation.exists);
      EXPECT_EQ(observation.reads.size(), 1U);
      if (observation.reads.size() != 1U) {
        co_return;
      }
      EXPECT_EQ(observation.reads.front().offset, 1U);
      EXPECT_EQ(observation.reads.front().max_bytes, 3U);
      EXPECT_EQ(observation.reads.front().digest,
        base::ComputeSha256(
          std::as_bytes(std::span(reader_.content)).subspan(1, 3)));
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ProbeReportDoesNotReadContent)
  {
    reader_.present = false;
    co::Run(loop_, [&] -> co::Co<> {
      const auto exists = co_await snapshot_.Exists(source_);
      EXPECT_TRUE(exists.has_value());
      if (!exists.has_value()) {
        co_return;
      }
      EXPECT_FALSE(exists.value());
      co_await snapshot_.Verify();
      const auto observations = snapshot_.Observations();
      EXPECT_EQ(observations.size(), 1U);
      if (observations.size() != 1U) {
        co_return;
      }
      EXPECT_FALSE(observations.front().exists);
      EXPECT_TRUE(observations.front().reads.empty());
      EXPECT_FALSE(observations.front().metadata.has_value());
      EXPECT_EQ(reader_.content_reads, 0U);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, FailedReverificationInvalidatesReport)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.ReadFile(source_));
      co_await snapshot_.Verify();
      EXPECT_EQ(snapshot_.Observations().size(), 1U);
      reader_.content.at(0) = 'z';
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
      EXPECT_THROW(
        static_cast<void>(snapshot_.Observations()), std::logic_error);
    });
  }
  NOLINT_TEST_F(
    ImportSourceSnapshotTest, OverlappingVerificationCannotExportProofs)
  {
    snapshot_.RecordConsumed(
      source_, std::as_bytes(std::span(reader_.content)));
    reader_.block = true;
    co::Run(loop_,
      co::AllOf(RejectFirstVerification(), RejectOverlappingVerification()));
    EXPECT_THROW(static_cast<void>(snapshot_.Observations()), std::logic_error);
  }
  NOLINT_TEST_F(
    ImportSourceSnapshotTest, AccessInventorySurvivesFailedVerification)
  {
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await snapshot_.ReadFile(source_));
      reader_.content.at(0) = 'z';
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
      EXPECT_THROW(
        static_cast<void>(snapshot_.Observations()), std::logic_error);
      EXPECT_EQ(snapshot_.AccessedPaths(),
        std::vector<std::filesystem::path> {
          std::filesystem::absolute(source_).lexically_normal() });
    });
  }
  NOLINT_TEST_F(ImportSourceSnapshotTest, CapturedReadsPreserveLogicalIdentity)
  {
    const auto input = CapturedFile();
    if (!input.file.has_value()) {
      FAIL() << "Expected input.file to contain a value";
    }
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await mapped.ReadFile(source_));
      EXPECT_EQ(reader_.last_read_path, input.file.value().path);
      co_await mapped.Verify();
      const auto facts = mapped.Observations();
      EXPECT_EQ(facts.front().path, input.logical_path);
      EXPECT_EQ(reader_.content_reads, 2U);
      EXPECT_EQ(reader_.last_info_path, input.file.value().path);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, CapturedAbsenceDoesNotConsultTheLiveSource)
  {
    const auto input
      = CapturedInput { .logical_path = std::filesystem::absolute(source_),
          .exists = false,
          .metadata = {},
          .file = {} };
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    reader_.present = true;
    co::Run(loop_, [&] -> co::Co<> {
      const auto exists = co_await mapped.Exists(source_);
      EXPECT_TRUE(exists.has_value());
      EXPECT_FALSE(exists.value());
      const auto bytes = co_await mapped.ReadFile(source_);
      EXPECT_FALSE(bytes.has_value());
      EXPECT_EQ(bytes.error().code, FileError::kNotFound);
      co_await mapped.Verify();
    });
    EXPECT_EQ(reader_.content_reads, 0U);
    EXPECT_EQ(reader_.presence_reads, 0U);
    EXPECT_EQ(reader_.metadata_reads, 0U);
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, UndeclaredReadRemainsFatalWhenItsErrorIsIgnored)
  {
    const auto input = CapturedFile();
    if (!input.file.has_value()) {
      FAIL() << "Expected input.file to contain a value";
    }
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_FALSE(co_await mapped.ReadFile("not-declared.bin"));
      EXPECT_THROW(co_await mapped.Verify(), std::runtime_error);
    });
    EXPECT_EQ(reader_.content_reads, 0U);
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ProbeAdmissionDoesNotPermitByteReads)
  {
    auto input = CapturedFile();
    input.file.reset();
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await mapped.Exists(source_));
      EXPECT_FALSE(co_await mapped.ReadFile(source_));
      EXPECT_THROW(co_await mapped.Verify(), std::runtime_error);
    });
    EXPECT_EQ(reader_.content_reads, 0U);
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, InitiallyCorruptCaptureCannotBeAccepted)
  {
    const auto input = CapturedFile();
    reader_.content = "zbcdef";
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_FALSE(co_await mapped.ReadFile(source_));
      EXPECT_THROW(co_await mapped.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, PartialReadStillQualifiesTheCapturedFileDigest)
  {
    const auto input = CapturedFile();
    reader_.content = "zbcdef";
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await mapped.ReadFile(source_,
        ReadOptions {
          .offset = 1U,
          .max_bytes = 2U,
          .size_hint = 0U,
          .alignment = 0U,
        }));
      EXPECT_THROW(co_await mapped.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, BoundedWholeReadRejectsAppendedCapturedBytes)
  {
    const auto input = CapturedFile();
    if (!input.file.has_value()) {
      FAIL() << "Expected input.file to contain a value";
    }
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await mapped.ReadFile(source_,
        ReadOptions {
          .offset = 0U,
          .max_bytes = input.file.value().size,
          .size_hint = 0U,
          .alignment = 0U,
        }));
      reader_.content += "tail";
      EXPECT_THROW(co_await mapped.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, UnusedCapturesAreNotScannedByEachJob)
  {
    const auto input = CapturedFile();
    auto unused = CapturedFile("a different larger file");
    unused.logical_path = std::filesystem::absolute("unused.bin");
    if (!unused.file.has_value()) {
      FAIL() << "Expected unused.file to contain a value";
    }
    unused.file.value().path = std::filesystem::absolute("unused-copy.bin");
    const auto entries = std::array { input, unused };
    auto mapped = detail::ImportSourceSnapshot(reader_, pool_,
      std::make_shared<const CapturedInputSet>(std::span(entries)));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await mapped.ReadFile(source_));
      co_await mapped.Verify();
    });
    EXPECT_EQ(reader_.content_reads, 2U);
    EXPECT_EQ(reader_.metadata_reads, 1U);
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, CapturedMetadataUsesOriginalSourceFacts)
  {
    auto input = CapturedFile();
    input.metadata = FileInfo { .size = reader_.content.size(),
      .last_modified = {},
      .is_directory = false,
      .is_symlink = true };
    reader_.metadata.is_directory = true;
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      const auto info = co_await mapped.GetFileInfo(source_);
      EXPECT_TRUE(info.has_value());
      EXPECT_EQ(info.value(), input.metadata.value());
      co_await mapped.Verify();
    });
    EXPECT_EQ(reader_.metadata_reads, 0U);
    EXPECT_EQ(reader_.content_reads, 0U);
  }
  NOLINT_TEST_F(ImportSourceSnapshotTest, CapturedMetadataFailurePreservesCause)
  {
    const auto input = CapturedFile();
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await mapped.ReadFile(source_));
      reader_.metadata_denied = true;
      try {
        co_await mapped.Verify();
        ADD_FAILURE() << "Expected metadata verification failure";
      } catch (const std::runtime_error& error) {
        EXPECT_THAT(error.what(), ::testing::HasSubstr("Metadata denied"));
      }
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, CapturedChunkFailurePreservesCause)
  {
    const auto input = CapturedFile();
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_TRUE(co_await mapped.ReadFile(source_,
        ReadOptions {
          .offset = 1U, .max_bytes = 2U, .size_hint = 0U, .alignment = 0U }));
      // The range is rechecked first; the third read qualifies the whole
      // capture.
      reader_.deny_read_number = 3U;
      try {
        co_await mapped.Verify();
        ADD_FAILURE() << "Expected captured-file verification failure";
      } catch (const std::runtime_error& error) {
        EXPECT_THAT(error.what(), ::testing::HasSubstr("Read denied"));
      }
      EXPECT_EQ(reader_.content_reads, 3U);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ParserReadsPreserveLogicalIdentity)
  {
    const auto input = CapturedFile();
    if (!input.file.has_value()) {
      FAIL() << "Expected input.file to contain a value";
    }
    const auto physical_path = input.file.value().path;
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    {
      auto read = mapped.BeginParserRead(source_);
      EXPECT_EQ(read.PhysicalPath(), physical_path);
      read.Record(std::as_bytes(std::span(reader_.content)));
    }
    co::Run(loop_, [&] -> co::Co<> { co_await mapped.Verify(); });
    const auto facts = mapped.Observations();
    ASSERT_EQ(facts.size(), 1U);
    EXPECT_EQ(facts.front().path, input.logical_path);
    EXPECT_EQ(reader_.content_reads, 1U);
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ActiveParserReadPreventsVerification)
  {
    auto read = snapshot_.BeginParserRead(source_);
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, AbandonedCapturedParserReadIsFatal)
  {
    const auto input = CapturedFile();
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    {
      auto read = mapped.BeginParserRead(source_);
    }
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_THROW(co_await mapped.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, MovedParserReadRetainsSingleLease)
  {
    const auto input = CapturedFile();
    auto mapped
      = detail::ImportSourceSnapshot(reader_, pool_, CaptureMap(input));
    {
      auto original = mapped.BeginParserRead(source_);
      auto moved = std::move(original);
      moved.Record(std::as_bytes(std::span(reader_.content)));
    }
    co::Run(loop_, [&] -> co::Co<> { co_await mapped.Verify(); });
    EXPECT_EQ(mapped.Observations().size(), 1U);
  }
  NOLINT_TEST_F(
    ImportSourceSnapshotTest, PreparationFactsRemainPendingUntilVerified)
  {
    const auto proof = ImportSourceObservation {
      .path = source_,
      .exists = true,
      .metadata = {},
      .reads = { ImportSourceReadProof { .offset = 0U,
        .max_bytes = 0U,
        .digest
        = base::ComputeSha256(std::as_bytes(std::span(reader_.content))) } },
    };
    snapshot_.RecordPreparation(std::span(&proof, 1));
    EXPECT_THROW(static_cast<void>(snapshot_.Observations()), std::logic_error);
    co::Run(loop_, [&] -> co::Co<> { co_await snapshot_.Verify(); });
    EXPECT_EQ(snapshot_.Observations().size(), 1U);
    EXPECT_EQ(reader_.content_reads, 1U);
  }

  NOLINT_TEST_F(
    ImportSourceSnapshotTest, MutationAfterPreparationRejectsCompletion)
  {
    const auto proof = ImportSourceObservation {
      .path = source_,
      .exists = true,
      .metadata = {},
      .reads = { ImportSourceReadProof { .offset = 0U,
        .max_bytes = 0U,
        .digest
        = base::ComputeSha256(std::as_bytes(std::span(reader_.content))) } },
    };
    snapshot_.RecordPreparation(std::span(&proof, 1));
    reader_.content.at(0) = 'z';
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }

  NOLINT_TEST_F(ImportSourceSnapshotTest, ConflictingPreparationFactsAreFatal)
  {
    auto proof = ImportSourceObservation {
      .path = source_,
      .exists = true,
      .metadata = {},
      .reads = { ImportSourceReadProof { .offset = 0U,
        .max_bytes = 0U,
        .digest
        = base::ComputeSha256(std::as_bytes(std::span(reader_.content))) } },
    };
    snapshot_.RecordPreparation(std::span(&proof, 1));
    reader_.content.at(0) = 'z';
    proof.reads.front().digest
      = base::ComputeSha256(std::as_bytes(std::span(reader_.content)));
    EXPECT_THROW(
      snapshot_.RecordPreparation(std::span(&proof, 1)), std::runtime_error);
    co::Run(loop_, [&] -> co::Co<> {
      EXPECT_THROW(co_await snapshot_.Verify(), std::runtime_error);
    });
  }
} // namespace
} // namespace oxygen::content::import::test
