//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <stdexcept>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Async/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/emit/TextureEmissionUtils.h>
#include <Oxygen/Content/Import/util/Constants.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  //! Aligns a value up to the specified alignment.
  [[nodiscard]] constexpr auto AlignUp(
    const uint64_t value, const uint64_t alignment) noexcept -> uint64_t
  {
    if (alignment <= 1) {
      return value;
    }
    const auto remainder = value % alignment;
    return (remainder == 0) ? value : (value + (alignment - remainder));
  }

  [[nodiscard]] auto GetExistingDataSize(const std::filesystem::path& data_path)
    -> uint64_t
  {
    std::error_code ec;
    const auto size = std::filesystem::file_size(data_path, ec);
    if (!ec) {
      return size;
    }

    return 0;
  }

} // namespace

TextureEmitter::TextureEmitter(IAsyncFileWriter& file_writer,
  TextureTableAggregator& table_aggregator, const LooseCookedLayout& layout,
  const std::filesystem::path& cooked_root)
  : file_writer_(file_writer)
  , table_aggregator_(table_aggregator)
  , data_path_(cooked_root / layout.TexturesDataRelPath())
{
  data_file_size_.store(
    GetExistingDataSize(data_path_), std::memory_order_release);

  DLOG_F(INFO, "TextureEmitter created: data='{}'", data_path_.string());
}

TextureEmitter::~TextureEmitter()
{
  const auto pending = pending_count_.load(std::memory_order_acquire);
  if (pending > 0) {
    LOG_F(WARNING, "TextureEmitter destroyed with {} pending writes", pending);
  }
}

auto TextureEmitter::Emit(CookedTexturePayload cooked) -> uint32_t
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("TextureEmitter is finalized");
  }

  EnsureFallbackTexture();

  const auto tmp_desc = emit::ToPakDescriptor(cooked, 0);
  const auto signature = TextureTableTraits::SignatureForDescriptor(tmp_desc);
  DCHECK_F(!signature.empty(), "texture signature must not be empty");
  const auto acquire = table_aggregator_.AcquireOrInsert(signature, [&]() {
    const auto reserved
      = ReserveDataRange(util::kRowPitchAlignment, cooked.payload.size());
    auto desc = emit::ToPakDescriptor(cooked, reserved.aligned_offset);
    return std::make_pair(desc, reserved);
  });

  if (!acquire.is_new) {
    return acquire.index;
  }

  emitted_count_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved = acquire.reservation;

  DLOG_F(INFO,
    "TextureEmitter::Emit: index={} offset={} size={} padding={} format={}",
    acquire.index, reserved.aligned_offset, cooked.payload.size(),
    reserved.padding_size, static_cast<int>(cooked.desc.format));

  // Write padding if needed (before the texture data)
  if (reserved.padding_size > 0) {
    auto padding_ptr = std::make_shared<std::vector<std::byte>>(
      reserved.padding_size, std::byte { 0 });
    QueueDataWrite(WriteKind::kPadding, TextureKind::kUser, std::nullopt,
      reserved.reservation_start, padding_ptr);
  }

  // Move payload into shared_ptr for async lifetime management
  auto payload_ptr
    = std::make_shared<std::vector<std::byte>>(std::move(cooked.payload));

  // Queue async write at explicit offset for texture data
  QueueDataWrite(WriteKind::kPayload, TextureKind::kUser, acquire.index,
    reserved.aligned_offset, payload_ptr);

  return acquire.index;
}

auto TextureEmitter::ReserveDataRange(
  const uint64_t alignment, const uint64_t payload_size) -> WriteReservation
{
  uint64_t current_size = data_file_size_.load(std::memory_order_acquire);
  uint64_t aligned_offset = 0;
  uint64_t new_size = 0;

  do {
    aligned_offset = AlignUp(current_size, alignment);
    new_size = aligned_offset + payload_size;
  } while (!data_file_size_.compare_exchange_weak(
    current_size, new_size, std::memory_order_acq_rel));

  return {
    .reservation_start = current_size,
    .aligned_offset = aligned_offset,
    .padding_size = aligned_offset - current_size,
  };
}

