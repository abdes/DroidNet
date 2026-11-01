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
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/DedupCollision.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Types.h>
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

  [[nodiscard]] auto MakeTextureSignature(const CookedTexturePayload& cooked,
    const data::pak::core::TextureResourceDesc& desc,
    const std::string_view signature_salt) -> std::string
  {
    static_cast<void>(signature_salt);
    auto signature = TextureTableTraits::SignatureForDescriptor(desc);
    if (desc.content_hash == 0U) {
      signature.append(";fp=");
      signature.append(std::to_string(ComputeFastFingerprint(cooked.payload)));
    }
    return signature;
  }

  [[nodiscard]] auto ComputeTextureIdentity(const CookedTexturePayload& cooked)
    -> std::string
  {
    std::string identity;
    identity.reserve(128);
    identity.append("tex:");
    identity.append("w=");
    identity.append(std::to_string(cooked.desc.width));
    identity.append("x");
    identity.append(std::to_string(cooked.desc.height));
    identity.append(";m=");
    identity.append(std::to_string(cooked.desc.mip_levels));
    identity.append(";f=");
    identity.append(std::to_string(static_cast<uint8_t>(cooked.desc.format)));
    identity.append(";n=");
    identity.append(std::to_string(cooked.payload.size()));
    if (cooked.desc.content_hash != 0U) {
      identity.append(";h=");
      identity.append(std::to_string(cooked.desc.content_hash));
      return identity;
    }
    identity.append(";fp=");
    identity.append(std::to_string(ComputeFastFingerprint(cooked.payload)));
    return identity;
  }

  [[nodiscard]] auto BuildFallbackPayloadBytes(
    const ITexturePackingPolicy& policy,
    const data::pak::render::TexturePackingPolicyId policy_id,
    const bool with_content_hashing) -> std::vector<std::byte>
  {
    LOG_SCOPE_FUNCTION(1);
    constexpr uint32_t kUnalignedPitch = 4;
    const uint32_t aligned_pitch = policy.AlignRowPitchBytes(kUnalignedPitch);

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
    if (with_content_hashing) {
      header.content_hash = util::ComputeContentHash(payload_bytes);

      CheckResult(stream.Seek(serio::ByteOffset { 0 }), "stream_seek");
      CheckResult(writer.Write(header), "payload_header_hash");
    }

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

auto TextureEmitter::Emit(CookedTexturePayload cooked,
  const std::string_view signature_salt) -> uint32_t
{
  if (finalize_started_.load(std::memory_order_acquire)) {
    throw std::runtime_error("TextureEmitter is finalized");
  }

  const auto identity = ComputeTextureIdentity(cooked);
  DCHECK_F(!identity.empty(), "texture identity must not be empty");
  if (cooked.desc.content_hash == 0U && !signature_salt.empty()) {
    const std::string key(signature_salt);
    if (const auto it = identity_by_key_.find(key);
      it != identity_by_key_.end() && it->second != identity) {
      const auto existing_index_it = index_by_key_.find(key);
      const uint32_t existing_index = (existing_index_it != index_by_key_.end())
        ? existing_index_it->second
        : data::pak::core::kFallbackResourceIndex;
      const auto action
        = internal::DedupCollisionAction(config_.collision_policy);
      ImportDiagnostic diagnostic {
        .severity = (config_.collision_policy == DedupCollisionPolicy::kError)
          ? ImportSeverity::kError
          : ImportSeverity::kWarning,
        .code = "import.dedup_collision.texture",
        .message = "policy=" + to_string(config_.collision_policy)
          + ";action=" + std::string(action)
          + ";existing_index=" + std::to_string(existing_index),
        .source_path = key,
        .object_path = "texture",
      };
      if (config_.on_dedup_diagnostic) {
        config_.on_dedup_diagnostic(diagnostic);
      }
      if (config_.collision_policy == DedupCollisionPolicy::kError) {
        LOG_F(ERROR,
          "texture dedup collision: policy={} key='{}' existing_index={} "
          "action={}",
          to_string(config_.collision_policy), key, existing_index, "error");
        throw std::runtime_error("texture dedup collision");
      }
      LOG_F(WARNING,
        "texture dedup collision: policy={} key='{}' existing_index={} "
        "action={}",
        to_string(config_.collision_policy), key, existing_index,
        (config_.collision_policy == DedupCollisionPolicy::kWarnReplace)
          ? "replace"
          : "keep_first");
      if (config_.collision_policy == DedupCollisionPolicy::kWarnKeepFirst) {
        return existing_index;
      }
    }
  }

  EnsureFallbackTexture();

  const auto tmp_desc = ToPakDescriptor(cooked, 0);
  const auto signature = MakeTextureSignature(cooked, tmp_desc, signature_salt);
  DCHECK_F(!signature.empty(), "texture signature must not be empty");
  const auto acquire = table_aggregator_.AcquireOrInsert(signature,
    [&] -> std::pair<data::pak::core::TextureResourceDesc, WriteReservation> {
      const auto reserved = table_aggregator_.ReserveDataRange({
        .alignment = config_.data_alignment,
        .payload_size = cooked.payload.size(),
      });
      auto desc = ToPakDescriptor(cooked, reserved.aligned_offset);
      return std::make_pair(desc, reserved);
    });

  RecordEmissionSignature(signature);

  if (!acquire.is_new) {
    if (cooked.desc.content_hash == 0U && !signature_salt.empty()) {
      const std::string key(signature_salt);
      identity_by_key_[key] = identity;
      index_by_key_[key] = acquire.index;
    }
    return acquire.index;
  }

  UpdateDataFileSize(
    acquire.reservation.aligned_offset + cooked.payload.size());

  const auto reserved = acquire.reservation;
  DLOG_F(INFO, "Emit index={} offset={} size={} padding={} format={}",
    acquire.index, reserved.aligned_offset, cooked.payload.size(),
    reserved.padding_size, static_cast<int>(cooked.desc.format));

  // Write padding if needed (before the texture data)
  if (reserved.padding_size > 0) {
    QueueDataWrite(WriteKind::kPadding, TextureKind::kUser, std::nullopt,
      reserved.reservation_start,
      std::vector<std::byte>(reserved.padding_size, std::byte { 0 }));
  }

  // Queue async write at explicit offset for texture data; the write owns it.
  QueueDataWrite(WriteKind::kPayload, TextureKind::kUser, acquire.index,
    reserved.aligned_offset, std::move(cooked.payload));

  if (cooked.desc.content_hash == 0U && !signature_salt.empty()) {
    const std::string key(signature_salt);
    identity_by_key_[key] = identity;
    index_by_key_[key] = acquire.index;
  }

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
    .data_file_size = data_file_size_.load(std::memory_order_acquire),
    .error_count = writes_.ErrorCount(),
  };
}

