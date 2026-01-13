//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/emit/TextureEmissionUtils.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/ImageDecode.h>
#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import::emit {

namespace {

  [[nodiscard]] auto ToPackingPolicyId(const std::string_view id) noexcept
    -> std::optional<oxygen::data::pak::TexturePackingPolicyId>
  {
    if (id == "d3d12") {
      return oxygen::data::pak::TexturePackingPolicyId::kD3D12;
    }
    if (id == "tight") {
      return oxygen::data::pak::TexturePackingPolicyId::kTightPacked;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto BuildPlaceholderPayloadV4(
    const ITexturePackingPolicy& policy,
    const oxygen::data::pak::TexturePackingPolicyId policy_id,
    const std::array<std::byte, 4> pixel_rgba8) -> std::vector<std::byte>
  {
    const uint32_t unaligned_pitch = 4;
    const uint32_t aligned_pitch = policy.AlignRowPitchBytes(unaligned_pitch);

    const uint32_t layouts_offset
      = static_cast<uint32_t>(sizeof(oxygen::data::pak::TexturePayloadHeader));
    const uint32_t layouts_bytes
      = static_cast<uint32_t>(sizeof(oxygen::data::pak::SubresourceLayout));

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

    oxygen::data::pak::TexturePayloadHeader header {};
    header.magic = oxygen::data::pak::kTexturePayloadMagic;
    header.packing_policy = static_cast<uint8_t>(policy_id);
    header.flags
      = static_cast<uint8_t>(oxygen::data::pak::TexturePayloadFlags::kNone);
    header.subresource_count = 1;
    header.total_payload_size = static_cast<uint32_t>(total_payload64);
    header.layouts_offset_bytes = layouts_offset;
    header.data_offset_bytes = data_offset_bytes;

    const oxygen::data::pak::SubresourceLayout layout {
      .offset_bytes = 0,
      .row_pitch_bytes = aligned_pitch,
      .size_bytes = aligned_pitch,
    };

    std::vector<std::byte> payload(header.total_payload_size, std::byte { 0 });
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + layouts_offset, &layout, sizeof(layout));

    std::copy(pixel_rgba8.begin(), pixel_rgba8.end(),
      payload.begin() + static_cast<std::ptrdiff_t>(data_offset_bytes));

    header.content_hash
      = detail::ComputeContentHash(std::span<const std::byte>(payload));
    std::memcpy(payload.data(), &header, sizeof(header));

    return payload;
  }

  [[nodiscard]] auto CreatePlaceholderTextureWithPixel(std::string_view id,
    const CookerConfig& config, const std::array<std::byte, 4> pixel_rgba8)
    -> CookedEmissionResult
  {
    const auto& policy = GetPackingPolicy(config.packing_policy_id);
    const auto policy_id_opt = ToPackingPolicyId(policy.Id());
    if (!policy_id_opt.has_value()) {
      LOG_F(ERROR,
        "CreatePlaceholderTextureWithPixel: unknown packing policy id '{}'; "
        "falling back to d3d12",
        std::string(policy.Id()).c_str());
    }
    const auto policy_id = policy_id_opt.value_or(
      oxygen::data::pak::TexturePackingPolicyId::kD3D12);

    auto payload = BuildPlaceholderPayloadV4(policy, policy_id, pixel_rgba8);
    if (payload.empty()) {
      LOG_F(ERROR,
        "CreatePlaceholderTextureWithPixel: failed to build v4 payload for "
        "'{}'",
        std::string(id).c_str());
    }

    oxygen::data::pak::TexturePayloadHeader header {};
    if (payload.size() >= sizeof(header)) {
      std::memcpy(&header, payload.data(), sizeof(header));
    }

    CookedEmissionResult result {};
    result.payload = std::move(payload);
    result.is_placeholder = true;

    result.desc.data_offset = 0; // Will be set when appended
    result.desc.size_bytes = static_cast<uint32_t>(result.payload.size());
    result.desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
    result.desc.compression_type = 0;
    result.desc.width = 1;
    result.desc.height = 1;
    result.desc.depth = 1;
    result.desc.array_layers = 1;
    result.desc.mip_levels = 1;
    result.desc.format = static_cast<uint8_t>(Format::kRGBA8UNorm);
    result.desc.alignment = static_cast<uint16_t>(policy.AlignRowPitchBytes(1));
    result.desc.content_hash = header.content_hash;

    return result;
  }

  //! D3D12 packing policy (256-byte row pitch, 512-byte subresource alignment).
  class D3D12PackingPolicy final : public ITexturePackingPolicy {
  public:
    [[nodiscard]] auto Id() const noexcept -> std::string_view override
    {
      return "d3d12";
    }

    [[nodiscard]] auto AlignRowPitchBytes(
      const uint32_t unaligned_pitch) const noexcept -> uint32_t override
    {
      return (unaligned_pitch + kD3D12RowPitchAlignment - 1)
        & ~(kD3D12RowPitchAlignment - 1);
    }

    [[nodiscard]] auto AlignSubresourceOffset(
      const uint64_t offset) const noexcept -> uint64_t override
    {
      return (offset + kD3D12SubresourcePlacementAlignment - 1)
        & ~(kD3D12SubresourcePlacementAlignment - 1);
    }
  };

  //! Tight packing policy (minimal alignment, 4-byte subresource).
  class TightPackingPolicy final : public ITexturePackingPolicy {
  public:
    [[nodiscard]] auto Id() const noexcept -> std::string_view override
    {
      return "tight";
    }

    [[nodiscard]] auto AlignRowPitchBytes(
      const uint32_t unaligned_pitch) const noexcept -> uint32_t override
    {
      return unaligned_pitch; // No padding
    }

    [[nodiscard]] auto AlignSubresourceOffset(
      const uint64_t offset) const noexcept -> uint64_t override
    {
      constexpr uint64_t kMinAlignment = 4;
      return (offset + kMinAlignment - 1) & ~(kMinAlignment - 1);
    }
  };

  // Static policy instances
  const D3D12PackingPolicy kD3D12Policy;
  const TightPackingPolicy kTightPolicy;

  //! Generate a deterministic placeholder color from texture ID.
  [[nodiscard]] auto MakeDeterministicPixelRGBA8(std::string_view id)
    -> std::array<std::byte, 4>
  {
    if (id.empty()) {
      return { std::byte { 0x7F }, std::byte { 0x7F }, std::byte { 0x7F },
        std::byte { 0xFF } };
    }

    const auto bytes
      = std::as_bytes(std::span(id.data(), static_cast<size_t>(id.size())));
    const auto digest = oxygen::base::ComputeSha256(bytes);
    return { std::byte { digest[0] }, std::byte { digest[1] },
      std::byte { digest[2] }, std::byte { 0xFF } };
  }

} // namespace

auto GetPackingPolicy(const std::string& policy_id)
  -> const ITexturePackingPolicy&
{
  if (policy_id == "tight") {
    return kTightPolicy;
  }
  // Default to D3D12
  return kD3D12Policy;
}

auto GetDefaultPackingPolicy() -> const ITexturePackingPolicy&
{
#if defined(_WIN32)
  return kD3D12Policy;
#else
  return kTightPolicy;
#endif
}

auto MakeImportDescFromConfig(
  const CookerConfig& config, std::string_view texture_id) -> TextureImportDesc
{
  TextureImportDesc desc {};

  // Basic settings
  desc.texture_type = TextureType::kTexture2D;
  desc.array_layers = 1;

  // Mip generation
  if (config.generate_mips) {
    desc.mip_policy = MipPolicy::kFullChain;
    desc.mip_filter = MipFilter::kKaiser;
  } else {
    desc.mip_policy = MipPolicy::kNone;
  }

  // BC7 compression
  if (config.use_bc7_compression) {
    desc.bc7_quality = config.bc7_quality;
  } else {
    desc.bc7_quality = Bc7Quality::kNone;
  }

  // Set identifier for diagnostics
  if (!texture_id.empty()) {
    desc.source_id = std::string(texture_id);
  }

  return desc;
}

auto CookTextureForEmission(std::span<const std::byte> source_bytes,
  const CookerConfig& config, std::string_view texture_id)
  -> oxygen::Result<CookedEmissionResult, TextureImportError>
{
  if (source_bytes.empty()) {
    return ::oxygen::Err(TextureImportError::kFileNotFound);
  }

  // Decode image first to get dimensions
  DecodeOptions decode_options {};
  if (auto dot_pos = std::string_view(texture_id).rfind('.');
    dot_pos != std::string_view::npos) {
    decode_options.extension_hint = texture_id.substr(dot_pos);
  }
  auto decoded = DecodeToScratchImage(source_bytes, decode_options);
  if (!decoded.has_value()) {
    return ::oxygen::Err(decoded.error());
  }

  const auto& meta = decoded->Meta();

  const auto& policy = GetPackingPolicy(config.packing_policy_id);
  auto desc = MakeImportDescFromConfig(config, texture_id);

  // Set dimensions from decoded image
  desc.width = meta.width;
  desc.height = meta.height;
  desc.output_format = meta.format;

  auto result = CookTexture(source_bytes, desc, policy);
  if (!result.has_value()) {
    return ::oxygen::Err(result.error());
  }

  CookedEmissionResult emission_result {};
  // IMPORTANT: Create descriptor BEFORE moving the payload
  emission_result.desc = ToPakDescriptor(result.value(), 0);
  emission_result.payload = std::move(result.value().payload);
  emission_result.is_placeholder = false;

  return ::oxygen::Ok(emission_result);
}

auto CookTextureWithFallback(std::span<const std::byte> source_bytes,
  const CookerConfig& config, std::string_view texture_id)
  -> CookedEmissionResult
{
  auto result = CookTextureForEmission(source_bytes, config, texture_id);
  if (result.has_value()) {
    return std::move(result.value());
  }

  LOG_F(WARNING, "Failed to cook texture '{}': error {}; using placeholder",
    std::string(texture_id).c_str(), static_cast<int>(result.error()));

  return CreatePlaceholderForMissingTexture(texture_id, config);
}

auto CreatePlaceholderForMissingTexture(std::string_view texture_id,
  const CookerConfig& config) -> CookedEmissionResult
{
  return CreatePlaceholderTextureWithPixel(
    texture_id, config, MakeDeterministicPixelRGBA8(texture_id));
}

auto CreateFallbackTexture(const CookerConfig& config) -> CookedEmissionResult
{
  const std::array<std::byte, 4> white_pixel { std::byte { 0xFF },
    std::byte { 0xFF }, std::byte { 0xFF }, std::byte { 0xFF } };
  return CreatePlaceholderTextureWithPixel(
    "_fallback_white_", config, white_pixel);
}

auto ToPakDescriptor(const CookedTexturePayload& payload, uint64_t data_offset)
  -> oxygen::data::pak::TextureResourceDesc
{
  oxygen::data::pak::TextureResourceDesc desc {};

  desc.data_offset = data_offset;
  desc.size_bytes = static_cast<uint32_t>(payload.payload.size());
  desc.texture_type = static_cast<uint8_t>(payload.desc.texture_type);
  desc.compression_type = 0; // Set based on format below
  desc.width = payload.desc.width;
  desc.height = payload.desc.height;
  desc.depth = payload.desc.depth;
  desc.array_layers = payload.desc.array_layers;
  desc.mip_levels = payload.desc.mip_levels;
  desc.format = static_cast<uint8_t>(payload.desc.format);
  desc.content_hash = payload.desc.content_hash;

  // Determine alignment from packing policy
  const auto& policy = GetPackingPolicy(payload.desc.packing_policy_id);
  desc.alignment = static_cast<uint16_t>(policy.AlignRowPitchBytes(1));

  // Set compression type based on format
  switch (payload.desc.format) {
  case Format::kBC7UNorm:
  case Format::kBC7UNormSRGB:
    desc.compression_type = 7; // BC7
    break;
  default:
    desc.compression_type = 0; // Uncompressed
    break;
  }

  return desc;
}

} // namespace oxygen::content::import::emit
