//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
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

  [[nodiscard]] auto MakeTextureSignature(
    const oxygen::data::pak::TextureResourceDesc& desc) -> std::string
  {
    std::string signature;
    signature.reserve(96);

    signature.append("tex:");
    signature.append(std::to_string(desc.content_hash));
    signature.append(";w=");
    signature.append(std::to_string(desc.width));
    signature.append("x");
    signature.append(std::to_string(desc.height));
    signature.append(";m=");
    signature.append(std::to_string(desc.mip_levels));
    signature.append(";f=");
    signature.append(std::to_string(desc.format));
    signature.append(";a=");
    signature.append(std::to_string(desc.alignment));
    signature.append(";n=");
    signature.append(std::to_string(desc.size_bytes));
    return signature;
  }

  auto LoadExistingTable(const std::filesystem::path& table_path,
    std::vector<oxygen::data::pak::TextureResourceDesc>& table,
    std::unordered_map<std::string, uint32_t>& index_by_signature) -> void
  {
    if (!std::filesystem::exists(table_path)) {
      return;
    }

    std::ifstream in(table_path, std::ios::binary | std::ios::ate);
    if (!in) {
      LOG_F(WARNING, "TextureEmitter: failed to open existing table '{}'",
        table_path.string());
      return;
    }

    const auto size = in.tellg();
    if (size <= 0) {
      return;
    }

    const auto size_bytes = static_cast<size_t>(size);
    if (size_bytes % sizeof(oxygen::data::pak::TextureResourceDesc) != 0) {
      LOG_F(WARNING,
        "TextureEmitter: invalid table size {} for '{}' (entry size {})",
        size_bytes, table_path.string(),
        sizeof(oxygen::data::pak::TextureResourceDesc));
      return;
    }

    const auto count
      = size_bytes / sizeof(oxygen::data::pak::TextureResourceDesc);
    table.resize(count);
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(table.data()),
      static_cast<std::streamsize>(size_bytes));
    if (!in) {
      LOG_F(WARNING, "TextureEmitter: failed to read existing table '{}'",
        table_path.string());
      table.clear();
      return;
    }

    index_by_signature.clear();
    for (uint32_t i = 0; i < table.size(); ++i) {
      const auto signature = MakeTextureSignature(table[i]);
      if (!signature.empty()) {
        index_by_signature.emplace(signature, i);
      }
    }
  }

  [[nodiscard]] auto GetExistingDataSize(const std::filesystem::path& data_path,
    const std::vector<oxygen::data::pak::TextureResourceDesc>& table)
    -> uint64_t
  {
    std::error_code ec;
    const auto size = std::filesystem::file_size(data_path, ec);
    if (!ec) {
      return size;
    }

    uint64_t max_end = 0;
    for (const auto& entry : table) {
      const auto end = entry.data_offset + entry.size_bytes;
      if (end > max_end) {
        max_end = end;
      }
    }

    if (max_end > 0) {
      LOG_F(WARNING,
        "TextureEmitter: data file '{}' missing; using derived size {}",
        data_path.string(), max_end);
    }

    return max_end;
  }

} // namespace

