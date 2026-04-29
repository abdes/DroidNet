//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Cooker/Import/TextureImporter.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>

#include "DemoShell/Services/SkyboxService.h"

namespace oxygen::examples {
namespace {

auto CubeLayoutFromSkyboxLayout(const SkyboxService::Layout layout)
  -> content::import::CubeMapImageLayout
{
  using content::import::CubeMapImageLayout;

  switch (layout) {
  case SkyboxService::Layout::kHorizontalCross:
    return CubeMapImageLayout::kHorizontalCross;
  case SkyboxService::Layout::kVerticalCross:
    return CubeMapImageLayout::kVerticalCross;
  case SkyboxService::Layout::kHorizontalStrip:
    return CubeMapImageLayout::kHorizontalStrip;
  case SkyboxService::Layout::kVerticalStrip:
    return CubeMapImageLayout::kVerticalStrip;
  case SkyboxService::Layout::kEquirectangular:
    return CubeMapImageLayout::kUnknown;
  }
  return CubeMapImageLayout::kUnknown;
}

} // namespace

SkyboxService::SkyboxService(observer_ptr<content::IAssetLoader> asset_loader,
  observer_ptr<scene::Scene> scene)
  : asset_loader_(asset_loader)
  , scene_(scene)
{
}

SkyboxService::~SkyboxService() { ReleasePinnedResource(); }

auto SkyboxService::StartLoadSkybox(const std::string& file_path,
  const LoadOptions& options, LoadCallback on_complete) -> void
{
  LoadResult result;

  const std::filesystem::path img_path { file_path };
  if (img_path.empty()) {
    result.status_message = "No skybox path provided";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  if (!asset_loader_) {
    result.status_message = "AssetLoader unavailable";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  using namespace oxygen::content::import;

  // Determine output format
  auto output_fmt = Format::kRGBA8UNormSRGB;
  bool use_bc7 = false;
  auto format_name = "RGBA8";

  switch (options.output_format) {
  case OutputFormat::kRGBA8:
    output_fmt = Format::kRGBA8UNormSRGB;
    format_name = "RGBA8";
    break;
  case OutputFormat::kRGBA16Float:
    output_fmt = Format::kRGBA16Float;
    format_name = "RGBA16F";
    break;
  case OutputFormat::kRGBA32Float:
    output_fmt = Format::kRGBA32Float;
    format_name = "RGBA32F";
    break;
  case OutputFormat::kBC7:
    output_fmt = Format::kBC7UNormSRGB;
    use_bc7 = true;
    format_name = "BC7";
    break;
  }

  std::string ext = img_path.extension().string();
  std::ranges::transform(ext, ext.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const bool is_hdr_source = (ext == ".hdr") || (ext == ".exr");

  const bool is_ldr_output = (output_fmt == Format::kRGBA8UNormSRGB)
    || (output_fmt == Format::kBC7UNormSRGB);
  const bool should_tonemap_hdr_to_ldr = is_hdr_source && is_ldr_output;
  const bool tonemap_forced
    = should_tonemap_hdr_to_ldr && !options.tonemap_hdr_to_ldr;

  std::optional<TextureImportResult> cooked_result;

  if (options.layout == Layout::kEquirectangular) {
    TextureImportDesc desc;
    desc.texture_type = TextureType::kTextureCube;
    desc.intent
      = is_hdr_source ? TextureIntent::kHdrEnvironment : TextureIntent::kData;
    desc.source_color_space
      = is_hdr_source ? ColorSpace::kLinear : ColorSpace::kSRGB;
    desc.output_format = output_fmt;
    desc.bc7_quality = use_bc7 ? Bc7Quality::kDefault : Bc7Quality::kNone;
    desc.source_id = img_path.string();
    desc.flip_y_on_decode = options.flip_y;

    desc.hdr_handling = should_tonemap_hdr_to_ldr ? HdrHandling::kTonemapAuto
                                                  : HdrHandling::kError;
    desc.bake_hdr_to_ldr = should_tonemap_hdr_to_ldr;
    desc.exposure_ev = options.hdr_exposure_ev;

    // Be explicit about mip generation: IBL specular relies on sampling across
    // the mip chain for roughness-based filtering.
    desc.mip_policy = MipPolicy::kFullChain;
    desc.mip_filter = MipFilter::kKaiser;
    desc.mip_filter_space = ColorSpace::kLinear;

    auto equirect_result = ImportCubeMapFromEquirect(img_path,
      static_cast<uint32_t>(options.cube_face_size), desc,
      D3D12PackingPolicy::Instance());

    if (!equirect_result.has_value()) {
      result.status_message = to_string(equirect_result.error());
      if (on_complete) {
        on_complete(std::move(result));
      }
      return;
    }
    cooked_result = std::move(*equirect_result);
  } else {
    // Cross/strip layout - use ImportCubeMapFromLayoutImage which handles
    // layout detection, face extraction, and cooking automatically
    TextureImportDesc desc;
    desc.texture_type = TextureType::kTextureCube;
    desc.intent
      = is_hdr_source ? TextureIntent::kHdrEnvironment : TextureIntent::kData;
    desc.source_color_space
      = is_hdr_source ? ColorSpace::kLinear : ColorSpace::kSRGB;
    desc.output_format = output_fmt;
    desc.bc7_quality = use_bc7 ? Bc7Quality::kDefault : Bc7Quality::kNone;
    desc.source_id = img_path.string();
    desc.flip_y_on_decode = options.flip_y;

    desc.hdr_handling = should_tonemap_hdr_to_ldr ? HdrHandling::kTonemapAuto
                                                  : HdrHandling::kError;
    desc.bake_hdr_to_ldr = should_tonemap_hdr_to_ldr;
    desc.exposure_ev = options.hdr_exposure_ev;

    // Be explicit about mip generation: IBL specular relies on sampling across
    // the mip chain for roughness-based filtering.
    desc.mip_policy = MipPolicy::kFullChain;
    desc.mip_filter = MipFilter::kKaiser;
    desc.mip_filter_space = ColorSpace::kLinear;

    const auto layout = CubeLayoutFromSkyboxLayout(options.layout);
    auto layout_result = ImportCubeMapFromLayoutImage(
      img_path, layout, desc, D3D12PackingPolicy::Instance());

    if (!layout_result.has_value()) {
      result.status_message = to_string(layout_result.error());
      if (on_complete) {
        on_complete(std::move(result));
      }
      return;
    }
    cooked_result = std::move(*layout_result);
  }

  if (!cooked_result.has_value()) {
    result.status_message = "Failed to cook cubemap";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  // Access the cooked payload
  const auto& payload = cooked_result->payload;

  // Build PAK descriptor
  using PakTextureResourceDesc = data::pak::core::TextureResourceDesc;
  PakTextureResourceDesc pak_desc {};
  pak_desc.data_offset = sizeof(PakTextureResourceDesc);
  pak_desc.size_bytes
    = static_cast<data::pak::core::DataBlobSizeT>(payload.payload.size());
  pak_desc.texture_type = static_cast<std::uint8_t>(payload.desc.texture_type);
  pak_desc.compression_type = 0;
  pak_desc.width = payload.desc.width;
  pak_desc.height = payload.desc.height;
  pak_desc.depth = payload.desc.depth;
  pak_desc.array_layers = payload.desc.array_layers;
  pak_desc.mip_levels = payload.desc.mip_levels;
  pak_desc.format = static_cast<std::uint8_t>(payload.desc.format);
  pak_desc.alignment = 256U;

  // Mint a fresh key
  const auto resource_key = asset_loader_->MintSyntheticTextureKey();

  auto packed = std::make_shared<std::vector<std::uint8_t>>();
  packed->resize(sizeof(PakTextureResourceDesc) + payload.payload.size());
  std::memcpy(packed->data(), &pak_desc, sizeof(PakTextureResourceDesc));
  std::memcpy(packed->data() + sizeof(PakTextureResourceDesc),
    payload.payload.data(), payload.payload.size());

  asset_loader_->StartLoadTexture(
    oxygen::content::CookedResourceData<data::TextureResource> {
      .key = resource_key,
      .bytes = std::span<const std::uint8_t>(packed->data(), packed->size()),
    },
    [this, on_complete = std::move(on_complete), format_name,
      should_tonemap_hdr_to_ldr, tonemap_forced,
      face_size = static_cast<int>(payload.desc.width),
      mip_levels = payload.desc.mip_levels,
      resource_key](std::shared_ptr<data::TextureResource> tex) mutable {
      LoadResult callback_result;
      callback_result.resource_key = resource_key;
      callback_result.face_size = face_size;

      if (!tex) {
        callback_result.status_message = "Skybox texture upload failed";
      } else if (!PinCurrentResource(resource_key)) {
        callback_result.status_message
          = "Skybox texture loaded but could not be pinned";
      } else {
        current_resource_key_ = resource_key;
        callback_result.success = true;
        callback_result.status_message = std::string("Loaded (") + format_name
          + (should_tonemap_hdr_to_ldr ? ", HDR->LDR" : "")
          + (tonemap_forced ? " [auto]" : "")
          + ", mips=" + std::to_string(mip_levels) + ")";
      }

      if (on_complete) {
        on_complete(std::move(callback_result));
      }
    });
}

auto SkyboxService::LoadAndEquip(const std::string& file_path,
  const LoadOptions& options, const SkyLightParams& params,
  LoadCallback on_complete) -> void
{
  StartLoadSkybox(file_path, options,
    [this, params, on_complete = std::move(on_complete)](LoadResult result) {
      if (result.success) {
        ApplyToScene(params);
      }
      if (on_complete) {
        on_complete(std::move(result));
      }
    });
}

auto SkyboxService::SetSkyboxResourceKey(content::ResourceKey key) -> void
{
  if (asset_loader_ && key != content::ResourceKey { 0U }
    && asset_loader_->HasTexture(key)) {
    static_cast<void>(PinCurrentResource(key));
  } else if (key != pinned_resource_key_) {
    ReleasePinnedResource();
  }
  current_resource_key_ = key;
}

auto SkyboxService::ApplyToScene(const SkyLightParams& params) -> void
{
  if (!scene_ || current_resource_key_ == content::ResourceKey { 0U }) {
    return;
  }

  auto env = scene_->GetEnvironment();
  if (!env) {
    auto new_env = std::make_unique<scene::SceneEnvironment>();

    auto& sky = new_env->AddSystem<scene::environment::SkySphere>();
    sky.SetEnabled(true);
    sky.SetSource(scene::environment::SkySphereSource::kCubemap);
    sky.SetCubemapResource(current_resource_key_);
    sky.SetIntensity(params.sky_sphere_intensity);

    auto& sky_light = new_env->AddSystem<scene::environment::SkyLight>();
    sky_light.SetEnabled(true);
    sky_light.SetSource(scene::environment::SkyLightSource::kSpecifiedCubemap);
    sky_light.SetCubemapResource(current_resource_key_);
    sky_light.SetIntensityMul(params.intensity_mul);
    sky_light.SetDiffuseIntensity(params.diffuse_intensity);
    sky_light.SetSpecularIntensity(params.specular_intensity);
    sky_light.SetTintRgb(params.tint_rgb);

    scene_->SetEnvironment(std::move(new_env));
  } else {
    auto sky = env->TryGetSystem<scene::environment::SkySphere>();
    if (!sky) {
      auto& sky_ref = env->AddSystem<scene::environment::SkySphere>();
      sky = observer_ptr(&sky_ref);
    }
    sky->SetEnabled(true);
    sky->SetSource(scene::environment::SkySphereSource::kCubemap);
    sky->SetCubemapResource(current_resource_key_);
    sky->SetIntensity(params.sky_sphere_intensity);

    auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
    if (!sky_light) {
      auto& sky_light_ref = env->AddSystem<scene::environment::SkyLight>();
      sky_light = observer_ptr(&sky_light_ref);
    }
    sky_light->SetEnabled(true);
    sky_light->SetSource(scene::environment::SkyLightSource::kSpecifiedCubemap);
    sky_light->SetCubemapResource(current_resource_key_);
    sky_light->SetIntensityMul(params.intensity_mul);
    sky_light->SetDiffuseIntensity(params.diffuse_intensity);
    sky_light->SetSpecularIntensity(params.specular_intensity);
    sky_light->SetTintRgb(params.tint_rgb);
  }
}

auto SkyboxService::PinCurrentResource(content::ResourceKey key) -> bool
{
  if (!asset_loader_ || key == content::ResourceKey { 0U }) {
    return false;
  }
  if (key == pinned_resource_key_) {
    return true;
  }
  if (!asset_loader_->PinResource(key)) {
    return false;
  }
  ReleasePinnedResource();
  pinned_resource_key_ = key;
  return true;
}

auto SkyboxService::ReleasePinnedResource() noexcept -> void
{
  if (!asset_loader_ || pinned_resource_key_ == content::ResourceKey { 0U }) {
    return;
  }
  static_cast<void>(asset_loader_->UnpinResource(pinned_resource_key_));
  pinned_resource_key_ = content::ResourceKey { 0U };
}

auto SkyboxService::UpdateSkyLightParams(const SkyLightParams& params) -> void
{
  if (!scene_) {
    return;
  }

  auto env = scene_->GetEnvironment();
  if (!env) {
    return;
  }

  auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
  if (sky_light) {
    sky_light->SetIntensityMul(params.intensity_mul);
    sky_light->SetDiffuseIntensity(params.diffuse_intensity);
    sky_light->SetSpecularIntensity(params.specular_intensity);
    sky_light->SetTintRgb(params.tint_rgb);
  }
}

} // namespace oxygen::examples
