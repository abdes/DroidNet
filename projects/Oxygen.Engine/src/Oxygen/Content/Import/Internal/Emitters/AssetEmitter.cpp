//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <stdexcept>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Internal/Emitters/AssetEmitter.h>

namespace oxygen::content::import {

namespace {

  //! Validate path segments don't contain `.` or `..`.
  auto ValidateNoDotSegments(
    const std::string_view path, const std::string_view what) -> void
  {
    size_t pos = 0;
    while (pos <= path.size()) {
      const auto next = path.find('/', pos);
      const auto len
        = (next == std::string_view::npos) ? (path.size() - pos) : (next - pos);
      const auto segment = path.substr(pos, len);
      if (segment == ".") {
        throw std::runtime_error(std::string(what) + " must not contain '.'");
      }
      if (segment == "..") {
        throw std::runtime_error(std::string(what) + " must not contain '..'");
      }

      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
  }

  //! Validate container-relative path (e.g., "Materials/Wood.omat").
  auto ValidateRelativePath(const std::string_view relpath) -> void
  {
    if (relpath.empty()) {
      throw std::runtime_error("Relative path must not be empty");
    }

    if (relpath.find('\\') != std::string_view::npos) {
      throw std::runtime_error("Relative path must use '/' as the separator");
    }
    if (relpath.find(':') != std::string_view::npos) {
      throw std::runtime_error("Relative path must not contain ':'");
    }
    if (relpath.front() == '/') {
      throw std::runtime_error("Relative path must be container-relative");
    }
    if (relpath.back() == '/') {
      throw std::runtime_error("Relative path must not end with '/'");
    }
    if (relpath.find("//") != std::string_view::npos) {
      throw std::runtime_error("Relative path must not contain '//'");
    }

    ValidateNoDotSegments(relpath, "Relative path");

    std::filesystem::path p(relpath);
    if (p.is_absolute() || p.has_root_path() || p.has_root_name()) {
      throw std::runtime_error("Relative path must be container-relative");
    }
  }

  //! Validate virtual path (e.g., "/.cooked/Materials/Wood").
  auto ValidateVirtualPath(const std::string_view virtual_path) -> void
  {
    if (virtual_path.empty()) {
      throw std::runtime_error("Virtual path must not be empty");
    }
    if (virtual_path.find('\\') != std::string_view::npos) {
      throw std::runtime_error("Virtual path must use '/' as the separator");
    }
    if (virtual_path.front() != '/') {
      throw std::runtime_error("Virtual path must start with '/'");
    }
    if (virtual_path.size() > 1 && virtual_path.back() == '/') {
      throw std::runtime_error(
        "Virtual path must not end with '/' (except the root)");
    }
    if (virtual_path.find("//") != std::string_view::npos) {
      throw std::runtime_error("Virtual path must not contain '//'");
    }

    ValidateNoDotSegments(virtual_path, "Virtual path");
  }

} // namespace

AssetEmitter::AssetEmitter(IAsyncFileWriter& file_writer,
  const LooseCookedLayout& layout, const std::filesystem::path& cooked_root,
  const bool compute_sha256)
  : file_writer_(file_writer)
  , cooked_root_(cooked_root)
  , compute_sha256_(compute_sha256)
{
  // layout is available for future use if needed
  (void)layout;
  DLOG_F(INFO, "AssetEmitter created: cooked_root='{}' sha256={}",
    cooked_root_.string(), compute_sha256_);
}

AssetEmitter::~AssetEmitter()
{
  const auto pending = pending_count_.load(std::memory_order_acquire);
  if (pending > 0) {
    LOG_F(WARNING, "AssetEmitter destroyed with {} pending writes", pending);
  }
}

auto AssetEmitter::Emit(const data::AssetKey& key, data::AssetType asset_type,
  std::string_view virtual_path, std::string_view descriptor_relpath,
  std::span<const std::byte> bytes) -> void
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("AssetEmitter is finalized");
  }

  // Validate paths (must match PAK format requirements)
  ValidateVirtualPath(virtual_path);
  ValidateRelativePath(descriptor_relpath);

  if (const auto it = key_by_virtual_path_.find(std::string(virtual_path));
    it != key_by_virtual_path_.end() && it->second != key) {
    throw std::runtime_error(
      "Conflicting virtual path mapping in loose cooked container");
  }

  std::optional<base::Sha256Digest> sha256;
  if (compute_sha256_) {
    sha256 = base::ComputeSha256(bytes);
  }

  // Build full path for the descriptor file
  const auto descriptor_path = cooked_root_ / descriptor_relpath;

  DLOG_F(INFO, "AssetEmitter::Emit: type={} vpath='{}' relpath='{}' size={}",
    static_cast<int>(asset_type), virtual_path, descriptor_relpath,
    bytes.size());

  RecordAsset(key, asset_type, virtual_path, descriptor_relpath,
    static_cast<uint64_t>(bytes.size()), sha256);
  QueueDescriptorWrite(descriptor_path, descriptor_relpath, bytes);
}

auto AssetEmitter::Count() const noexcept -> size_t { return records_.size(); }

auto AssetEmitter::PendingCount() const noexcept -> size_t
{
  return pending_count_.load(std::memory_order_acquire);
}

