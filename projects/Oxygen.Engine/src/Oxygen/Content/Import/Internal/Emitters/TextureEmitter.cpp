//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  // Accepts any Result<T> and checks for error, logs and throws if needed.
  template <typename ResultT>
  auto CheckResult(const ResultT& result, const char* field_name) -> void
  {
    if (result) {
      return;
    }

    LOG_F(ERROR, "-failed- on {}: {}", field_name,
      result.error().message().c_str());

    std::string message = "error building texture payload (";
    message.append(field_name);
    message.append("): ");
    message.append(result.error().message());
    throw std::runtime_error(message);
  }

  [[nodiscard]] auto DefaultPackingPolicyId() -> std::string
  {
#if defined(_WIN32)
    return "d3d12";
#else
    return "tight";
#endif
  }

  [[nodiscard]] auto ResolvePackingPolicy(std::string_view id)
    -> const ITexturePackingPolicy&
  {
    if (id == "tight") {
      return TightPackedPolicy::Instance();
    }
    if (id != "d3d12") {
      LOG_F(WARNING, "unknown packing policy '{}', using 'd3d12'",
        std::string(id).c_str());
    }
    return D3D12PackingPolicy::Instance();
  }

  [[nodiscard]] auto ResolvePackingPolicyId(std::string_view id) noexcept
    -> data::pak::TexturePackingPolicyId
  {
    if (id == "tight") {
      return data::pak::TexturePackingPolicyId::kTightPacked;
    }
    return data::pak::TexturePackingPolicyId::kD3D12;
  }

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

  [[nodiscard]] auto BuildFallbackPayloadBytes(
    const ITexturePackingPolicy& policy,
    const data::pak::TexturePackingPolicyId policy_id,
    const bool with_content_hashing) -> std::vector<std::byte>
  {
    LOG_SCOPE_FUNCTION(1);
    constexpr uint32_t kUnalignedPitch = 4;
    const uint32_t aligned_pitch = policy.AlignRowPitchBytes(kUnalignedPitch);

    const uint32_t layouts_offset
      = static_cast<uint32_t>(sizeof(data::pak::TexturePayloadHeader));
    const uint32_t layouts_bytes
      = static_cast<uint32_t>(sizeof(data::pak::SubresourceLayout));

    const uint64_t data_offset64
      = policy.AlignSubresourceOffset(layouts_offset + layouts_bytes);
    if (data_offset64 > std::numeric_limits<uint32_t>::max()) {
      return {};
    }
    const auto data_offset_bytes = static_cast<uint32_t>(data_offset64);

    const uint64_t payload_data_size = aligned_pitch;
    const uint64_t total_payload64 = data_offset64 + payload_data_size;
    if (total_payload64 > std::numeric_limits<uint32_t>::max()) {
      return {};
    }

    data::pak::TexturePayloadHeader header {};
    header.magic = data::pak::kTexturePayloadMagic;
    header.packing_policy = static_cast<uint8_t>(policy_id);
    header.flags = static_cast<uint8_t>(data::pak::TexturePayloadFlags::kNone);
    header.subresource_count = 1;
    header.total_payload_size = static_cast<uint32_t>(total_payload64);
    header.layouts_offset_bytes = layouts_offset;
    header.data_offset_bytes = data_offset_bytes;

    const data::pak::SubresourceLayout layout {
      .offset_bytes = 0,
      .row_pitch_bytes = aligned_pitch,
      .size_bytes = aligned_pitch,
    };

    const std::array<std::byte, 4> white_pixel { std::byte { 0xFF },
      std::byte { 0xFF }, std::byte { 0xFF }, std::byte { 0xFF } };

    oxygen::serio::MemoryStream stream;
    oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(stream);

    CheckResult(writer.Write(header), "payload_header");
    CheckResult(writer.Write(layout), "subresource_layout");

    const auto position = stream.Position();
    CheckResult(position, "stream_position");
    if (position.value() > data_offset_bytes) {
      return {};
    }

    const size_t padding_size
      = data_offset_bytes - static_cast<uint32_t>(position.value());
    if (padding_size > 0) {
      std::vector<std::byte> padding(padding_size, std::byte { 0 });
      CheckResult(writer.WriteBlob(
                    std::span<const std::byte>(padding.data(), padding.size())),
        "payload_padding");
    }

    CheckResult(writer.WriteBlob(std::span<const std::byte>(white_pixel)),
      "payload_pixel");

    const auto payload_bytes = stream.Data();
    if (with_content_hashing) {
      header.content_hash = util::ComputeContentHash(payload_bytes);

      CheckResult(stream.Seek(0), "stream_seek");
      CheckResult(writer.Write(header), "payload_header_hash");
    }

    return std::vector<std::byte>(payload_bytes.begin(), payload_bytes.end());
  }

} // namespace

