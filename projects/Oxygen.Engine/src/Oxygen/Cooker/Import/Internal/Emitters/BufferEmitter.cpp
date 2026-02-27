//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

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

  [[nodiscard]] auto ComputeBufferIdentity(const CookedBufferPayload& cooked)
    -> std::string
  {
    std::string identity;
    identity.reserve(128);
    identity.append("buf:");
    identity.append("u=");
    identity.append(std::to_string(cooked.usage_flags));
    identity.append(";s=");
    identity.append(std::to_string(cooked.element_stride));
    identity.append(";f=");
    identity.append(std::to_string(cooked.element_format));
    identity.append(";n=");
    identity.append(std::to_string(cooked.data.size()));
    if (cooked.content_hash != 0ULL) {
      identity.append(";h=");
      identity.append(std::to_string(cooked.content_hash));
      return identity;
    }
    identity.append(";fp=");
    identity.append(std::to_string(ComputeFastFingerprint(cooked.data)));
    return identity;
  }

  [[nodiscard]] auto MakeBufferSignature(const CookedBufferPayload& cooked,
    std::string_view signature_salt) -> std::string
  {
    static_cast<void>(signature_salt);
    return ComputeBufferIdentity(cooked);
  }

} // namespace

BufferEmitter::BufferEmitter(IAsyncFileWriter& file_writer,
  BufferTableAggregator& table_aggregator, const LooseCookedLayout& layout,
  const std::filesystem::path& cooked_root, Config config)
  : file_writer_(file_writer)
  , table_aggregator_(table_aggregator)
  , config_(std::move(config))
  , data_path_(cooked_root / layout.BuffersDataRelPath())
{
  DLOG_F(INFO, "Created buffer emitter: data='{}'", data_path_.string());
}

BufferEmitter::~BufferEmitter()
{
  const auto pending = pending_count_.load(std::memory_order_acquire);
  if (pending > 0) {
    LOG_F(WARNING, "Destroyed with {} pending writes", pending);
  }
}

auto BufferEmitter::Emit(
  CookedBufferPayload cooked, std::string_view signature_salt) -> uint32_t
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("BufferEmitter is finalized");
  }

  const auto identity = ComputeBufferIdentity(cooked);
  DCHECK_F(!identity.empty(), "buffer identity must not be empty");

  if (cooked.content_hash == 0ULL && !signature_salt.empty()) {
    const std::string key(signature_salt);
    if (const auto it = identity_by_key_.find(key);
      it != identity_by_key_.end() && it->second != identity) {
      const auto existing_index_it = index_by_key_.find(key);
      const uint32_t existing_index = (existing_index_it != index_by_key_.end())
        ? existing_index_it->second
        : 0U;

      ImportDiagnostic diagnostic {
        .severity = (config_.collision_policy == DedupCollisionPolicy::kError)
          ? ImportSeverity::kError
          : ImportSeverity::kWarning,
        .code = "import.dedup_collision.buffer",
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
        .object_path = "buffer",
      };
      if (config_.on_dedup_diagnostic) {
        config_.on_dedup_diagnostic(diagnostic);
      }
      if (config_.collision_policy == DedupCollisionPolicy::kError) {
        LOG_F(ERROR,
          "buffer dedup collision: policy={} key='{}' existing_index={} "
          "action={}",
          to_string(config_.collision_policy), key, existing_index, "error");
      } else {
        LOG_F(WARNING,
          "buffer dedup collision: policy={} key='{}' existing_index={} "
          "action={}",
          to_string(config_.collision_policy), key, existing_index,
          (config_.collision_policy == DedupCollisionPolicy::kWarnReplace)
            ? "replace"
            : "keep_first");
      }

      if (config_.collision_policy == DedupCollisionPolicy::kError) {
        throw std::runtime_error("buffer dedup collision");
      }
      if (config_.collision_policy == DedupCollisionPolicy::kWarnKeepFirst) {
        return existing_index;
      }
    }
  }

  const auto signature = MakeBufferSignature(cooked, signature_salt);
  DCHECK_F(!signature.empty(), "buffer signature must not be empty");

  // Use buffer's specified alignment (defaults to 16)
  const auto buffer_alignment = cooked.alignment > 0 ? cooked.alignment : 16ULL;

  const auto acquire = table_aggregator_.AcquireOrInsert(signature, [&]() {
    const auto reserved = table_aggregator_.ReserveDataRange(
      buffer_alignment, cooked.data.size());
    auto desc = MakeTableEntry(cooked, reserved.aligned_offset);
    return std::make_pair(desc, reserved);
  });

  if (!acquire.is_new) {
    if (cooked.content_hash == 0ULL && !signature_salt.empty()) {
      const std::string key(signature_salt);
      identity_by_key_[key] = identity;
      index_by_key_[key] = acquire.index;
    }
    return acquire.index;
  }

  emitted_count_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved = acquire.reservation;

  DLOG_F(INFO,
    "Emit: index={} offset={} size={} padding={} usage=0x{:x} stride={}",
    acquire.index, reserved.aligned_offset, cooked.data.size(),
    reserved.padding_size, cooked.usage_flags, cooked.element_stride);

  if (reserved.padding_size > 0) {
    auto padding_ptr = std::make_shared<std::vector<std::byte>>(
      reserved.padding_size, std::byte { 0 });
    QueueDataWrite(WriteKind::kPadding, std::nullopt,
      reserved.reservation_start, padding_ptr);
  }

  // Move payload into shared_ptr for async lifetime management
  auto payload_ptr
    = std::make_shared<std::vector<std::byte>>(std::move(cooked.data));

  // Queue async write at explicit offset for buffer data
  QueueDataWrite(
    WriteKind::kPayload, acquire.index, reserved.aligned_offset, payload_ptr);

  if (cooked.content_hash == 0ULL && !signature_salt.empty()) {
    const std::string key(signature_salt);
    identity_by_key_[key] = identity;
    index_by_key_[key] = acquire.index;
  }

  return acquire.index;
}