TextureEmitter::TextureEmitter(IAsyncFileWriter& file_writer,
  const LooseCookedLayout& layout, const std::filesystem::path& cooked_root)
  : file_writer_(file_writer)
  , data_path_(cooked_root / layout.TexturesDataRelPath())
  , table_path_(cooked_root / layout.TexturesTableRelPath())
{
  LoadExistingTable(table_path_, table_, index_by_signature_);
  if (!table_.empty()) {
    next_index_.store(
      static_cast<uint32_t>(table_.size()), std::memory_order_release);
    data_file_size_.store(
      GetExistingDataSize(data_path_, table_), std::memory_order_release);
  }

  DLOG_F(INFO, "TextureEmitter created: data='{}' table='{}'",
    data_path_.string(), table_path_.string());
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
  const auto signature = MakeTextureSignature(tmp_desc);
  DCHECK_F(!signature.empty(), "texture signature must not be empty");
  if (const auto existing = FindExistingIndex(signature);
    existing.has_value()) {
    return existing.value();
  }

  // Assign index atomically (stable immediately)
  const auto index = next_index_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved
    = ReserveDataRange(util::kRowPitchAlignment, cooked.payload.size());
  RecordTextureEntry(signature, index, cooked, reserved.aligned_offset);

  DLOG_F(INFO,
    "TextureEmitter::Emit: index={} offset={} size={} padding={} format={}",
    index, reserved.aligned_offset, cooked.payload.size(),
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
  QueueDataWrite(WriteKind::kPayload, TextureKind::kUser, index,
    reserved.aligned_offset, payload_ptr);

  return index;
}

auto TextureEmitter::FindExistingIndex(const std::string& signature) const
  -> std::optional<uint32_t>
{
  const auto it = index_by_signature_.find(signature);
  if (it == index_by_signature_.end()) {
    return std::nullopt;
  }
  return it->second;
}

auto TextureEmitter::ReserveDataRange(
  const uint64_t alignment, const uint64_t payload_size) -> ReservedWriteRange
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

auto TextureEmitter::RecordTextureEntry(const std::string& signature,
  const uint32_t index, const CookedTexturePayload& cooked,
  const uint64_t aligned_offset) -> void
{
  table_.push_back(emit::ToPakDescriptor(cooked, aligned_offset));
  index_by_signature_.emplace(signature, index);
}

auto TextureEmitter::RecordFallbackEntry(
  const std::string& signature, const TextureResourceDesc& desc) -> void
{
  const uint32_t index = 0;
  table_.push_back(desc);
  index_by_signature_.emplace(signature, index);

  // Next user-emitted texture starts at index 1.
  next_index_.store(1, std::memory_order_release);
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
  return next_index_.load(std::memory_order_acquire);
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

  EnsureFallbackTexture();

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

  // Write table file if we have any textures
  if (!table_.empty()) {
    const auto table_ok = co_await WriteTableFile();
    if (!table_ok) {
      co_return false;
    }
  }

  DLOG_F(INFO, "TextureEmitter::Finalize: complete, {} textures emitted",
    table_.size());

  co_return true;
}

auto TextureEmitter::MakeTableEntry(const CookedTexturePayload& cooked,
  const uint64_t data_offset) -> TextureResourceDesc
{
  return emit::ToPakDescriptor(cooked, data_offset);
}

auto TextureEmitter::EnsureFallbackTexture() -> void
{
  if (!table_.empty()) {
    return;
  }

  emit::CookerConfig config {};
  config.packing_policy_id = std::string(emit::GetDefaultPackingPolicy().Id());
  auto fallback = emit::CreateFallbackTexture(config);

  const auto reserved
    = ReserveDataRange(util::kRowPitchAlignment, fallback.payload.size());

  fallback.desc.data_offset
    = static_cast<data::pak::OffsetT>(reserved.aligned_offset);
  const auto signature = MakeTextureSignature(fallback.desc);
  DCHECK_F(!signature.empty(), "fallback texture signature must not be empty");

  RecordFallbackEntry(signature, fallback.desc);

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

auto TextureEmitter::WriteTableFile() -> co::Co<bool>
{
  DLOG_F(INFO, "TextureEmitter::WriteTableFile: writing {} entries to '{}'",
    table_.size(), table_path_.string());

  // Serialize table entries to bytes
  serio::MemoryStream stream;
  serio::Writer<serio::MemoryStream> writer(stream);

  // Use alignment of 1 for packed table (matches existing sync code)
  const auto pack = writer.ScopedAlignment(1);
  auto write_result = writer.WriteBlob(std::as_bytes(std::span(table_)));
  if (!write_result.has_value()) {
    LOG_F(ERROR, "TextureEmitter::WriteTableFile: serialization failed");
    co_return false;
  }

  // Write table file
  auto result = co_await file_writer_.Write(table_path_,
    std::span<const std::byte>(stream.Data()),
    WriteOptions { .create_directories = true });

  if (!result.has_value()) {
    LOG_F(ERROR, "TextureEmitter::WriteTableFile: failed: {}",
      result.error().ToString());
    co_return false;
  }

  DLOG_F(
    INFO, "TextureEmitter::WriteTableFile: wrote {} bytes", result.value());

  co_return true;
}

} // namespace oxygen::content::import