TextureEmitter::TextureEmitter(IAsyncFileWriter& file_writer,
  TextureTableAggregator& table_aggregator, Config config)
  : file_writer_(file_writer)
  , table_aggregator_(table_aggregator)
  , config_(std::move(config))
  , data_path_(config_.cooked_root / config_.layout.TexturesDataRelPath())
{
  if (config_.packing_policy_id.empty()) {
    config_.packing_policy_id = DefaultPackingPolicyId();
  }

  data_file_size_.store(
    GetExistingDataSize(data_path_), std::memory_order_release);

  DLOG_F(INFO, "created data='{}'", data_path_.string());
}

TextureEmitter::~TextureEmitter()
{
  const auto pending = pending_count_.load(std::memory_order_acquire);
  if (pending > 0) {
    LOG_F(WARNING, "destroyed with {} pending writes", pending);
  }
}

auto TextureEmitter::Emit(CookedTexturePayload cooked) -> uint32_t
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("TextureEmitter is finalized");
  }

  EnsureFallbackTexture();

  const auto tmp_desc = ToPakDescriptor(cooked, 0);
  const auto signature = TextureTableTraits::SignatureForDescriptor(tmp_desc);
  DCHECK_F(!signature.empty(), "texture signature must not be empty");
  const auto acquire = table_aggregator_.AcquireOrInsert(signature, [&]() {
    const auto reserved
      = ReserveDataRange(config_.data_alignment, cooked.payload.size());
    auto desc = ToPakDescriptor(cooked, reserved.aligned_offset);
    return std::make_pair(desc, reserved);
  });

  if (!acquire.is_new) {
    return acquire.index;
  }

  emitted_count_.fetch_add(1, std::memory_order_acq_rel);

  const auto reserved = acquire.reservation;
  DLOG_SCOPE_F(INFO, "Emit index={} offset={} size={} padding={} format={}",
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
      LOG_F(ERROR, "failed to write fallback padding: {}", error.ToString());
      return;
    }

    LOG_F(ERROR, "failed to write padding: {}", error.ToString());
    return;
  }

  if (texture_kind == TextureKind::kFallback) {
    LOG_F(ERROR, "failed to write fallback texture: {}", error.ToString());
    return;
  }

  LOG_F(ERROR, "failed to write texture {}: {}", index.value_or(0),
    error.ToString());
}

auto TextureEmitter::GetStats() const noexcept -> Stats
{
  return {
    .emitted_textures = emitted_count_.load(std::memory_order_acquire),
    .data_file_size = data_file_size_.load(std::memory_order_acquire),
    .pending_writes = pending_count_.load(std::memory_order_acquire),
    .error_count = error_count_.load(std::memory_order_acquire),
  };
}

auto TextureEmitter::Finalize() -> co::Co<bool>
{
  finalize_started_.store(true, std::memory_order_release);
  EnsureFallbackTexture();
  DLOG_SCOPE_F(INFO, "Finalize pending={}",
    pending_count_.load(std::memory_order_acquire));

  // Wait for all pending writes via flush
  auto flush_result = co_await file_writer_.Flush();

  if (!flush_result.has_value()) {
    LOG_F(ERROR, "flush failed: {}", flush_result.error().ToString());
    co_return false;
  }

  // Check for accumulated errors
  const auto errors = error_count_.load(std::memory_order_acquire);
  if (errors > 0) {
    LOG_F(ERROR, "I/O errors occurred: {}", errors);
    co_return false;
  }

  co_return true;
}