auto BufferEmitter::QueueDataWrite(const WriteKind kind,
  const std::optional<uint32_t> index, const uint64_t offset,
  std::shared_ptr<std::vector<std::byte>> data) -> void
{
  pending_count_.fetch_add(1, std::memory_order_acq_rel);

  file_writer_.WriteAtAsync(data_path_, offset,
    std::span<const std::byte>(*data),
    WriteOptions { .create_directories = true, .share_write = true },
    [this, kind, index, data](
      const FileErrorInfo& error, [[maybe_unused]] uint64_t bytes_written) {
      OnWriteComplete(kind, index, error);
    });
}

auto BufferEmitter::OnWriteComplete(const WriteKind kind,
  const std::optional<uint32_t> index, const FileErrorInfo& error) -> void
{
  pending_count_.fetch_sub(1, std::memory_order_acq_rel);

  if (!error.IsError()) {
    return;
  }

  error_count_.fetch_add(1, std::memory_order_acq_rel);
  if (kind == WriteKind::kPadding) {
    LOG_F(ERROR, "Failed to write padding: {}", error.ToString());
    return;
  }

  LOG_F(ERROR, "Failed to write buffer {}: {}", index.value_or(0),
    error.ToString());
}

auto BufferEmitter::Count() const noexcept -> uint32_t
{
  return emitted_count_.load(std::memory_order_acquire);
}

auto BufferEmitter::PendingCount() const noexcept -> size_t
{
  return pending_count_.load(std::memory_order_acquire);
}

auto BufferEmitter::ErrorCount() const noexcept -> size_t
{
  return error_count_.load(std::memory_order_acquire);
}

auto BufferEmitter::DataFileSize() const noexcept -> uint64_t
{
  return table_aggregator_.DataFileSize();
}

auto BufferEmitter::Finalize() -> co::Co<bool>
{
  finalize_started_.store(true, std::memory_order_release);

  DLOG_F(INFO, "Finalize: waiting for {} pending writes",
    pending_count_.load(std::memory_order_acquire));

  // Wait for all pending writes via flush
  auto flush_result = co_await file_writer_.Flush();

  if (!flush_result.has_value()) {
    LOG_F(ERROR, "Finalize: flush failed: {}", flush_result.error().ToString());
    co_return false;
  }

  // Check for accumulated errors
  const auto errors = error_count_.load(std::memory_order_acquire);
  if (errors > 0) {
    LOG_F(ERROR, "Finalize: {} I/O errors occurred", errors);
    co_return false;
  }

  DLOG_F(INFO, "Finalize: complete, {} buffers emitted",
    emitted_count_.load(std::memory_order_acquire));

  co_return true;
}

auto BufferEmitter::MakeTableEntry(
  const CookedBufferPayload& cooked, uint64_t data_offset) -> BufferResourceDesc
{
  using data::pak::core::DataBlobSizeT;
  using data::pak::core::OffsetT;

  BufferResourceDesc entry {};
  entry.data_offset = data_offset;
  entry.size_bytes = static_cast<DataBlobSizeT>(cooked.data.size());
  entry.usage_flags = cooked.usage_flags;
  entry.element_stride = cooked.element_stride;
  entry.element_format = cooked.element_format;
  entry.content_hash = (cooked.content_hash != 0ULL)
    ? cooked.content_hash
    : ComputeFastFingerprint(cooked.data);
  // entry.reserved is zero-initialized by default

  return entry;
}

} // namespace oxygen::content::import
