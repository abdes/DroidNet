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

auto ImportSourceSnapshot::NormalizePath(const std::filesystem::path& path)
  -> std::filesystem::path
{
  if (path.empty()) {
    throw std::invalid_argument("Source path is empty");
  }
  // Resolving a symlink once would hide later retargeting.
  return std::filesystem::absolute(path).lexically_normal();
}

auto ImportSourceSnapshot::MakeReadKey(
  const std::filesystem::path& path, const ReadOptions options) -> ReadKey
{
  return { NormalizePath(path), options.offset, options.max_bytes };
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

auto ImportSourceSnapshot::RecordObservationNoLock(
  const std::filesystem::path& path, const Observation& observation) -> bool
{
  if (inconsistent_) {
    return false;
  }
  const auto [position, inserted]
    = observations_.try_emplace(path, observation);
  if (!inserted) {
    auto& previous = position->second;
    if (previous.exists != observation.exists
      || (previous.metadata && observation.metadata
        && *previous.metadata != *observation.metadata)) {
      inconsistent_ = true;
      return false;
    }
    if (observation.metadata) {
      previous.metadata = observation.metadata;
    }
    previous.content_read = previous.content_read || observation.content_read;
  }
  return true;
}

auto ImportSourceSnapshot::RecordObservation(
  const std::filesystem::path& path, const Observation& observation) -> bool
{
  const std::scoped_lock lock(mutex_);
  return RecordObservationNoLock(path, observation);
}

auto ImportSourceSnapshot::RecordDigest(
  ReadKey key, const base::Sha256Digest& digest) -> void
{
  const std::scoped_lock lock(mutex_);
  const auto [found, inserted] = reads_.try_emplace(std::move(key), digest);
  const auto& path = std::get<0>(found->first);
  if ((!inserted && found->second != digest)
    || !RecordObservationNoLock(path,
      Observation { .exists = true, .metadata = {}, .content_read = true })) {
    inconsistent_ = true;
    throw std::runtime_error(
      "Source changed between consumed observations: " + path.string());
  }
}

auto ImportSourceSnapshot::ReadFile(
  const std::filesystem::path& path, const ReadOptions options)
  -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>>
{
  if (path.empty()) {
    co_return Err(MakeFileError(path, FileError::kInvalidPath, "Empty path"));
  }
  try {
    auto key = MakeReadKey(path, options);
    BeginRead(path);
    const auto read_guard = ScopeGuard([this] noexcept -> void { EndRead(); });
    auto result = co_await reader_->ReadFile(std::get<0>(key), options);
    if (!result) {
      if (result.error().code == FileError::kNotFound) {
        // Keep the underlying error even when this contradicts an earlier
        // probe.
        static_cast<void>(RecordObservation(std::get<0>(key),
          Observation {
            .exists = false, .metadata = {}, .content_read = false }));
      }
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
  } catch (const std::filesystem::filesystem_error& error) {
    co_return Err(MakeFileError(path, error.code()));
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
  if (path.empty()) {
    co_return Err(MakeFileError(path, FileError::kInvalidPath, "Empty path"));
  }
  try {
    const auto normalized = NormalizePath(path);
    BeginRead(normalized);
    const auto guard = ScopeGuard([this] noexcept -> void { EndRead(); });
    auto result = co_await reader_->GetFileInfo(normalized);
    if (!result) {
      if (result.error().code == FileError::kNotFound) {
        static_cast<void>(RecordObservation(normalized,
          Observation {
            .exists = false, .metadata = {}, .content_read = false }));
      }
      co_return result;
    }
    if (!RecordObservation(normalized,
          Observation { .exists = true,
            .metadata = result.value(),
            .content_read = false })) {
      throw std::runtime_error(
        "Source metadata changed between observations: " + normalized.string());
    }
    co_return result;
  } catch (const co::TaskCancelledException&) {
    co_return Err(FileErrorInfo {
      .code = FileError::kCancelled,
      .path = path,
      .system_error = std::make_error_code(std::errc::operation_canceled),
      .message = "Source metadata read cancelled",
    });
  } catch (const std::filesystem::filesystem_error& error) {
    co_return Err(MakeFileError(path, error.code()));
  } catch (const std::exception& error) {
    co_return Err(FileErrorInfo {
      .code = FileError::kIOError,
      .path = path,
      .system_error = std::make_error_code(std::errc::state_not_recoverable),
      .message = error.what(),
    });
  }
}

auto ImportSourceSnapshot::Exists(const std::filesystem::path& path)
  -> co::Co<Result<bool, FileErrorInfo>>
{
  if (path.empty()) {
    co_return Err(MakeFileError(path, FileError::kInvalidPath, "Empty path"));
  }
  try {
    const auto normalized = NormalizePath(path);
    BeginRead(normalized);
    const auto guard = ScopeGuard([this] noexcept -> void { EndRead(); });
    auto result = co_await reader_->Exists(normalized);
    if (!result) {
      if (result.error().code == FileError::kNotFound) {
        static_cast<void>(RecordObservation(normalized,
          Observation {
            .exists = false, .metadata = {}, .content_read = false }));
      }
      co_return result;
    }
    if (!RecordObservation(normalized,
          Observation { .exists = result.value(),
            .metadata = {},
            .content_read = false })) {
      throw std::runtime_error(
        "Source presence changed between observations: " + normalized.string());
    }
    co_return result;
  } catch (const co::TaskCancelledException&) {
    co_return Err(FileErrorInfo {
      .code = FileError::kCancelled,
      .path = path,
      .system_error = std::make_error_code(std::errc::operation_canceled),
      .message = "Source presence check cancelled",
    });
  } catch (const std::filesystem::filesystem_error& error) {
    co_return Err(MakeFileError(path, error.code()));
  } catch (const std::exception& error) {
    co_return Err(FileErrorInfo {
      .code = FileError::kIOError,
      .path = path,
      .system_error = std::make_error_code(std::errc::state_not_recoverable),
      .message = error.what(),
    });
  }
}

auto ImportSourceSnapshot::Verify() -> co::Co<>
{
  {
    const std::scoped_lock lock(mutex_);
    sealed_ = true;
    if (active_reads_ != 0U || inconsistent_) {
      inconsistent_ = true;
      throw std::runtime_error(
        "Cannot publish incomplete or inconsistent source observations");
    }
  }
  // Sealing with no active operations makes both collections immutable.
  for (const auto& [path, observation] : observations_) {
    if (observation.metadata) {
      const auto actual = co_await reader_->GetFileInfo(path);
      if (!actual) {
        throw std::runtime_error(
          "Cannot verify source metadata: " + actual.error().ToString());
      }
      if (actual.value() != *observation.metadata) {
        throw std::runtime_error(
          "Source metadata changed before publication: " + path.string());
      }
    } else if (!observation.content_read) {
      const auto actual = co_await reader_->Exists(path);
      if (!actual) {
        throw std::runtime_error(
          "Cannot verify source presence: " + actual.error().ToString());
      }
      if (actual.value() != observation.exists) {
        throw std::runtime_error(
          "Source presence changed before publication: " + path.string());
      }
    }
  }
  for (const auto& [key, expected] : reads_) {
    const auto& [path, offset, maximum] = key;
    auto bytes = co_await reader_->ReadFile(path,
      ReadOptions {
        .offset = offset,
        .max_bytes = maximum,
        .size_hint = 0U,
        .alignment = 0U,
      });
    if (!bytes) {
      throw std::runtime_error("Source became unavailable before publication: "
        + bytes.error().ToString());
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
