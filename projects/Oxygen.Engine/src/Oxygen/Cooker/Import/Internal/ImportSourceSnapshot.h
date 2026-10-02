//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportSourceObservation.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import::detail {

//! Records consumed bytes and input probes, then verifies those same facts
//! before publication. Inputs are neither copied to disk nor rewritten.
class ImportSourceSnapshot final : public IAsyncFileReader {
public:
  //! Scoped synchronous parser access; the snapshot outlives the returned
  //! lease.
  class ParserRead final {
  public:
    ~ParserRead();
    OXYGEN_MAKE_NON_COPYABLE(ParserRead)
    ParserRead(ParserRead&& other) noexcept;
    auto operator=(ParserRead&&) -> ParserRead& = delete;

    [[nodiscard]] auto PhysicalPath() const noexcept
      -> const std::filesystem::path&
    {
      return physical_path_;
    }
    auto Record(std::span<const std::byte> bytes, ReadOptions options = {})
      -> void;

  private:
    friend class ImportSourceSnapshot;
    ParserRead(ImportSourceSnapshot& owner, std::filesystem::path logical_path,
      std::filesystem::path physical_path);
    observer_ptr<ImportSourceSnapshot> owner_;
    std::filesystem::path logical_path_;
    std::filesystem::path physical_path_;
    bool recorded_ = false;
  };

  ImportSourceSnapshot(IAsyncFileReader& reader, co::ThreadPool& pool,
    std::shared_ptr<const CapturedInputSet> captured_inputs = {})
    : reader_(&reader)
    , pool_(&pool)
    , captured_inputs_(std::move(captured_inputs))
  {
  }
  ~ImportSourceSnapshot() override = default;
  OXYGEN_MAKE_NON_COPYABLE(ImportSourceSnapshot)
  OXYGEN_MAKE_NON_MOVABLE(ImportSourceSnapshot)

  auto ReadFile(const std::filesystem::path& path, ReadOptions options = {})
    -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> override;
  auto GetFileInfo(const std::filesystem::path& path)
    -> co::Co<Result<FileInfo, FileErrorInfo>> override;
  auto Exists(const std::filesystem::path& path)
    -> co::Co<Result<bool, FileErrorInfo>> override;
  //! Hash a parser-owned buffer on its worker, without rereading or copying it.
  auto RecordConsumed(const std::filesystem::path& path,
    std::span<const std::byte> bytes, ReadOptions options = {}) -> void;

  //! Inherit preparation facts as pending reads; Verify still rechecks them.
  auto RecordPreparation(std::span<const ImportSourceObservation> inputs)
    -> void;

  [[nodiscard]] auto BeginParserRead(const std::filesystem::path& path)
    -> ParserRead;

  //! Seal input collection and recheck consumed bytes immediately before CAS.
  auto Verify() -> co::Co<>;

  //! Copy verified facts for analysis reports; unverified observations are
  //! rejected.
  [[nodiscard]] auto Observations() const
    -> std::vector<ImportSourceObservation>;

  //! Attempted paths for output protection, independent of proof verification.
  [[nodiscard]] auto AccessedPaths() const
    -> std::vector<std::filesystem::path>;

private:
  auto CaptureFor(const std::filesystem::path& path) -> const CapturedInput*;
  auto Invalidate() noexcept -> void;
  auto ReadSource(const std::filesystem::path& path, ReadOptions options)
    -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>>;
  auto SourceInfo(const std::filesystem::path& path)
    -> co::Co<Result<FileInfo, FileErrorInfo>>;
  auto SourceExists(const std::filesystem::path& path)
    -> co::Co<Result<bool, FileErrorInfo>>;
  auto VerifyCapturedFiles() -> co::Co<>;

  struct Observation {
    bool exists = false;
    std::optional<FileInfo> metadata {};
    bool content_read = false;
  };

  auto RecordObservation(
    const std::filesystem::path& path, const Observation& observation) -> bool;
  auto RecordObservationNoLock(
    const std::filesystem::path& path, const Observation& observation) -> bool;
  [[nodiscard]] static auto NormalizePath(const std::filesystem::path& path)
    -> std::filesystem::path;

  using ReadKey = std::tuple<std::filesystem::path, uint64_t, uint64_t>;
  auto BeginRead(const std::filesystem::path& path) -> void;
  auto EndRead() noexcept -> void;
  auto RecordDigest(ReadKey key, const base::Sha256Digest& digest) -> void;
  [[nodiscard]] static auto MakeReadKey(
    const std::filesystem::path& path, ReadOptions options) -> ReadKey;

  observer_ptr<IAsyncFileReader> reader_;
  observer_ptr<co::ThreadPool> pool_;
  std::shared_ptr<const CapturedInputSet> captured_inputs_;
  mutable std::mutex mutex_;
  std::map<ReadKey, base::Sha256Digest> reads_;
  std::map<std::filesystem::path, Observation> observations_;
  std::set<std::filesystem::path> accessed_paths_;
  std::size_t active_reads_ = 0;
  bool sealed_ = false;
  bool inconsistent_ = false;
  bool verified_ = false;
  bool verifying_ = false;
  bool access_tracking_failed_ = false;
};

} // namespace oxygen::content::import::detail
