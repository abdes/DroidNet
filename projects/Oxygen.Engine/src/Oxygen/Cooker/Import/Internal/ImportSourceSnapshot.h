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
#include <mutex>
#include <span>
#include <tuple>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import::detail {

//! Records the bytes actually read by the importer and verifies they still
//! match before publication. Inputs are neither copied to disk nor rewritten.
class ImportSourceSnapshot final : public IAsyncFileReader {
public:
  ImportSourceSnapshot(IAsyncFileReader& reader, co::ThreadPool& pool)
    : reader_(&reader)
    , pool_(&pool)
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

  //! Seal input collection and recheck consumed bytes immediately before CAS.
  auto Verify() -> co::Co<>;

private:
  using ReadKey = std::tuple<std::filesystem::path, uint64_t, uint64_t>;
  auto BeginRead(const std::filesystem::path& path) -> void;
  auto EndRead() noexcept -> void;
  auto RecordDigest(ReadKey key, const base::Sha256Digest& digest) -> void;
  [[nodiscard]] static auto MakeReadKey(
    const std::filesystem::path& path, ReadOptions options) -> ReadKey;

  observer_ptr<IAsyncFileReader> reader_;
  observer_ptr<co::ThreadPool> pool_;
  std::mutex mutex_;
  std::map<ReadKey, base::Sha256Digest> reads_;
  std::size_t active_reads_ = 0;
  bool sealed_ = false;
  bool inconsistent_ = false;
};

} // namespace oxygen::content::import::detail
