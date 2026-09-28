//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <exception>
#include <filesystem>
#include <map>
#include <mutex>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/Internal/ImportSourceSnapshot.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/TaskCancelledException.h>

namespace oxygen::content::import::detail {

auto ImportSourceSnapshot::MakeReadKey(
  const std::filesystem::path& path, const ReadOptions options) -> ReadKey
{
  if (path.empty()) {
    throw std::invalid_argument("Source path is empty");
  }
  // Keep the authored path: resolving its symlink once would hide retargeting.
  return { std::filesystem::absolute(path).lexically_normal(), options.offset,
    options.max_bytes };
}

auto ImportSourceSnapshot::BeginRead(const std::filesystem::path& path) -> void
{
  const std::scoped_lock lock(mutex_);
  if (sealed_ || inconsistent_) {
    throw std::runtime_error("Input collection is closed: " + path.string());
  }
  ++active_reads_;
}

auto ImportSourceSnapshot::EndRead() noexcept -> void
{
  const std::scoped_lock lock(mutex_);
  --active_reads_;
}

auto ImportSourceSnapshot::RecordDigest(
  ReadKey key, const base::Sha256Digest& digest) -> void
{
  const std::scoped_lock lock(mutex_);
  const auto [found, inserted] = reads_.try_emplace(std::move(key), digest);
  if (!inserted && found->second != digest) {
    inconsistent_ = true;
    throw std::runtime_error("Source changed between consumed reads: "
      + std::get<0>(found->first).string());
  }
}

auto ImportSourceSnapshot::ReadFile(
  const std::filesystem::path& path, const ReadOptions options)
  -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>>
{
  try {
    auto key = MakeReadKey(path, options);
    BeginRead(path);
    const auto read_guard = ScopeGuard([this] noexcept -> void { EndRead(); });
    auto result = co_await reader_->ReadFile(std::get<0>(key), options);
    if (!result) {
      co_return result;
    }
    auto hashed = co_await pool_->Run(
      [bytes = std::move(result).value()](co::ThreadPool::CancelToken) mutable
        -> std::pair<std::vector<std::byte>, base::Sha256Digest> {
        const auto digest = base::ComputeSha256(bytes);
        return std::pair { std::move(bytes), digest };
      });
    RecordDigest(std::move(key), hashed.second);
    co_return Result<std::vector<std::byte>, FileErrorInfo>::Ok(
      std::move(hashed.first));
  } catch (const co::TaskCancelledException&) {
    co_return Result<std::vector<std::byte>, FileErrorInfo>::Err(FileErrorInfo {
      .code = FileError::kCancelled,
      .path = path,
      .system_error = std::make_error_code(std::errc::operation_canceled),
      .message = "Source read cancelled",
    });
  } catch (const std::exception& error) {
    co_return Result<std::vector<std::byte>, FileErrorInfo>::Err(FileErrorInfo {
      .code = FileError::kIOError,
      .path = path,
      .system_error = std::make_error_code(std::errc::state_not_recoverable),
      .message = error.what(),
    });
  }
}

auto ImportSourceSnapshot::RecordConsumed(const std::filesystem::path& path,
  const std::span<const std::byte> bytes, const ReadOptions options) -> void
{
  auto key = MakeReadKey(path, options);
  BeginRead(path);
  const auto read_guard = ScopeGuard([this] noexcept -> void { EndRead(); });
  RecordDigest(std::move(key), base::ComputeSha256(bytes));
}

auto ImportSourceSnapshot::GetFileInfo(const std::filesystem::path& path)
  -> co::Co<Result<FileInfo, FileErrorInfo>>
{
  co_return co_await reader_->GetFileInfo(path);
}

auto ImportSourceSnapshot::Exists(const std::filesystem::path& path)
  -> co::Co<Result<bool, FileErrorInfo>>
{
  co_return co_await reader_->Exists(path);
}

auto ImportSourceSnapshot::Verify() -> co::Co<>
{
  std::map<ReadKey, base::Sha256Digest> reads;
  {
    const std::scoped_lock lock(mutex_);
    if (active_reads_ != 0U || inconsistent_) {
      throw std::runtime_error(
        "Cannot publish incomplete or inconsistent source reads");
    }
    sealed_ = true;
    reads = reads_;
  }
  for (const auto& [key, expected] : reads) {
    const auto& [path, offset, maximum] = key;
    auto bytes = co_await reader_->ReadFile(path,
      ReadOptions {
        .offset = offset,
        .max_bytes = maximum,
        .size_hint = 0U,
        .alignment = 0U,
      });
    if (!bytes) {
      throw std::runtime_error(
        "Source became unavailable before publication: " + path.string());
    }
    const auto actual = co_await pool_->Run(
      [data = std::move(bytes).value()](
        co::ThreadPool::CancelToken) -> base::Sha256Digest {
        return base::ComputeSha256(data);
      });
    if (actual != expected) {
      throw std::runtime_error(
        "Source changed before publication: " + path.string());
    }
  }
}

} // namespace oxygen::content::import::detail
