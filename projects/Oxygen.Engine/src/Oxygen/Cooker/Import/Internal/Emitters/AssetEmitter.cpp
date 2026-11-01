//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/IoDriver.h>

namespace oxygen::content::import {

namespace {

  //! Validate path segments don't contain `.` or `..`.
  auto RequireCanonicalVirtualPath(const std::string_view virtual_path) -> void
  {
    if (const auto error = oxygen::content::ValidateVirtualPath(virtual_path);
      error.has_value()) {
      throw std::runtime_error(std::string("Virtual path is not canonical: ")
        + nostd::to_string(*error));
    }
  }

} // namespace

AssetEmitter::AssetEmitter(cooked::ImportStorage& storage,
  serio::IoDriver& driver, co::Nursery& writes,
  std::filesystem::path cooked_root)
  : storage_(make_observer(&storage))
  , driver_(make_observer(&driver))
  , writes_(writes)
  , cooked_root_(std::move(cooked_root))
{
  DLOG_F(
    INFO, "Created asset emitter: cooked_root='{}'", cooked_root_.string());
}

AssetEmitter::~AssetEmitter()
{
  const auto pending = writes_.Running();
  if (pending > 0) {
    LOG_F(WARNING, "Destroyed with {} pending writes", pending);
  }
}

auto AssetEmitter::Emit(const data::AssetKey& key, data::AssetType asset_type,
  std::string_view virtual_path, std::string_view descriptor_relpath,
  std::span<const std::byte> bytes, data::AssetReferences references) -> void
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("AssetEmitter is finalized");
  }

  // Validate paths (must match PAK format requirements)
  RequireCanonicalVirtualPath(virtual_path);
  data::loose_cooked::RequireIndexRelPath(descriptor_relpath);
  ValidateAssetDescriptor(asset_type, key, bytes, references);

  if (const auto it = key_by_virtual_path_.find(std::string(virtual_path));
    it != key_by_virtual_path_.end() && it->second != key) {
    throw std::runtime_error(
      "Conflicting virtual path mapping in loose cooked container");
  }
  if (const auto it
    = key_by_descriptor_relpath_.find(std::string(descriptor_relpath));
    it != key_by_descriptor_relpath_.end() && it->second != key) {
    throw std::runtime_error(
      "Conflicting descriptor path mapping in loose cooked container");
  }

  const auto sha256 = base::ComputeSha256(bytes);

  // Build full path for the descriptor file
  const auto descriptor_path = cooked_root_ / descriptor_relpath;

  DLOG_F(INFO, "Emit: type={} vpath='{}' relpath='{}' size={}",
    static_cast<int>(asset_type), virtual_path, descriptor_relpath,
    bytes.size());

  RecordAsset(key, asset_type, virtual_path, descriptor_relpath, bytes.size(),
    std::move(references), sha256);
  QueueDescriptorWrite(descriptor_path, descriptor_relpath, bytes);
}

auto AssetEmitter::EmitSync(const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, std::span<const std::byte> bytes,
  data::AssetReferences references) -> co::Co<void>
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("AssetEmitter is finalized");
  }

  RequireCanonicalVirtualPath(virtual_path);
  data::loose_cooked::RequireIndexRelPath(descriptor_relpath);
  ValidateAssetDescriptor(asset_type, key, bytes, references);

  if (const auto it = key_by_virtual_path_.find(std::string(virtual_path));
    it != key_by_virtual_path_.end() && it->second != key) {
    throw std::runtime_error(
      "Conflicting virtual path mapping in loose cooked container");
  }
  if (const auto it
    = key_by_descriptor_relpath_.find(std::string(descriptor_relpath));
    it != key_by_descriptor_relpath_.end() && it->second != key) {
    throw std::runtime_error(
      "Conflicting descriptor path mapping in loose cooked container");
  }

  const auto sha256 = base::ComputeSha256(bytes);

  const auto descriptor_path = cooked_root_ / descriptor_relpath;
  const auto write_result
    = co_await storage_->PublishAsync(*driver_, descriptor_path, bytes);
  if (!write_result) {
    throw std::runtime_error("Failed to write '"
      + std::string(descriptor_relpath) + "' (" + descriptor_path.string()
      + "): " + write_result.error().message());
  }

  RecordAsset(key, asset_type, virtual_path, descriptor_relpath, bytes.size(),
    std::move(references), sha256);
}