auto TextureEmitter::CreateFallbackPayload() const -> CookedTexturePayload
{
  const auto& policy = ResolvePackingPolicy(config_.packing_policy_id);
  const auto policy_id = ResolvePackingPolicyId(config_.packing_policy_id);

  auto payload_bytes = BuildFallbackPayloadBytes(
    policy, policy_id, config_.with_content_hashing);
  if (payload_bytes.empty()) {
    LOG_F(ERROR, "failed to build fallback payload");
    throw std::runtime_error("TextureEmitter: fallback payload build failed");
  }
  const auto content_hash = config_.with_content_hashing
    ? util::ComputeContentHash(payload_bytes)
    : 0U;

  CookedTexturePayload cooked {};
  cooked.desc.texture_type = TextureType::kTexture2D;
  cooked.desc.width = 1;
  cooked.desc.height = 1;
  cooked.desc.depth = 1;
  cooked.desc.array_layers = 1;
  cooked.desc.mip_levels = 1;
  cooked.desc.format = Format::kRGBA8UNorm;
  cooked.desc.packing_policy_id = config_.packing_policy_id;
  cooked.desc.content_hash = content_hash;
  cooked.payload = std::move(payload_bytes);
  return cooked;
}

auto TextureEmitter::ToPakDescriptor(const CookedTexturePayload& cooked,
  const uint64_t data_offset) const -> TextureResourceDesc
{
  const auto policy_id = cooked.desc.packing_policy_id.empty()
    ? config_.packing_policy_id
    : cooked.desc.packing_policy_id;
  const auto& policy = ResolvePackingPolicy(policy_id);

  TextureResourceDesc desc {};
  desc.data_offset = data_offset;
  desc.size_bytes
    = static_cast<data::pak::DataBlobSizeT>(cooked.payload.size());
  desc.texture_type = static_cast<uint8_t>(cooked.desc.texture_type);
  desc.compression_type = 0;
  desc.width = cooked.desc.width;
  desc.height = cooked.desc.height;
  desc.depth = cooked.desc.depth;
  desc.array_layers = cooked.desc.array_layers;
  desc.mip_levels = cooked.desc.mip_levels;
  desc.format = static_cast<uint8_t>(cooked.desc.format);
  desc.alignment = static_cast<uint16_t>(policy.AlignRowPitchBytes(1));
  desc.content_hash = cooked.desc.content_hash;

  switch (cooked.desc.format) {
  case Format::kBC7UNorm:
  case Format::kBC7UNormSRGB:
    desc.compression_type = 7;
    break;
  default:
    desc.compression_type = 0;
    break;
  }

  return desc;
}

auto TextureEmitter::EnsureFallbackTexture() -> void
{
  DLOG_SCOPE_FUNCTION(1);
  if (fallback_emitted_.load(std::memory_order_acquire)) {
    return;
  }

  CookedTexturePayload fallback = CreateFallbackPayload();
  const auto tmp_desc = ToPakDescriptor(fallback, 0);
  const auto signature = TextureTableTraits::SignatureForDescriptor(tmp_desc);
  DCHECK_F(!signature.empty(), "fallback texture signature must not be empty");

  const auto acquire = table_aggregator_.AcquireOrInsert(signature, [&]() {
    const auto reserved
      = ReserveDataRange(config_.data_alignment, fallback.payload.size());
    auto desc = ToPakDescriptor(fallback, reserved.aligned_offset);
    return std::make_pair(desc, reserved);
  });

  if (!acquire.is_new) {
    fallback_emitted_.store(true, std::memory_order_release);
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

  auto payload_ptr = std::make_shared<std::vector<std::byte>>(fallback.payload);
  QueueDataWrite(WriteKind::kPayload, TextureKind::kFallback, std::nullopt,
    reserved.aligned_offset, payload_ptr);

  fallback_emitted_.store(true, std::memory_order_release);
}

} // namespace oxygen::content::import
