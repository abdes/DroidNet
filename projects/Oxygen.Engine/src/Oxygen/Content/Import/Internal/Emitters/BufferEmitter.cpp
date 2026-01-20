//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <stdexcept>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Internal/Emitters/BufferEmitter.h>
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

  [[nodiscard]] auto MakeBufferSignature(const CookedBufferPayload& cooked)
    -> std::string
  {
    std::string signature;
    signature.reserve(96);

    // Dedupe signature is always present. When `content_hash` is available, it
    // is incorporated to distinguish buffers that share metadata.
    signature.append("buf:");
    signature.append("u=");
    signature.append(std::to_string(cooked.usage_flags));
    signature.append(";s=");
    signature.append(std::to_string(cooked.element_stride));
    signature.append(";f=");
    signature.append(std::to_string(cooked.element_format));
    signature.append(";a=");
    signature.append(std::to_string(cooked.alignment));
    signature.append(";n=");
    signature.append(std::to_string(cooked.data.size()));
    if (cooked.content_hash != 0) {
      signature.append(";h=");
      signature.append(std::to_string(cooked.content_hash));
    }
    return signature;
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

BufferEmitter::BufferEmitter(IAsyncFileWriter& file_writer,
  BufferTableAggregator& table_aggregator, const LooseCookedLayout& layout,
  const std::filesystem::path& cooked_root)
  : file_writer_(file_writer)
  , table_aggregator_(table_aggregator)
  , data_path_(cooked_root / layout.BuffersDataRelPath())
{
  data_file_size_.store(
    GetExistingDataSize(data_path_), std::memory_order_release);

  DLOG_F(INFO, "BufferEmitter created: data='{}'", data_path_.string());
}

BufferEmitter::~BufferEmitter()
{
  const auto pending = pending_count_.load(std::memory_order_acquire);
  if (pending > 0) {
    LOG_F(WARNING, "BufferEmitter destroyed with {} pending writes", pending);
  }
}

auto BufferEmitter::Emit(CookedBufferPayload cooked) -> uint32_t
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("BufferEmitter is finalized");
  }

  const auto signature = MakeBufferSignature(cooked);
  DCHECK_F(!signature.empty(), "buffer signature must not be empty");

  // Use buffer's specified alignment (defaults to 16)
  const auto buffer_alignment = cooked.alignment > 0 ? cooked.alignment : 16ULL;

  const auto acquire = table_aggregator_.AcquireOrInsert(signature, [&]() {
    const auto reserved
      = ReserveDataRange(buffer_alignment, cooked.data.size());
    auto desc = MakeTableEntry(cooked, reserved.aligned_offset);
    return std::make_pair(desc, reserved);
  });

  if (!acquire.is_new) {
    return acquire.index;
  }

  emitted_count_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved = acquire.reservation;

  DLOG_F(INFO,
    "BufferEmitter::Emit: index={} offset={} size={} padding={} "
    "usage=0x{:x} stride={}",
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

  return acquire.index;
}

auto BufferEmitter::ReserveDataRange(
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
    LOG_F(
      ERROR, "BufferEmitter: failed to write padding: {}", error.ToString());
    return;
  }

  LOG_F(ERROR, "BufferEmitter: failed to write buffer {}: {}",
    index.value_or(0), error.ToString());
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
  return data_file_size_.load(std::memory_order_acquire);
}

auto BufferEmitter::Finalize() -> co::Co<bool>
{
  finalize_started_.store(true, std::memory_order_release);

  DLOG_F(INFO, "BufferEmitter::Finalize: waiting for {} pending writes",
    pending_count_.load(std::memory_order_acquire));

  // Wait for all pending writes via flush
  auto flush_result = co_await file_writer_.Flush();

  if (!flush_result.has_value()) {
    LOG_F(ERROR, "BufferEmitter::Finalize: flush failed: {}",
      flush_result.error().ToString());
    co_return false;
  }

  // Check for accumulated errors
  const auto errors = error_count_.load(std::memory_order_acquire);
  if (errors > 0) {
    LOG_F(ERROR, "BufferEmitter::Finalize: {} I/O errors occurred", errors);
    co_return false;
  }

  DLOG_F(INFO, "BufferEmitter::Finalize: complete, {} buffers emitted",
    emitted_count_.load(std::memory_order_acquire));

  co_return true;
}

auto BufferEmitter::MakeTableEntry(
  const CookedBufferPayload& cooked, uint64_t data_offset) -> BufferResourceDesc
{
  using data::pak::DataBlobSizeT;
  using data::pak::OffsetT;

  BufferResourceDesc entry {};
  entry.data_offset = static_cast<OffsetT>(data_offset);
  entry.size_bytes = static_cast<DataBlobSizeT>(cooked.data.size());
  entry.usage_flags = cooked.usage_flags;
  entry.element_stride = cooked.element_stride;
  entry.element_format = cooked.element_format;
  entry.content_hash = cooked.content_hash;
  // entry.reserved is zero-initialized by default

  return entry;
}

} // namespace oxygen::content::import