auto TextureEmitter::TryGetDescriptor(const uint32_t index) const
  -> std::optional<TextureResourceDesc>
{
  return table_aggregator_.TryGetDescriptor(index);
}

auto TextureEmitter::UpdateDataFileSize(const uint64_t new_size) -> void
{
  auto current = data_file_size_.load(std::memory_order_acquire);
  while (current < new_size
    && !data_file_size_.compare_exchange_weak(
      current, new_size, std::memory_order_acq_rel)) { }
}

auto TextureEmitter::RecordEmissionSignature(const std::string& signature)
  -> void
{
  if (signature.empty()) {
    return;
  }

  if (emitted_signatures_.insert(signature).second) {
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

  auto payload_bytes = BuildFallbackPayloadBytes(
    policy, policy_id, config_.with_content_hashing);
  if (payload_bytes.empty()) {
    LOG_F(ERROR, "failed to build fallback payload");
    throw std::runtime_error("fallback payload build failed");
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
    = static_cast<data::pak::core::DataBlobSizeT>(cooked.payload.size());
  desc.texture_type = static_cast<uint8_t>(cooked.desc.texture_type);
  desc.compression_type = 0;
  desc.width = cooked.desc.width;
  desc.height = cooked.desc.height;
  desc.depth = cooked.desc.depth;
  desc.array_layers = cooked.desc.array_layers;
  desc.mip_levels = cooked.desc.mip_levels;
  desc.format = static_cast<uint8_t>(cooked.desc.format);
  desc.alignment = static_cast<uint16_t>(policy.AlignRowPitchBytes(1));
  desc.content_hash = (cooked.desc.content_hash != 0U)
    ? cooked.desc.content_hash
    : ComputeFastFingerprint(cooked.payload);

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
  const auto signature = MakeTextureSignature(fallback, tmp_desc, "fallback");
  DCHECK_F(!signature.empty(), "fallback texture signature must not be empty");

  const auto acquire = table_aggregator_.AcquireOrInsert(signature,
    [&] -> std::pair<data::pak::core::TextureResourceDesc, WriteReservation> {
      const auto reserved = table_aggregator_.ReserveDataRange({
        .alignment = config_.data_alignment,
        .payload_size = fallback.payload.size(),
      });
      auto desc = ToPakDescriptor(fallback, reserved.aligned_offset);
      return std::make_pair(desc, reserved);
    });

  RecordEmissionSignature(signature);

  if (!acquire.is_new) {
    fallback_emitted_.store(true, std::memory_order_release);
    return;
  }

  UpdateDataFileSize(
    acquire.reservation.aligned_offset + fallback.payload.size());

  const auto reserved = acquire.reservation;
  if (reserved.padding_size > 0) {
    QueueDataWrite(WriteKind::kPadding, TextureKind::kFallback, std::nullopt,
      reserved.reservation_start,
      std::vector<std::byte>(reserved.padding_size, std::byte { 0 }));
  }

  QueueDataWrite(WriteKind::kPayload, TextureKind::kFallback, std::nullopt,
    reserved.aligned_offset, std::vector<std::byte>(fallback.payload));

  fallback_emitted_.store(true, std::memory_order_release);
}

} // namespace oxygen::content::import
