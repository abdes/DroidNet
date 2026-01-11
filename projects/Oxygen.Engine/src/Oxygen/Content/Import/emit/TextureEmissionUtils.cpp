//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/emit/TextureEmissionUtils.h>

#include <array>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/ImageDecode.h>
#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import::emit {

namespace {

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

  return CreatePlaceholderTexture(texture_id, config);
}

auto CreatePlaceholderTexture(std::string_view texture_id,
  const CookerConfig& config) -> CookedEmissionResult
{
  const auto& policy = GetPackingPolicy(config.packing_policy_id);

  // Create 1x1 RGBA8 placeholder
  const auto pixel = MakeDeterministicPixelRGBA8(texture_id);

  // Compute aligned row pitch (for 1 pixel of RGBA8 = 4 bytes)
  const uint32_t unaligned_pitch = 4;
  const uint32_t aligned_pitch = policy.AlignRowPitchBytes(unaligned_pitch);

  // Create payload with aligned pitch
  std::vector<std::byte> payload(aligned_pitch, std::byte { 0 });
  std::copy(pixel.begin(), pixel.end(), payload.begin());

  // Compute content hash
  const uint64_t content_hash = detail::ComputeContentHash(payload);

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
  result.desc.alignment = static_cast<uint16_t>(aligned_pitch);
  result.desc.content_hash = content_hash;

  return result;
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
