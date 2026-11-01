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
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import {

BufferEmitter::BufferEmitter(co::Nursery& writes,
  BufferTableAggregator& table_aggregator,
  const std::filesystem::path& cooked_root)
  : writes_(writes)
  , table_aggregator_(table_aggregator)
  , data_path_(cooked_root / internal::loose_layout::kBuffersData)
{
  DLOG_F(INFO, "Created buffer emitter: data='{}'", data_path_.string());
}

BufferEmitter::~BufferEmitter()
{
  const auto pending = writes_.Running();
  if (pending > 0) {
    LOG_F(WARNING, "Destroyed with {} pending writes", pending);
  }
}

auto BufferEmitter::Emit(CookedBufferPayload cooked) -> uint32_t
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("BufferEmitter is finalized");
  }
  if (base::IsAllZero(cooked.content_hash)) {
    throw std::invalid_argument("the cooked buffer has no SHA-256");
  }

  const auto signature
    = BufferTableTraits::SignatureForDescriptor(MakeTableEntry(cooked, 0));

  // Use buffer's specified alignment (defaults to 16)
  const auto buffer_alignment = cooked.alignment > 0 ? cooked.alignment : 16ULL;

  const auto acquire = table_aggregator_.AcquireOrInsert(signature,
    [&] -> std::pair<data::pak::core::BufferResourceDesc, WriteReservation> {
      const auto reserved = table_aggregator_.ReserveDataRange({
        .alignment = buffer_alignment,
        .payload_size = cooked.data.size(),
      });
      auto desc = MakeTableEntry(cooked, reserved.aligned_offset);
      return std::make_pair(desc, reserved);
    });

  if (!acquire.is_new) {
    return acquire.index;
  }

  emitted_count_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved = acquire.reservation;

  DLOG_F(INFO,
    "Emit: index={} offset={} size={} padding={} usage=0x{:x} stride={}",
    acquire.index, reserved.aligned_offset, cooked.data.size(),
    reserved.padding_size, cooked.usage_flags, cooked.element_stride);

  if (reserved.padding_size > 0) {
    QueueDataWrite(WriteKind::kPadding, std::nullopt,
      reserved.reservation_start,
      std::vector<std::byte>(reserved.padding_size, std::byte { 0 }));
  }

  // Queue async write at explicit offset for buffer data; the write owns it.
  QueueDataWrite(WriteKind::kPayload, acquire.index, reserved.aligned_offset,
    std::move(cooked.data));

  return acquire.index;
}

auto BufferEmitter::QueueDataWrite(const WriteKind kind,
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

auto BufferEmitter::WriteData(serio::AnyAsyncDevice* const file,
  const WriteKind kind, const std::optional<uint32_t> index,
  const uint64_t offset, std::vector<std::byte> data) -> co::Co<Result<void>>
{
  const auto written = co_await file->AsyncWriteAt(
    serio::ByteOffset { offset }, std::span<const std::byte>(data));
  if (!written) {
    if (kind == WriteKind::kPadding) {
      LOG_F(ERROR, "Failed to write padding to '{}': {}", data_path_.string(),
        written.error().message());
    } else {
      LOG_F(ERROR, "Failed to write buffer {} to '{}': {}", index.value_or(0),
        data_path_.string(), written.error().message());
    }
  }
  co_return written;
}

auto BufferEmitter::Count() const noexcept -> uint32_t
{
  return emitted_count_.load(std::memory_order_acquire);
}

auto BufferEmitter::ErrorCount() const noexcept -> size_t
{
  return writes_.ErrorCount();
}

auto BufferEmitter::DataFileSize() const noexcept -> uint64_t
{
  return table_aggregator_.DataFileSize();
}

auto BufferEmitter::TryGetDescriptor(const uint32_t index) const
  -> std::optional<BufferResourceDesc>
{
  return table_aggregator_.TryGetDescriptor(index);
}

auto BufferEmitter::Finalize() -> co::Co<Result<void, WriteFailure>>
{
  finalize_started_.store(true, std::memory_order_release);

  DLOG_F(INFO, "Finalize: waiting for {} pending writes", writes_.Running());

  co_return co_await writes_.Join();
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
  entry.content_hash = cooked.content_hash;
  // entry.reserved is zero-initialized by default

  return entry;
}

} // namespace oxygen::content::import
