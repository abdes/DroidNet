//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto ComputeFastFingerprint(
    const std::span<const std::byte> bytes) -> uint64_t
  {
    constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    auto hash = kFnvOffsetBasis;
    for (const auto b : bytes) {
      hash ^= static_cast<uint64_t>(std::to_integer<uint8_t>(b));
      hash *= kFnvPrime;
    }
    return hash;
  }

  auto AppendHashSlotHex(std::string& out, const uint64_t hash64) -> void
  {
    auto slot = std::array<uint8_t, 32> {};
    std::memcpy(slot.data(), &hash64, sizeof(hash64));

    constexpr auto kHex = "0123456789abcdef";
    for (const auto byte : slot) {
      out.push_back(kHex[(byte >> 4U) & 0x0FU]);
      out.push_back(kHex[byte & 0x0FU]);
    }
  }

  [[nodiscard]] auto ComputePhysicsIdentity(
    const CookedPhysicsResourcePayload& cooked) -> std::string
  {
    auto identity = std::string {};
    identity.reserve(160);
    identity.append("phys:");
    identity.append("f=");
    identity.append(std::to_string(static_cast<uint32_t>(cooked.format)));
    identity.append(";n=");
    identity.append(std::to_string(cooked.data.size()));

    const auto hash64 = (cooked.content_hash != 0ULL)
      ? cooked.content_hash
      : ComputeFastFingerprint(cooked.data);
    identity.append(";h=");
    AppendHashSlotHex(identity, hash64);
    return identity;
  }

  [[nodiscard]] auto MakePhysicsSignature(
    const CookedPhysicsResourcePayload& cooked, std::string_view signature_salt)
    -> std::string
  {
    static_cast<void>(signature_salt);
    return ComputePhysicsIdentity(cooked);
  }

} // namespace

PhysicsResourceEmitter::PhysicsResourceEmitter(IAsyncFileWriter& file_writer,
  PhysicsTableAggregator& table_aggregator, const LooseCookedLayout& layout,
  const std::filesystem::path& cooked_root)
  : PhysicsResourceEmitter(
      file_writer, table_aggregator, layout, cooked_root, Config {})
{
}

PhysicsResourceEmitter::PhysicsResourceEmitter(IAsyncFileWriter& file_writer,
  PhysicsTableAggregator& table_aggregator, const LooseCookedLayout& layout,
  const std::filesystem::path& cooked_root, Config config)
  : file_writer_(file_writer)
  , table_aggregator_(table_aggregator)
  , config_(std::move(config))
  , data_path_(cooked_root / layout.PhysicsDataRelPath())
{
  DLOG_F(
    INFO, "Created physics resource emitter: data='{}'", data_path_.string());
}

PhysicsResourceEmitter::~PhysicsResourceEmitter()
{
  const auto pending = pending_count_.load(std::memory_order_acquire);
  if (pending > 0) {
    LOG_F(WARNING, "Destroyed with {} pending writes", pending);
  }
}

auto PhysicsResourceEmitter::Emit(CookedPhysicsResourcePayload cooked,
  std::string_view signature_salt) -> uint32_t
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("PhysicsResourceEmitter is finalized");
  }

  const auto identity = ComputePhysicsIdentity(cooked);
  DCHECK_F(!identity.empty(), "physics identity must not be empty");

  if (cooked.content_hash == 0ULL && !signature_salt.empty()) {
    const auto key = std::string(signature_salt);
    if (const auto it = identity_by_key_.find(key);
      it != identity_by_key_.end() && it->second != identity) {
      const auto existing_index_it = index_by_key_.find(key);
      const auto existing_index = (existing_index_it != index_by_key_.end())
        ? existing_index_it->second
        : 0U;

      const auto diagnostic = ImportDiagnostic {
        .severity = (config_.collision_policy == DedupCollisionPolicy::kError)
          ? ImportSeverity::kError
          : ImportSeverity::kWarning,
        .code = "import.dedup_collision.physics",
        .message = "policy=" + to_string(config_.collision_policy) + ";action="
          + std::string(
            config_.collision_policy == DedupCollisionPolicy::kWarnReplace
              ? "replace"
              : (config_.collision_policy
                      == DedupCollisionPolicy::kWarnKeepFirst
                    ? "keep_first"
                    : "error"))
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
        return existing_index;
      }
    }
  }

  const auto signature = MakePhysicsSignature(cooked, signature_salt);
  DCHECK_F(!signature.empty(), "physics signature must not be empty");

  const auto resource_alignment = cooked.alignment > 0 ? cooked.alignment : 16;
  const auto acquire = table_aggregator_.AcquireOrInsert(signature, [&]() {
    const auto reserved = table_aggregator_.ReserveDataRange(
      resource_alignment, cooked.data.size());
    auto desc = MakeTableEntry(cooked, reserved.aligned_offset);
    return std::make_pair(desc, reserved);
  });

  if (!acquire.is_new) {
    if (cooked.content_hash == 0ULL && !signature_salt.empty()) {
      const auto key = std::string(signature_salt);
      identity_by_key_[key] = identity;
      index_by_key_[key] = acquire.index;
    }
    return acquire.index;
  }

  emitted_count_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved = acquire.reservation;
  if (reserved.padding_size > 0) {
    auto padding = std::make_shared<std::vector<std::byte>>(
      reserved.padding_size, std::byte { 0 });
    QueueDataWrite(
      WriteKind::kPadding, std::nullopt, reserved.reservation_start, padding);
  }
  auto payload
    = std::make_shared<std::vector<std::byte>>(std::move(cooked.data));
  QueueDataWrite(
    WriteKind::kPayload, acquire.index, reserved.aligned_offset, payload);

  if (cooked.content_hash == 0ULL && !signature_salt.empty()) {
    const auto key = std::string(signature_salt);
    identity_by_key_[key] = identity;
    index_by_key_[key] = acquire.index;
  }

  return acquire.index;
}

