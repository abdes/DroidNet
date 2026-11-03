//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  //! The format of the fallback texture: one opaque white texel.
  constexpr auto kFallbackFormat = Format::kRGBA8UNorm;

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

  [[nodiscard]] auto DefaultPackingPolicyId() -> std::string_view
  {
#ifdef _WIN32
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
    -> data::pak::render::TexturePackingPolicyId
  {
    if (id == "tight") {
      return data::pak::render::TexturePackingPolicyId::kTightPacked;
    }
    return data::pak::render::TexturePackingPolicyId::kD3D12;
  }

  [[nodiscard]] auto BuildFallbackPayloadBytes(
    const ITexturePackingPolicy& policy,
    const data::pak::render::TexturePackingPolicyId policy_id)
    -> std::vector<std::byte>
  {
    LOG_SCOPE_FUNCTION(1);
    const auto aligned_pitch64 = policy.AlignRowPitchBytes(
      graphics::detail::RowBytes(kFallbackFormat, 1U));
    if (aligned_pitch64 > std::numeric_limits<uint32_t>::max()) {
      return {};
    }
    const auto aligned_pitch = static_cast<uint32_t>(aligned_pitch64);

    const uint32_t layouts_offset
      = sizeof(data::pak::render::TexturePayloadHeader);
    const uint32_t layouts_bytes = sizeof(data::pak::render::SubresourceLayout);

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

    data::pak::render::TexturePayloadHeader header {};
    header.magic = data::pak::render::kTexturePayloadMagic;
    header.packing_policy = static_cast<uint8_t>(policy_id);
    header.flags
      = static_cast<uint8_t>(data::pak::render::TexturePayloadFlags::kNone);
    header.subresource_count = 1;
    header.total_payload_size = static_cast<uint32_t>(total_payload64);
    header.layouts_offset_bytes = layouts_offset;
    header.data_offset_bytes = data_offset_bytes;

    const data::pak::render::SubresourceLayout layout {
      .offset_bytes = 0,
      .row_pitch_bytes = aligned_pitch,
      .size_bytes = aligned_pitch,
    };

    auto row_data = std::vector<std::byte>(aligned_pitch, std::byte { 0 });
    constexpr std::array white_pixel {
      std::byte { 0xFF },
      std::byte { 0xFF },
      std::byte { 0xFF },
      std::byte { 0xFF },
    };
    std::ranges::copy(white_pixel, row_data.begin());

    serio::MemoryStream stream;
    serio::Writer writer(stream);

    CheckResult(writer.Write(header), "payload_header");
    CheckResult(writer.Write(layout), "subresource_layout");

    const auto position = stream.Position().get();
    if (position > data_offset_bytes) {
      return {};
    }

    const size_t padding_size
      = data_offset_bytes - static_cast<uint32_t>(position);
    if (padding_size > 0) {
      std::vector padding(padding_size, std::byte { 0 });
      CheckResult(writer.WriteBlob(
                    std::span<const std::byte>(padding.data(), padding.size())),
        "payload_padding");
    }

    CheckResult(writer.WriteBlob(
                  std::span<const std::byte>(row_data.data(), row_data.size())),
      "payload_pixel");

    const auto payload_bytes = stream.Data();
    return { payload_bytes.begin(), payload_bytes.end() };
  }

} // namespace

TextureEmitter::TextureEmitter(
  co::Nursery& writes, TextureTableAggregator& table_aggregator, Config config)
  : writes_(writes)
  , table_aggregator_(table_aggregator)
  , config_(std::move(config))
  , data_path_(config_.cooked_root / internal::loose_layout::kTexturesData)
{
  if (config_.packing_policy_id.empty()) {
    config_.packing_policy_id = DefaultPackingPolicyId();
  }
  DLOG_F(INFO, "Created texture emitter: data='{}'", data_path_.string());
}

TextureEmitter::~TextureEmitter()
{
  const auto pending = writes_.Running();
  if (pending > 0) {
    LOG_F(WARNING, "Destroyed with {} pending writes", pending);
  }
}

auto TextureEmitter::Emit(CookedTexturePayload cooked) -> uint32_t
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("TextureEmitter is finalized");
  }
  if (cooked.payload.Empty()) {
    throw std::invalid_argument("the cooked texture is empty");
  }

  EnsureFallbackTexture();

  const auto acquire = table_aggregator_.Acquire(
    ToPakDescriptor(cooked), config_.data_alignment);

  RecordEmission(acquire.index);

  if (!acquire.is_new) {
    return acquire.index;
  }

  const auto reserved = acquire.reservation;
  DLOG_F(INFO, "Emit index={} offset={} size={} padding={} format={}",
    acquire.index, reserved.aligned_offset, cooked.payload.Size(),
    reserved.padding_size, static_cast<int>(cooked.desc.format));

  // Write padding if needed (before the texture data)
  if (reserved.padding_size > 0) {
    QueueDataWrite(WriteKind::kPadding, TextureKind::kUser, std::nullopt,
      reserved.reservation_start,
      std::vector<std::byte>(reserved.padding_size, std::byte { 0 }));
  }

  // Queue async write at explicit offset for texture data; the write owns it.
  QueueDataWrite(WriteKind::kPayload, TextureKind::kUser, acquire.index,
    reserved.aligned_offset, std::move(cooked.payload).TakeBytes());

  return acquire.index;
}

