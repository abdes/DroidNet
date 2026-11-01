//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/DedupCollision.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto ComputeEffectiveHash(
    const CookedPhysicsResourcePayload& cooked) -> base::Sha256Digest
  {
    if (!util::IsZeroContentSha256(cooked.content_hash)) {
      return cooked.content_hash;
    }
    return util::ComputeContentSha256(cooked.data);
  }

  [[nodiscard]] auto ComputePhysicsIdentity(
    const CookedPhysicsResourcePayload& cooked, const base::Sha256Digest& hash)
    -> std::string
  {
    auto identity = std::string {};
    identity.reserve(160);
    identity.append("phys:");
    identity.append("f=");
    identity.append(std::to_string(static_cast<uint32_t>(cooked.format)));
    identity.append(";n=");
    identity.append(std::to_string(cooked.data.size()));
    identity.append(";h=");
    identity.append(base::ToHexString(hash));
    return identity;
  }

  [[nodiscard]] auto MakePhysicsSignature(
    const CookedPhysicsResourcePayload& cooked, const base::Sha256Digest& hash,
    std::string_view signature_salt) -> std::string
  {
    static_cast<void>(signature_salt);
    return ComputePhysicsIdentity(cooked, hash);
  }

  [[nodiscard]] auto MakePhysicsResourceAssetKey(std::string_view signature)
    -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath(
      std::string("physics.resource://") + std::string(signature));
  }

} // namespace

PhysicsResourceEmitter::PhysicsResourceEmitter(co::Nursery& writes,
  PhysicsTableAggregator& table_aggregator,
  const std::filesystem::path& cooked_root)
  : PhysicsResourceEmitter(writes, table_aggregator, cooked_root, Config {})
{
}

PhysicsResourceEmitter::PhysicsResourceEmitter(co::Nursery& writes,
  PhysicsTableAggregator& table_aggregator,
  const std::filesystem::path& cooked_root, Config config)
  : writes_(writes)
  , table_aggregator_(table_aggregator)
  , config_(std::move(config))
  , data_path_(cooked_root / internal::loose_layout::kPhysicsData)
{
  DLOG_F(
    INFO, "Created physics resource emitter: data='{}'", data_path_.string());
}

PhysicsResourceEmitter::~PhysicsResourceEmitter()
{
  const auto pending = writes_.Running();
  if (pending > 0) {
    LOG_F(WARNING, "Destroyed with {} pending writes", pending);
  }
}

auto PhysicsResourceEmitter::Emit(CookedPhysicsResourcePayload cooked,
  std::string_view signature_salt) -> EmittedPhysicsResourceRef
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("PhysicsResourceEmitter is finalized");
  }

  const auto hash_missing = util::IsZeroContentSha256(cooked.content_hash);
  const auto effective_hash = ComputeEffectiveHash(cooked);
  cooked.content_hash = effective_hash;

  const auto identity = ComputePhysicsIdentity(cooked, effective_hash);
  DCHECK_F(!identity.empty(), "physics identity must not be empty");

  if (hash_missing && !signature_salt.empty()) {
    const auto key = std::string(signature_salt);
    if (const auto it = identity_by_key_.find(key);
      it != identity_by_key_.end() && it->second != identity) {
      const auto existing_index_it = index_by_key_.find(key);
      const auto existing_index = (existing_index_it != index_by_key_.end())
        ? existing_index_it->second
        : 0U;

      const auto action
        = internal::DedupCollisionAction(config_.collision_policy);
      const auto diagnostic = ImportDiagnostic {
        .severity = (config_.collision_policy == DedupCollisionPolicy::kError)
          ? ImportSeverity::kError
          : ImportSeverity::kWarning,
        .code = "import.dedup_collision.physics",
        .message = "policy=" + to_string(config_.collision_policy)
          + ";action=" + std::string(action)
          + ";existing_index=" + std::to_string(existing_index),
        .source_path = key,
        .object_path = "physics",
      };
      if (config_.on_dedup_diagnostic) {
        config_.on_dedup_diagnostic(diagnostic);
      }

      if (config_.collision_policy == DedupCollisionPolicy::kError) {
        throw std::runtime_error("physics resource dedup collision");
      }
      if (config_.collision_policy == DedupCollisionPolicy::kWarnKeepFirst) {
        const auto existing_desc
          = table_aggregator_.TryGetDescriptor(existing_index);
        return EmittedPhysicsResourceRef {
          .resource_index = existing_index,
          .resource_asset_key = existing_desc.has_value()
            ? existing_desc->resource_asset_key
            : data::pak::physics::kInvalidPhysicsResourceAssetKey,
        };
      }
    }
  }

  const auto signature
    = MakePhysicsSignature(cooked, effective_hash, signature_salt);
  DCHECK_F(!signature.empty(), "physics signature must not be empty");
  const auto resource_asset_key = MakePhysicsResourceAssetKey(signature);

  const auto resource_alignment = cooked.alignment > 0 ? cooked.alignment : 16;
  const auto acquire = table_aggregator_.AcquireOrInsert(signature,
    [&]
    -> std::pair<data::pak::physics::PhysicsResourceDesc, WriteReservation> {
      const auto reserved = table_aggregator_.ReserveDataRange({
        .alignment = resource_alignment,
        .payload_size = cooked.data.size(),
      });
      auto desc
        = MakeTableEntry(cooked, reserved.aligned_offset, resource_asset_key);
      return std::make_pair(desc, reserved);
    });

  if (!acquire.is_new) {
    if (hash_missing && !signature_salt.empty()) {
      const auto key = std::string(signature_salt);
      identity_by_key_[key] = identity;
      index_by_key_[key] = acquire.index;
    }
    return EmittedPhysicsResourceRef {
      .resource_index = acquire.index,
      .resource_asset_key = resource_asset_key,
    };
  }

  emitted_count_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved = acquire.reservation;
  if (reserved.padding_size > 0) {
    QueueDataWrite(WriteKind::kPadding, std::nullopt,
      reserved.reservation_start,
      std::vector<std::byte>(reserved.padding_size, std::byte { 0 }));
  }
  QueueDataWrite(WriteKind::kPayload, acquire.index, reserved.aligned_offset,
    std::move(cooked.data));

  if (hash_missing && !signature_salt.empty()) {
    const auto key = std::string(signature_salt);
    identity_by_key_[key] = identity;
    index_by_key_[key] = acquire.index;
  }

  return EmittedPhysicsResourceRef {
    .resource_index = acquire.index,
    .resource_asset_key = resource_asset_key,
  };
}