auto PhysicsResourceEmitter::Count() const noexcept -> uint32_t
{
  return emitted_count_.load(std::memory_order_acquire);
}

auto PhysicsResourceEmitter::PendingCount() const noexcept -> size_t
{
  return pending_count_.load(std::memory_order_acquire);
}

auto PhysicsResourceEmitter::ErrorCount() const noexcept -> size_t
{
  return error_count_.load(std::memory_order_acquire);
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

auto PhysicsResourceEmitter::Finalize() -> co::Co<bool>
{
  finalize_started_.store(true, std::memory_order_release);

  auto flush_result = co_await file_writer_.Flush();
  if (!flush_result.has_value()) {
    LOG_F(ERROR, "Finalize: physics flush failed: {}",
      flush_result.error().ToString());
    co_return false;
  }

  if (error_count_.load(std::memory_order_acquire) > 0U) {
    co_return false;
  }
  co_return true;
}

auto PhysicsResourceEmitter::MakeTableEntry(
  const CookedPhysicsResourcePayload& cooked, const uint64_t data_offset)
  -> data::pak::physics::PhysicsResourceDesc
{
  auto entry = data::pak::physics::PhysicsResourceDesc {};
  entry.data_offset = data_offset;
  entry.size_bytes
    = static_cast<data::pak::core::DataBlobSizeT>(cooked.data.size());
  entry.format = cooked.format;
  const auto hash64 = (cooked.content_hash != 0ULL)
    ? cooked.content_hash
    : ComputeFastFingerprint(cooked.data);
  std::memcpy(entry.content_hash, &hash64, sizeof(hash64));
  return entry;
}

auto PhysicsResourceEmitter::QueueDataWrite(const WriteKind kind,
  const std::optional<uint32_t> index, const uint64_t offset,
  std::shared_ptr<std::vector<std::byte>> data) -> void
{
  auto keepalive = std::move(data);
  DCHECK_F(
    keepalive != nullptr, "physics resource write payload must not be null");
  const auto bytes = std::span<const std::byte>(*keepalive);

  pending_count_.fetch_add(1, std::memory_order_acq_rel);

  file_writer_.WriteAtAsync(data_path_, offset, bytes,
    WriteOptions { .create_directories = true, .share_write = true },
    [this, kind, index, data = std::move(keepalive)](
      const FileErrorInfo& error, [[maybe_unused]] uint64_t bytes_written) {
      OnWriteComplete(kind, index, error);
    });
}

auto PhysicsResourceEmitter::OnWriteComplete(const WriteKind kind,
  const std::optional<uint32_t> index, const FileErrorInfo& error) -> void
{
  pending_count_.fetch_sub(1, std::memory_order_acq_rel);

  if (!error.IsError()) {
    return;
  }

  error_count_.fetch_add(1, std::memory_order_acq_rel);
  if (kind == WriteKind::kPadding) {
    LOG_F(
      ERROR, "Failed to write physics resource padding: {}", error.ToString());
    return;
  }

  LOG_F(ERROR, "Failed to write physics resource {}: {}", index.value_or(0U),
    error.ToString());
}

} // namespace oxygen::content::import
