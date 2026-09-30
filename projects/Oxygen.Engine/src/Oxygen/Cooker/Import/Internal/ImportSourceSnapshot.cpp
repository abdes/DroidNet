//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <mutex>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportSourceObservation.h>
#include <Oxygen/Cooker/Import/Internal/ImportSourceSnapshot.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/TaskCancelledException.h>

namespace oxygen::content::import::detail {

ImportSourceSnapshot::ParserRead::ParserRead(ImportSourceSnapshot& owner,
  std::filesystem::path logical_path, std::filesystem::path physical_path)
  : owner_(&owner)
  , logical_path_(std::move(logical_path))
  , physical_path_(std::move(physical_path))
{
}

ImportSourceSnapshot::ParserRead::ParserRead(ParserRead&& other) noexcept
  : owner_(std::exchange(other.owner_, observer_ptr<ImportSourceSnapshot> {}))
  , logical_path_(std::move(other.logical_path_))
  , physical_path_(std::move(other.physical_path_))
  , recorded_(other.recorded_)
{
}

ImportSourceSnapshot::ParserRead::~ParserRead()
{
  if (owner_) {
    if (!recorded_ && owner_->captured_inputs_) {
      owner_->Invalidate();
    }
    owner_->EndRead();
  }
}

auto ImportSourceSnapshot::ParserRead::Record(
  const std::span<const std::byte> bytes, const ReadOptions options) -> void
{
  owner_->RecordDigest(
    MakeReadKey(logical_path_, options), base::ComputeSha256(bytes));
  recorded_ = true;
}

auto ImportSourceSnapshot::BeginParserRead(const std::filesystem::path& path)
  -> ParserRead
{
  auto logical = NormalizePath(path);
  BeginRead(logical);
  try {
    auto physical = logical;
    if (const auto* input = CaptureFor(logical); input != nullptr) {
      if (!input->exists) {
        static_cast<void>(RecordObservation(logical,
          Observation {
            .exists = false, .metadata = {}, .content_read = false }));
        throw std::filesystem::filesystem_error(
          "Source was absent when captured", logical,
          std::make_error_code(std::errc::no_such_file_or_directory));
      }
      if (!input->file.has_value()) {
        Invalidate();
        throw std::runtime_error(
          "Parser read was not captured: " + logical.string());
      }
      physical = input->file->path;
    }
    return ParserRead(*this, std::move(logical), std::move(physical));
  } catch (...) {
    EndRead();
    throw;
  }
}

auto ImportSourceSnapshot::NormalizePath(const std::filesystem::path& path)
  -> std::filesystem::path
{
  if (path.empty()) {
    throw std::invalid_argument("Source path is empty");
  }
  // Resolving a symlink once would hide later retargeting.
  return std::filesystem::absolute(path).lexically_normal();
}

auto ImportSourceSnapshot::RecordPreparation(
  const std::span<const ImportSourceObservation> inputs) -> void
{
  for (const auto& input : inputs) {
    const auto path = NormalizePath(input.path);
    BeginRead(path);
    auto release = ScopeGuard([this]() noexcept { EndRead(); });
    if (!RecordObservation(path,
          Observation { .exists = input.exists,
            .metadata = input.metadata,
            .content_read = !input.reads.empty() })) {
      throw std::runtime_error(
        "Conflicting source preparation observations: " + path.string());
    }
    for (const auto& read : input.reads) {
      RecordDigest(ReadKey { path, read.offset, read.max_bytes }, read.digest);
    }
  }
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
  try {
    accessed_paths_.insert(path);
  } catch (...) {
    inconsistent_ = true;
    access_tracking_failed_ = true;
    throw;
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
  try {
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
  } catch (...) {
    inconsistent_ = true;
    throw;
  }
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
  if (const auto* input = CaptureFor(std::get<0>(key)); input != nullptr) {
    if (!input->file.has_value()) {
      Invalidate();
      throw std::runtime_error(
        "Consumed bytes were not captured: " + std::get<0>(key).string());
    }
    const auto offset = std::get<1>(key);
    const auto maximum = std::get<2>(key);
    if (offset == 0U && (maximum == 0U || maximum >= input->file->size)
      && digest != input->file->digest) {
      Invalidate();
      throw std::runtime_error(
        "Captured source digest mismatch: " + std::get<0>(key).string());
    }
  }
  const std::scoped_lock lock(mutex_);
  try {
    const auto [found, inserted] = reads_.try_emplace(std::move(key), digest);
    const auto& path = std::get<0>(found->first);
    if ((!inserted && found->second != digest)
      || !RecordObservationNoLock(path,
        Observation { .exists = true, .metadata = {}, .content_read = true })) {
      inconsistent_ = true;
      throw std::runtime_error(
        "Source changed between consumed observations: " + path.string());
    }
  } catch (...) {
    inconsistent_ = true;
    throw;
  }
}

auto ImportSourceSnapshot::Invalidate() noexcept -> void
{
  const std::scoped_lock lock(mutex_);
  inconsistent_ = true;
}

auto ImportSourceSnapshot::CaptureFor(const std::filesystem::path& path)
  -> const CapturedInput*
{
  if (!captured_inputs_) {
    return nullptr;
  }
  try {
    const auto* input = captured_inputs_->Find(path);
    if (input != nullptr) {
      return input;
    }
    throw std::runtime_error(
      "Undeclared captured source access: " + path.string());
  } catch (...) {
    Invalidate();
    throw;
  }
}

auto ImportSourceSnapshot::ReadSource(
  const std::filesystem::path& path, const ReadOptions options)
  -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>>
{
  const auto* input = CaptureFor(path);
  if (input == nullptr) {
    co_return co_await reader_->ReadFile(path, options);
  }
  if (!input->exists) {
    co_return Err(MakeFileError(
      path, FileError::kNotFound, "Source was absent when captured"));
  }
  if (!input->file.has_value()) {
    Invalidate();
    throw std::runtime_error(
      "Read was not captured for source: " + path.string());
  }
  auto bytes = co_await reader_->ReadFile(input->file->path, options);
  if (!bytes && bytes.error().code != FileError::kCancelled) {
    Invalidate();
    auto failure = bytes.error();
    failure.path = path;
    failure.message
      = "Captured source bytes are unavailable: " + failure.message;
    co_return Err(std::move(failure));
  }
  co_return bytes;
}

auto ImportSourceSnapshot::SourceInfo(const std::filesystem::path& path)
  -> co::Co<Result<FileInfo, FileErrorInfo>>
{
  const auto* input = CaptureFor(path);
  if (input == nullptr) {
    co_return co_await reader_->GetFileInfo(path);
  }
  if (!input->exists) {
    co_return Err(MakeFileError(
      path, FileError::kNotFound, "Source was absent when captured"));
  }
  if (!input->metadata.has_value()) {
    Invalidate();
    throw std::runtime_error(
      "Metadata was not captured for source: " + path.string());
  }
  co_return Ok(*input->metadata);
}

auto ImportSourceSnapshot::SourceExists(const std::filesystem::path& path)
  -> co::Co<Result<bool, FileErrorInfo>>
{
  const auto* input = CaptureFor(path);
  if (input == nullptr) {
    co_return co_await reader_->Exists(path);
  }
  co_return Ok(input->exists);
}

auto ImportSourceSnapshot::ReadFile(
  const std::filesystem::path& path, const ReadOptions options)
  -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>>
{
  if (path.empty()) {
    if (captured_inputs_) {
      Invalidate();
    }
    co_return Err(MakeFileError(path, FileError::kInvalidPath, "Empty path"));
  }
  try {
    auto key = MakeReadKey(path, options);
    BeginRead(std::get<0>(key));
    const auto read_guard = ScopeGuard([this] noexcept -> void { EndRead(); });
    auto result = co_await ReadSource(std::get<0>(key), options);
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
  BeginRead(std::get<0>(key));
  const auto read_guard = ScopeGuard([this] noexcept -> void { EndRead(); });
  RecordDigest(std::move(key), base::ComputeSha256(bytes));
}

auto ImportSourceSnapshot::GetFileInfo(const std::filesystem::path& path)
  -> co::Co<Result<FileInfo, FileErrorInfo>>
{
  if (path.empty()) {
    if (captured_inputs_) {
      Invalidate();
    }
    co_return Err(MakeFileError(path, FileError::kInvalidPath, "Empty path"));
  }
  try {
    const auto normalized = NormalizePath(path);
    BeginRead(normalized);
    const auto guard = ScopeGuard([this] noexcept -> void { EndRead(); });
    auto result = co_await SourceInfo(normalized);
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
    if (captured_inputs_) {
      Invalidate();
    }
    co_return Err(MakeFileError(path, FileError::kInvalidPath, "Empty path"));
  }
  try {
    const auto normalized = NormalizePath(path);
    BeginRead(normalized);
    const auto guard = ScopeGuard([this] noexcept -> void { EndRead(); });
    auto result = co_await SourceExists(normalized);
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

auto ImportSourceSnapshot::VerifyCapturedFiles() -> co::Co<>
{
  if (!captured_inputs_) {
    co_return;
  }
  constexpr uint64_t kVerificationChunkBytes = 1024U * 1024U;
  for (const auto& [path, observation] : observations_) {
    if (!observation.content_read) {
      continue;
    }
    const auto* input = CaptureFor(path);
    if (input == nullptr || !input->file.has_value()) {
      throw std::runtime_error(
        "Consumed source has no captured bytes: " + path.string());
    }
    const auto& file = *input->file;
    const auto info = co_await reader_->GetFileInfo(file.path);
    if (!info) {
      throw std::runtime_error(
        "Cannot verify captured source metadata: " + info.error().ToString());
    }
    if (info.value().is_directory || info.value().size != file.size) {
      throw std::runtime_error(
        "Captured source size or type changed: " + path.string());
    }
    auto whole_read = false;
    for (auto read = reads_.lower_bound(ReadKey { path, 0U, 0U });
      read != reads_.end() && std::get<0>(read->first) == path
      && std::get<1>(read->first) == 0U;
      ++read) {
      const auto maximum = std::get<2>(read->first);
      if (maximum == 0U || maximum >= file.size) {
        whole_read = true;
        break;
      }
    }
    if (whole_read) {
      continue;
    }
    auto hash = base::Sha256 {};
    uint64_t offset = 0;
    while (offset < file.size) {
      const auto count = std::min(kVerificationChunkBytes, file.size - offset);
      const auto bytes = co_await reader_->ReadFile(file.path,
        ReadOptions {
          .offset = offset,
          .max_bytes = count,
          .size_hint = count,
          .alignment = 0U,
        });
      if (!bytes) {
        throw std::runtime_error(
          "Cannot verify captured source: " + bytes.error().ToString());
      }
      if (bytes.value().size() != count) {
        throw std::runtime_error(
          "Captured source returned a short read: " + path.string());
      }
      hash.Update(bytes.value());
      offset += count;
    }
    if (hash.Finalize() != file.digest) {
      throw std::runtime_error(
        "Captured source digest mismatch: " + path.string());
    }
  }
}

auto ImportSourceSnapshot::Verify() -> co::Co<>
{
  {
    const std::scoped_lock lock(mutex_);
    verified_ = false;
    sealed_ = true;
    if (verifying_ || active_reads_ != 0U || inconsistent_) {
      inconsistent_ = true;
      throw std::runtime_error(
        "Cannot publish incomplete or inconsistent source observations");
    }
    verifying_ = true;
  }
  const auto finish = ScopeGuard([this] noexcept {
    const std::scoped_lock lock(mutex_);
    verifying_ = false;
  });
  // Sealing with no active operations makes both collections immutable.
  for (const auto& [path, observation] : observations_) {
    if (observation.metadata) {
      const auto actual = co_await SourceInfo(path);
      if (!actual) {
        throw std::runtime_error(
          "Cannot verify source metadata: " + actual.error().ToString());
      }
      if (actual.value() != *observation.metadata) {
        throw std::runtime_error(
          "Source metadata changed before publication: " + path.string());
      }
    } else if (!observation.content_read) {
      const auto actual = co_await SourceExists(path);
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
    auto bytes = co_await ReadSource(path,
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
  co_await VerifyCapturedFiles();
  const std::scoped_lock lock(mutex_);
  if (inconsistent_) {
    throw std::runtime_error(
      "Source verification was invalidated by an overlapping operation");
  }
  verified_ = true;
}

auto ImportSourceSnapshot::AccessedPaths() const
  -> std::vector<std::filesystem::path>
{
  const std::scoped_lock lock(mutex_);
  if (access_tracking_failed_) {
    throw std::runtime_error("Source access tracking failed");
  }
  return { accessed_paths_.begin(), accessed_paths_.end() };
}

auto ImportSourceSnapshot::Observations() const
  -> std::vector<ImportSourceObservation>
{
  const std::scoped_lock lock(mutex_);
  if (!verified_ || verifying_ || inconsistent_ || active_reads_ != 0U) {
    throw std::logic_error("Source observations have not been verified");
  }
  auto result = std::vector<ImportSourceObservation> {};
  result.reserve(observations_.size());
  auto read = reads_.begin();
  for (const auto& [path, observation] : observations_) {
    auto& value = result.emplace_back(ImportSourceObservation {
      .path = path,
      .exists = observation.exists,
      .metadata = observation.metadata,
      .reads = {},
    });
    while (read != reads_.end() && std::get<0>(read->first) == path) {
      value.reads.push_back({
        .offset = std::get<1>(read->first),
        .max_bytes = std::get<2>(read->first),
        .digest = read->second,
      });
      ++read;
    }
  }
  return result;
}

} // namespace oxygen::content::import::detail