auto TextureEmitter::QueueDataWrite(const WriteKind kind,
  const TextureKind texture_kind, const std::optional<uint32_t> index,
  const uint64_t offset, std::vector<std::byte> data) -> void
{
  const auto file = table_aggregator_.DataFile();
  if (!file) {
    LOG_F(ERROR, "failed to open '{}': {}", data_path_.string(),
      file.error().message());
    writes_.Fail(data_path_, file.error());
    return;
  }
  writes_.Start(data_path_,
    WriteData(*file, kind, texture_kind, index, offset, std::move(data)));
}

auto TextureEmitter::WriteData(serio::AnyAsyncDevice* const file,
  const WriteKind kind, const TextureKind texture_kind,
  const std::optional<uint32_t> index, const uint64_t offset,
  std::vector<std::byte> data) -> co::Co<Result<void>>
{
  const auto written = co_await file->AsyncWriteAt(
    serio::ByteOffset { offset }, std::span<const std::byte>(data));
  if (!written) {
    const auto message = written.error().message();
    if (kind == WriteKind::kPadding) {
      LOG_F(ERROR, "failed to write {}padding to '{}': {}",
        texture_kind == TextureKind::kFallback ? "fallback " : "",
        data_path_.string(), message);
    } else if (texture_kind == TextureKind::kFallback) {
      LOG_F(ERROR, "failed to write fallback texture to '{}': {}",
        data_path_.string(), message);
    } else {
      LOG_F(ERROR, "failed to write texture {} to '{}': {}", index.value_or(0),
        data_path_.string(), message);
    }
  }
  co_return written;
}

auto TextureEmitter::GetStats() const noexcept -> Stats
{
  return {
    .emitted_textures = emitted_count_.load(std::memory_order_acquire),
    .data_file_size = table_aggregator_.DataFileSize(),
    .error_count = writes_.ErrorCount(),
  };
}

auto TextureEmitter::TryGetDescriptor(const uint32_t index) const
  -> std::optional<TextureResourceDesc>
{
  return table_aggregator_.TryGetDescriptor(index);
}

auto TextureEmitter::RecordEmission(const uint32_t index) -> void
{
  if (emitted_indices_.insert(index).second) {
    emitted_count_.fetch_add(1, std::memory_order_acq_rel);
  }
}

auto TextureEmitter::Finalize() -> co::Co<Result<void, WriteFailure>>
{
  finalize_started_.store(true, std::memory_order_release);
  EnsureFallbackTexture();
  DLOG_F(INFO, "Finalize pending={}", writes_.Running());

  co_return co_await writes_.Join();
}

auto TextureEmitter::CreateFallbackPayload() const -> CookedTexturePayload
{
  const auto& policy = ResolvePackingPolicy(config_.packing_policy_id);
  const auto policy_id = ResolvePackingPolicyId(config_.packing_policy_id);

  auto payload_bytes = BuildFallbackPayloadBytes(policy, policy_id);
  if (payload_bytes.empty()) {
    LOG_F(ERROR, "failed to build fallback payload");
    throw std::runtime_error("fallback payload build failed");
  }
  CookedTexturePayload cooked {};
  cooked.desc.texture_type = TextureType::kTexture2D;
  cooked.desc.width = 1;
  cooked.desc.height = 1;
  cooked.desc.depth = 1;
  cooked.desc.array_layers = 1;
  cooked.desc.mip_levels = 1;
  cooked.desc.format = kFallbackFormat;
  cooked.desc.packing_policy_id = config_.packing_policy_id;
  cooked.payload = HashedBytes(std::move(payload_bytes));
  return cooked;
}

auto TextureEmitter::ToPakDescriptor(const CookedTexturePayload& cooked)
  -> TextureResourceDesc
{
  TextureResourceDesc desc {};
  desc.data_offset = 0;
  desc.size_bytes
    = static_cast<data::pak::core::DataBlobSizeT>(cooked.payload.Size());
  desc.texture_type = static_cast<uint8_t>(cooked.desc.texture_type);
  desc.width = cooked.desc.width;
  desc.height = cooked.desc.height;
  desc.depth = cooked.desc.depth;
  desc.array_layers = cooked.desc.array_layers;
  desc.mip_levels = cooked.desc.mip_levels;
  desc.format = static_cast<uint8_t>(cooked.desc.format);
  desc.alignment = data::TextureResource::kDataAlignment;
  desc.content_hash = cooked.payload.Sha256();

  desc.compression_type
    = data::pak::core::TextureCompressionOf(cooked.desc.format);

  return desc;
}

auto TextureEmitter::EnsureFallbackTexture() -> void
{
  DLOG_SCOPE_FUNCTION(1);
  if (fallback_emitted_.load(std::memory_order_acquire)) {
    return;
  }

  CookedTexturePayload fallback = CreateFallbackPayload();
  const auto acquire = table_aggregator_.Acquire(
    ToPakDescriptor(fallback), config_.data_alignment);

  RecordEmission(acquire.index);

  if (!acquire.is_new) {
    fallback_emitted_.store(true, std::memory_order_release);
    return;
  }

  const auto reserved = acquire.reservation;
  if (reserved.padding_size > 0) {
    QueueDataWrite(WriteKind::kPadding, TextureKind::kFallback, std::nullopt,
      reserved.reservation_start,
      std::vector<std::byte>(reserved.padding_size, std::byte { 0 }));
  }

  QueueDataWrite(WriteKind::kPayload, TextureKind::kFallback, std::nullopt,
    reserved.aligned_offset, std::move(fallback.payload).TakeBytes());

  fallback_emitted_.store(true, std::memory_order_release);
}

} // namespace oxygen::content::import
