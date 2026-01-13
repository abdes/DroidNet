//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "TextureLoadingService.h"

#include <cstring>
#include <filesystem>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Content/Import/TextureImporter.h>
#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::examples::textured_cube {

TextureLoadingService::TextureLoadingService(
  oxygen::observer_ptr<oxygen::content::AssetLoader> asset_loader)
  : asset_loader_(asset_loader)
{
}

auto TextureLoadingService::LoadTextureAsync(const std::string& file_path,
  const LoadOptions& options) -> co::Co<LoadResult>
{
  LoadResult result;

  const std::filesystem::path img_path { file_path };
  if (img_path.empty()) {
    result.status_message = "No image path provided";
    co_return result;
  }

  if (!asset_loader_) {
    result.status_message = "AssetLoader unavailable";
    co_return result;
  }

  using namespace oxygen::content::import;

  // Start with kAlbedo preset (sRGB, Kaiser mips, sensible defaults)
  auto desc = MakeDescFromPreset(TexturePreset::kAlbedo);
  desc.source_id = img_path.string();

  // Override output format based on user selection
  const char* format_name = "RGBA8";
  switch (options.output_format_idx) {
  case 0: // RGBA8 (uncompressed for fast iteration)
    desc.output_format = oxygen::Format::kRGBA8UNormSRGB;
    desc.bc7_quality = Bc7Quality::kNone;
    format_name = "RGBA8";
    break;
  case 1: // BC7 (production quality)
    desc.output_format = oxygen::Format::kBC7UNormSRGB;
    desc.bc7_quality = Bc7Quality::kDefault;
    format_name = "BC7";
    break;
  case 2: // RGBA16F (HDR)
    desc.output_format = oxygen::Format::kRGBA16Float;
    desc.bc7_quality = Bc7Quality::kNone;
    format_name = "RGBA16F";
    break;
  case 3: // RGBA32F (full HDR precision)
    desc.output_format = oxygen::Format::kRGBA32Float;
    desc.bc7_quality = Bc7Quality::kNone;
    format_name = "RGBA32F";
    break;
  default:
    break;
  }

  // Override mip policy based on user selection
  desc.mip_policy
    = options.generate_mips ? MipPolicy::kFullChain : MipPolicy::kNone;

  // Configure HDR handling
  desc.hdr_handling = options.tonemap_hdr_to_ldr ? HdrHandling::kTonemapAuto
                                                 : HdrHandling::kError;
  desc.bake_hdr_to_ldr = options.tonemap_hdr_to_ldr;
  desc.exposure_ev = options.hdr_exposure_ev;

  auto cooked = ImportTexture(img_path, desc, D3D12PackingPolicy::Instance());

  if (!cooked.has_value()) {
    if (cooked.error() == TextureImportError::kHdrRequiresFloatFormat
      && !options.tonemap_hdr_to_ldr) {
      result.status_message
        = "HDR image requires 'Tonemap HDR to LDR' enabled for RGBA8 output";
    } else {
      result.status_message = to_string(cooked.error());
    }
    co_return result;
  }

  // Mint a fresh key for this texture
  result.resource_key = asset_loader_->MintSyntheticTextureKey();

  // Access the cooked payload from the import result
  const auto& payload = cooked->payload;

  // Build the packed buffer with descriptor + payload
  using oxygen::data::pak::TextureResourceDesc;
  TextureResourceDesc pak_desc {};
  pak_desc.data_offset
    = static_cast<oxygen::data::pak::OffsetT>(sizeof(TextureResourceDesc));
  pak_desc.size_bytes
    = static_cast<oxygen::data::pak::DataBlobSizeT>(payload.payload.size());
  pak_desc.texture_type = static_cast<std::uint8_t>(payload.desc.texture_type);
  pak_desc.compression_type = 0;
  pak_desc.width = payload.desc.width;
  pak_desc.height = payload.desc.height;
  pak_desc.depth = payload.desc.depth;
  pak_desc.array_layers = payload.desc.array_layers;
  pak_desc.mip_levels = payload.desc.mip_levels;
  pak_desc.format = static_cast<std::uint8_t>(payload.desc.format);
  pak_desc.alignment = 256U;

  std::vector<std::uint8_t> packed;
  packed.resize(sizeof(TextureResourceDesc) + payload.payload.size());
  std::memcpy(packed.data(), &pak_desc, sizeof(TextureResourceDesc));
  std::memcpy(packed.data() + sizeof(TextureResourceDesc),
    payload.payload.data(), payload.payload.size());

  auto tex
    = co_await asset_loader_->LoadResourceAsync<oxygen::data::TextureResource>(
      oxygen::content::CookedResourceData<oxygen::data::TextureResource> {
        .key = result.resource_key,
        .bytes = std::span<const std::uint8_t>(packed.data(), packed.size()),
      });

  if (!tex) {
    result.status_message = "Upload failed";
    co_return result;
  }

  result.success = true;
  result.width = static_cast<int>(tex->GetWidth());
  result.height = static_cast<int>(tex->GetHeight());

  // Build informative status message
  std::string status = "Loaded";
  // Check if HDR processing was applied
  const bool is_hdr_output = (desc.output_format == oxygen::Format::kRGBA16Float
    || desc.output_format == oxygen::Format::kRGBA32Float);
  if (options.tonemap_hdr_to_ldr) {
    status += " (HDR→LDR)";
  } else if (is_hdr_output) {
    status += " (HDR)";
  }
  status += " [";
  status += format_name;
  status += "]";
  result.status_message = status;

  co_return result;
}

auto TextureLoadingService::MintTextureKey() const
  -> oxygen::content::ResourceKey
{
  if (!asset_loader_) {
    return oxygen::content::ResourceKey { 0U };
  }
  return asset_loader_->MintSyntheticTextureKey();
}

} // namespace oxygen::examples::textured_cube