auto PhysicsResourceEmitter::Count() const noexcept -> uint32_t
{
  return emitted_count_.load(std::memory_order_acquire);
}

auto PhysicsResourceEmitter::ErrorCount() const noexcept -> size_t
{
  return writes_.ErrorCount();
}

auto PhysicsResourceEmitter::DataFileSize() const noexcept -> uint64_t
{
  return table_aggregator_.DataFileSize();
}

auto PhysicsResourceEmitter::TryGetDescriptor(const uint32_t index) const
  -> std::optional<data::pak::physics::PhysicsResourceDesc>
{
  return table_aggregator_.TryGetDescriptor(index);
}

auto PhysicsResourceEmitter::Finalize() -> co::Co<Result<void, WriteFailure>>
{
  finalize_started_.store(true, std::memory_order_release);

  co_return co_await writes_.Join();
}

auto PhysicsResourceEmitter::MakeTableEntry(
  const CookedPhysicsResourcePayload& cooked, const uint64_t data_offset,
  const data::AssetKey& resource_asset_key)
  -> data::pak::physics::PhysicsResourceDesc
{
  auto entry = data::pak::physics::PhysicsResourceDesc {};
  entry.data_offset = data_offset;
  entry.size_bytes
    = static_cast<data::pak::core::DataBlobSizeT>(cooked.data.size());
  entry.format = cooked.format;
  entry.resource_asset_key = resource_asset_key;
  static_assert(sizeof(entry.content_hash) == sizeof(cooked.content_hash));
  std::memcpy(std::span { entry.content_hash }.data(),
    cooked.content_hash.data(), sizeof(entry.content_hash));
  return entry;
}

auto PhysicsResourceEmitter::QueueDataWrite(const WriteKind kind,
  const std::optional<uint32_t> index, const uint64_t offset,
  std::vector<std::byte> data) -> void
{
  const auto file = table_aggregator_.DataFile();
  if (!file) {
    LOG_F(ERROR, "Failed to open '{}': {}", data_path_.string(),
      file.error().message());
    writes_.Fail(data_path_, file.error());
    return;
  }
  writes_.Start(
    data_path_, WriteData(*file, kind, index, offset, std::move(data)));
}

auto PhysicsResourceEmitter::WriteData(serio::AnyAsyncDevice* const file,
  const WriteKind kind, const std::optional<uint32_t> index,
  const uint64_t offset, std::vector<std::byte> data) -> co::Co<Result<void>>
{
  const auto written = co_await file->AsyncWriteAt(
    serio::ByteOffset { offset }, std::span<const std::byte>(data));
  if (!written) {
    if (kind == WriteKind::kPadding) {
      LOG_F(ERROR, "Failed to write physics resource padding to '{}': {}",
        data_path_.string(), written.error().message());
    } else {
      LOG_F(ERROR, "Failed to write physics resource {} to '{}': {}",
        index.value_or(0U), data_path_.string(), written.error().message());
    }
  }
  co_return written;
}

} // namespace oxygen::content::import