auto AssetEmitter::Count() const noexcept -> size_t { return records_.size(); }

auto AssetEmitter::ErrorCount() const noexcept -> size_t
{
  return writes_.ErrorCount();
}

auto AssetEmitter::Records() const noexcept
  -> const std::vector<EmittedAssetRecord>&
{
  return records_;
}

auto AssetEmitter::RecordAsset(const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, const uint64_t descriptor_size,
  data::AssetReferences references, const base::Sha256Digest& sha256) -> void
{
  if (const auto it = record_index_by_key_.find(key);
    it != record_index_by_key_.end()) {
    auto& record = records_.at(it->second);

    if (record.virtual_path != virtual_path) {
      const auto old_virtual_path = record.virtual_path;
      if (const auto key_it = key_by_virtual_path_.find(old_virtual_path);
        key_it != key_by_virtual_path_.end() && key_it->second == key) {
        key_by_virtual_path_.erase(key_it);
      }
      key_by_virtual_path_.insert_or_assign(std::string(virtual_path), key);
    }
    if (record.descriptor_relpath != descriptor_relpath) {
      const auto old_descriptor_relpath = record.descriptor_relpath;
      if (const auto key_it
        = key_by_descriptor_relpath_.find(old_descriptor_relpath);
        key_it != key_by_descriptor_relpath_.end() && key_it->second == key) {
        key_by_descriptor_relpath_.erase(key_it);
      }
      key_by_descriptor_relpath_.insert_or_assign(
        std::string(descriptor_relpath), key);
    }

    record.asset_type = asset_type;
    record.virtual_path = std::string(virtual_path);
    record.descriptor_relpath = std::string(descriptor_relpath);
    record.descriptor_size = descriptor_size;
    record.descriptor_sha256 = sha256;
    record.references = std::move(references);
    return;
  }

  EmittedAssetRecord record {
    .key = key,
    .asset_type = asset_type,
    .virtual_path = std::string(virtual_path),
    .descriptor_relpath = std::string(descriptor_relpath),
    .descriptor_size = descriptor_size,
    .descriptor_sha256 = sha256,
    .references = std::move(references),
  };

  const auto index = records_.size();
  records_.push_back(std::move(record));
  record_index_by_key_.emplace(key, index);
  key_by_virtual_path_.insert_or_assign(std::string(virtual_path), key);
  key_by_descriptor_relpath_.insert_or_assign(
    std::string(descriptor_relpath), key);
}

auto AssetEmitter::QueueDescriptorWrite(
  const std::filesystem::path& descriptor_path,
  std::string_view descriptor_relpath, std::span<const std::byte> bytes) -> void
{
  auto owned = std::vector<std::byte>(bytes.begin(), bytes.end());

  auto& state = write_state_by_relpath_[std::string(descriptor_relpath)];
  if (state.descriptor_path.empty()) {
    state.descriptor_path = descriptor_path;
  }

  if (state.write_in_flight) {
    state.queued_bytes = std::move(owned);
    return;
  }

  state.write_in_flight = true;
  writes_.Start(descriptor_path,
    WriteDescriptor(std::string(descriptor_relpath), std::move(owned)));
}

auto AssetEmitter::WriteDescriptor(const std::string descriptor_relpath,
  std::vector<std::byte> bytes) -> co::Co<Result<void>>
{
  for (;;) {
    auto& state = write_state_by_relpath_.at(descriptor_relpath);
    const auto written = co_await storage_->PublishAsync(
      *driver_, state.descriptor_path, std::span<const std::byte>(bytes));
    if (!written) {
      LOG_F(ERROR, "Failed to write '{}' ({}): {}", descriptor_relpath,
        state.descriptor_path.string(), written.error().message());
      writes_.Record(state.descriptor_path, written);
    }

    // The latest bytes queued while this write was in flight go next.
    auto& after = write_state_by_relpath_.at(descriptor_relpath);
    if (!after.queued_bytes) {
      after.write_in_flight = false;
      co_return Result<void> {};
    }
    bytes = std::move(*after.queued_bytes);
    after.queued_bytes.reset();
  }
}

auto AssetEmitter::Finalize() -> co::Co<Result<void, WriteFailure>>
{
  finalize_started_.store(true, std::memory_order_release);

  DLOG_F(INFO, "Finalize: waiting for {} pending writes", writes_.Running());

  co_return co_await writes_.Join();
}

} // namespace oxygen::content::import