auto AssetEmitter::ErrorCount() const noexcept -> size_t
{
  return error_count_.load(std::memory_order_acquire);
}

auto AssetEmitter::Records() const noexcept
  -> const std::vector<EmittedAssetRecord>&
{
  return records_;
}

auto AssetEmitter::RecordAsset(const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, const uint64_t descriptor_size,
  std::optional<base::Sha256Digest> sha256) -> void
{
  if (const auto it = record_index_by_key_.find(key);
    it != record_index_by_key_.end()) {
    auto& record = records_[it->second];

    if (record.virtual_path != virtual_path) {
      const auto old_virtual_path = record.virtual_path;
      if (const auto key_it = key_by_virtual_path_.find(old_virtual_path);
        key_it != key_by_virtual_path_.end() && key_it->second == key) {
        key_by_virtual_path_.erase(key_it);
      }
      key_by_virtual_path_.insert_or_assign(std::string(virtual_path), key);
    }

    record.asset_type = asset_type;
    record.virtual_path = std::string(virtual_path);
    record.descriptor_relpath = std::string(descriptor_relpath);
    record.descriptor_size = descriptor_size;
    record.descriptor_sha256 = std::move(sha256);
    return;
  }

  EmittedAssetRecord record {
    .key = key,
    .asset_type = asset_type,
    .virtual_path = std::string(virtual_path),
    .descriptor_relpath = std::string(descriptor_relpath),
    .descriptor_size = descriptor_size,
    .descriptor_sha256 = std::move(sha256),
  };

  const auto index = records_.size();
  records_.push_back(std::move(record));
  record_index_by_key_.emplace(key, index);
  key_by_virtual_path_.insert_or_assign(std::string(virtual_path), key);
}

auto AssetEmitter::QueueDescriptorWrite(
  const std::filesystem::path& descriptor_path,
  std::string_view descriptor_relpath, std::span<const std::byte> bytes) -> void
{
  auto bytes_ptr
    = std::make_shared<std::vector<std::byte>>(bytes.begin(), bytes.end());

  auto& state = write_state_by_relpath_[std::string(descriptor_relpath)];
  if (state.descriptor_path.empty()) {
    state.descriptor_path = descriptor_path;
  }

  if (state.write_in_flight) {
    state.queued_bytes = std::move(bytes_ptr);
    return;
  }

  state.write_in_flight = true;
  pending_count_.fetch_add(1, std::memory_order_acq_rel);

  file_writer_.WriteAsync(descriptor_path,
    std::span<const std::byte>(*bytes_ptr),
    WriteOptions { .create_directories = true, .share_write = true },
    [this, bytes_ptr, relpath = std::string(descriptor_relpath)](
      const FileErrorInfo& error, [[maybe_unused]] uint64_t bytes_written) {
      OnWriteComplete(relpath, error);
    });
}

auto AssetEmitter::OnWriteComplete(
  const std::string_view descriptor_relpath, const FileErrorInfo& error) -> void
{
  pending_count_.fetch_sub(1, std::memory_order_acq_rel);

  const auto state_it
    = write_state_by_relpath_.find(std::string(descriptor_relpath));
  if (state_it != write_state_by_relpath_.end()) {
    auto& state = state_it->second;
    state.write_in_flight = false;

    if (state.queued_bytes) {
      auto bytes_ptr = std::move(state.queued_bytes);
      state.queued_bytes.reset();
      state.write_in_flight = true;
      pending_count_.fetch_add(1, std::memory_order_acq_rel);

      file_writer_.WriteAsync(state.descriptor_path,
        std::span<const std::byte>(*bytes_ptr),
        WriteOptions { .create_directories = true, .share_write = true },
        [this, bytes_ptr, relpath = std::string(descriptor_relpath)](
          const FileErrorInfo& next_error,
          [[maybe_unused]] uint64_t bytes_written) {
          OnWriteComplete(relpath, next_error);
        });
    }
  }

  if (!error.IsError()) {
    return;
  }

  error_count_.fetch_add(1, std::memory_order_acq_rel);
  LOG_F(ERROR, "AssetEmitter: failed to write '{}': {}", descriptor_relpath,
    error.ToString());
}

auto AssetEmitter::Finalize() -> co::Co<bool>
{
  finalize_started_.store(true, std::memory_order_release);

  DLOG_F(INFO, "AssetEmitter::Finalize: waiting for {} pending writes",
    pending_count_.load(std::memory_order_acquire));

  // Wait for all pending writes via flush
  auto flush_result = co_await file_writer_.Flush();

  if (!flush_result.has_value()) {
    LOG_F(ERROR, "AssetEmitter::Finalize: flush failed: {}",
      flush_result.error().ToString());
    co_return false;
  }

  // Check for accumulated errors
  const auto errors = error_count_.load(std::memory_order_acquire);
  if (errors > 0) {
    LOG_F(ERROR, "AssetEmitter::Finalize: {} I/O errors occurred", errors);
    co_return false;
  }

  DLOG_F(INFO, "AssetEmitter::Finalize: complete, {} assets emitted",
    records_.size());

  co_return true;
}

} // namespace oxygen::content::import
