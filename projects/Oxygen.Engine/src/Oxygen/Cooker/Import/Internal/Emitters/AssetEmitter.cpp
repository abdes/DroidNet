//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
#include <Oxygen/Data/DescriptorHash.h>
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
        + std::string(nostd::to_string(*error)));
    }
  }

  //! The descriptor `bytes` with its content hash recorded, as a cook emits
  //! every asset descriptor.
  auto StampedDescriptor(const std::span<const std::byte> bytes)
    -> std::vector<std::byte>
  {
    auto stamped = std::vector<std::byte>(bytes.begin(), bytes.end());
    data::StampDescriptorHash(stamped);
    return stamped;
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
  const auto descriptor = StampedDescriptor(bytes);
  RequireCanonicalVirtualPath(virtual_path);
  data::loose_cooked::RequireIndexRelPath(descriptor_relpath);
  ValidateAssetDescriptor(asset_type, key, descriptor, references);

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

  const auto sha256 = base::ComputeSha256(descriptor);
  const auto content_relpath
    = data::loose_cooked::ContentRelPath(descriptor_relpath, sha256);

  DLOG_F(INFO, "Emit: type={} vpath='{}' relpath='{}' size={}",
    static_cast<int>(asset_type), virtual_path, content_relpath,
    descriptor.size());

  RecordAsset(key, asset_type, virtual_path,
    { .layout = descriptor_relpath, .content = content_relpath },
    descriptor.size(), std::move(references), sha256);
  QueueDescriptorWrite(content_relpath, descriptor);
}

auto AssetEmitter::EmitSync(const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, std::span<const std::byte> bytes,
  data::AssetReferences references) -> co::Co<void>
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("AssetEmitter is finalized");
  }

  const auto descriptor = StampedDescriptor(bytes);
  RequireCanonicalVirtualPath(virtual_path);
  data::loose_cooked::RequireIndexRelPath(descriptor_relpath);
  ValidateAssetDescriptor(asset_type, key, descriptor, references);

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

  const auto sha256 = base::ComputeSha256(descriptor);
  const auto content_relpath
    = data::loose_cooked::ContentRelPath(descriptor_relpath, sha256);

  const auto write_result
    = co_await WriteDescriptor(content_relpath, descriptor);
  if (!write_result) {
    throw std::runtime_error("Failed to write '" + content_relpath
      + "': " + write_result.error().message());
  }
  written_relpaths_.insert(content_relpath);

  RecordAsset(key, asset_type, virtual_path,
    { .layout = descriptor_relpath, .content = content_relpath },
    descriptor.size(), std::move(references), sha256);
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
  const DescriptorPaths paths, const uint64_t descriptor_size,
  data::AssetReferences references, const base::Sha256Digest& sha256) -> void
{
  const auto descriptor_relpath = paths.layout;
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
    if (record.layout_relpath != descriptor_relpath) {
      const auto old_descriptor_relpath = record.layout_relpath;
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
    record.layout_relpath = std::string(descriptor_relpath);
    record.descriptor_relpath = std::string(paths.content);
    record.descriptor_size = descriptor_size;
    record.descriptor_sha256 = sha256;
    record.references = std::move(references);
    return;
  }

  EmittedAssetRecord record {
    .key = key,
    .asset_type = asset_type,
    .virtual_path = std::string(virtual_path),
    .descriptor_relpath = std::string(paths.content),
    .layout_relpath = std::string(descriptor_relpath),
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

auto AssetEmitter::QueueDescriptorWrite(const std::string& content_relpath,
  const std::span<const std::byte> bytes) -> void
{
  if (!written_relpaths_.insert(content_relpath).second) {
    return;
  }
  writes_.Start(cooked_root_ / std::filesystem::path(content_relpath),
    WriteDescriptor(
      content_relpath, std::vector<std::byte>(bytes.begin(), bytes.end())));
}

auto AssetEmitter::WriteDescriptor(const std::string content_relpath,
  std::vector<std::byte> bytes) -> co::Co<Result<void>>
{
  const auto path = cooked_root_ / std::filesystem::path(content_relpath);
  // A file of this content name already holds these bytes.
  if (const auto size = storage_->SizeOf(path);
    size.has_value() && size->get() == bytes.size()) {
    co_return Result<void> {};
  }
  const auto written = co_await storage_->PublishAsync(
    *driver_, path, std::span<const std::byte>(bytes));
  if (!written) {
    // The write group records the failure this returns.
    LOG_F(ERROR, "Failed to write '{}': {}", content_relpath,
      written.error().message());
  }
  co_return written;
}

auto AssetEmitter::Finalize() -> co::Co<Result<void, WriteFailure>>
{
  finalize_started_.store(true, std::memory_order_release);

  DLOG_F(INFO, "Finalize: waiting for {} pending writes", writes_.Running());

  co_return co_await writes_.Join();
}

} // namespace oxygen::content::import