auto TextureEmitter::QueueDataWrite(const WriteKind kind,
  const TextureKind texture_kind, const std::optional<uint32_t> index,
  const uint64_t offset, std::shared_ptr<std::vector<std::byte>> data) -> void
{
  pending_count_.fetch_add(1, std::memory_order_acq_rel);

  file_writer_.WriteAtAsync(data_path_, offset,
    std::span<const std::byte>(*data),
    WriteOptions { .create_directories = true, .share_write = true },
    [this, kind, texture_kind, index, data](
      const FileErrorInfo& error, [[maybe_unused]] uint64_t bytes_written) {
      OnWriteComplete(kind, texture_kind, index, error);
    });
}

auto TextureEmitter::OnWriteComplete(const WriteKind kind,
  const TextureKind texture_kind, const std::optional<uint32_t> index,
  const FileErrorInfo& error) -> void
{
  pending_count_.fetch_sub(1, std::memory_order_acq_rel);

  if (!error.IsError()) {
    return;
  }

  error_count_.fetch_add(1, std::memory_order_acq_rel);

  if (kind == WriteKind::kPadding) {
    if (texture_kind == TextureKind::kFallback) {
      LOG_F(ERROR, "TextureEmitter: failed to write fallback padding: {}",
        error.ToString());
      return;
    }

    LOG_F(
      ERROR, "TextureEmitter: failed to write padding: {}", error.ToString());
    return;
  }

  if (texture_kind == TextureKind::kFallback) {
    LOG_F(ERROR, "TextureEmitter: failed to write fallback texture: {}",
      error.ToString());
    return;
  }

  LOG_F(ERROR, "TextureEmitter: failed to write texture {}: {}",
    index.value_or(0), error.ToString());
}

auto TextureEmitter::Count() const noexcept -> uint32_t
{
  return emitted_count_.load(std::memory_order_acquire);
}

auto TextureEmitter::PendingCount() const noexcept -> size_t
{
  return pending_count_.load(std::memory_order_acquire);
}

auto TextureEmitter::ErrorCount() const noexcept -> size_t
{
  return error_count_.load(std::memory_order_acquire);
}

auto TextureEmitter::DataFileSize() const noexcept -> uint64_t
{
  return data_file_size_.load(std::memory_order_acquire);
}

auto TextureEmitter::Finalize() -> co::Co<bool>
{
  finalize_started_.store(true, std::memory_order_release);

  DLOG_F(INFO, "TextureEmitter::Finalize: waiting for {} pending writes",
    pending_count_.load(std::memory_order_acquire));

  // Wait for all pending writes via flush
  auto flush_result = co_await file_writer_.Flush();

  if (!flush_result.has_value()) {
    LOG_F(ERROR, "TextureEmitter::Finalize: flush failed: {}",
      flush_result.error().ToString());
    co_return false;
  }

  // Check for accumulated errors
  const auto errors = error_count_.load(std::memory_order_acquire);
  if (errors > 0) {
    LOG_F(ERROR, "TextureEmitter::Finalize: {} I/O errors occurred", errors);
    co_return false;
  }

  DLOG_F(INFO, "TextureEmitter::Finalize: complete, {} textures emitted",
    emitted_count_.load(std::memory_order_acquire));

  co_return true;
}

auto TextureEmitter::EnsureFallbackTexture() -> void
{
  emit::CookerConfig config {};
  config.packing_policy_id = std::string(emit::GetDefaultPackingPolicy().Id());
  auto fallback = emit::CreateFallbackTexture(config);
  const auto signature
    = TextureTableTraits::SignatureForDescriptor(fallback.desc);
  DCHECK_F(!signature.empty(), "fallback texture signature must not be empty");

  const auto acquire = table_aggregator_.AcquireOrInsert(signature, [&]() {
    const auto reserved
      = ReserveDataRange(util::kRowPitchAlignment, fallback.payload.size());
    fallback.desc.data_offset
      = static_cast<data::pak::OffsetT>(reserved.aligned_offset);
    return std::make_pair(fallback.desc, reserved);
  });

  if (!acquire.is_new) {
    return;
  }

  emitted_count_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved = acquire.reservation;
  if (reserved.padding_size > 0) {
    auto padding_ptr = std::make_shared<std::vector<std::byte>>(
      reserved.padding_size, std::byte { 0 });
    QueueDataWrite(WriteKind::kPadding, TextureKind::kFallback, std::nullopt,
      reserved.reservation_start, padding_ptr);
  }

  auto payload_ptr
    = std::make_shared<std::vector<std::byte>>(std::move(fallback.payload));
  QueueDataWrite(WriteKind::kPayload, TextureKind::kFallback, std::nullopt,
    reserved.aligned_offset, payload_ptr);
}

} // namespace oxygen::content::import
