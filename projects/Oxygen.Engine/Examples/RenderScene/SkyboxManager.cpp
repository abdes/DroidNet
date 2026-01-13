//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SkyboxManager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/TextureImporter.h>
#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>

namespace oxygen::examples::render_scene {

SkyboxManager::SkyboxManager(
  oxygen::observer_ptr<oxygen::content::AssetLoader> asset_loader,
  std::shared_ptr<scene::Scene> scene)
  : asset_loader_(asset_loader)
  , scene_(std::move(scene))
{
}

auto SkyboxManager::LoadSkyboxAsync(const std::string& file_path,
  const LoadOptions& options) -> co::Co<LoadResult>
{
  LoadResult result;

  const std::filesystem::path img_path { file_path };
  if (img_path.empty()) {
    result.status_message = "No skybox path provided";
    co_return result;
  }

  if (!asset_loader_) {
    result.status_message = "AssetLoader unavailable";
    co_return result;
  }

  using namespace oxygen::content::import;

  oxygen::Format output_fmt = oxygen::Format::kRGBA8UNormSRGB;
  bool use_bc7 = false;
  const char* format_name = "RGBA8";

  switch (options.output_format) {
  case OutputFormat::kRGBA8:
    output_fmt = oxygen::Format::kRGBA8UNormSRGB;
    format_name = "RGBA8";
    break;
  case OutputFormat::kRGBA16Float:
    output_fmt = oxygen::Format::kRGBA16Float;
    format_name = "RGBA16F";
    break;
  case OutputFormat::kRGBA32Float:
    output_fmt = oxygen::Format::kRGBA32Float;
    format_name = "RGBA32F";
    break;
  case OutputFormat::kBC7:
    output_fmt = oxygen::Format::kBC7UNormSRGB;
    use_bc7 = true;
    format_name = "BC7";
    break;
  }

  std::string ext = img_path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const bool is_hdr_source = (ext == ".hdr") || (ext == ".exr");

  const bool is_ldr_output = (output_fmt == oxygen::Format::kRGBA8UNormSRGB)
    || (output_fmt == oxygen::Format::kBC7UNormSRGB);
  const bool should_tonemap_hdr_to_ldr = is_hdr_source && is_ldr_output;
  const bool tonemap_forced
    = should_tonemap_hdr_to_ldr && !options.tonemap_hdr_to_ldr;

  std::optional<TextureImportResult> cooked_result;

  auto BuildDesc = [&](TextureImportDesc& desc) {
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

    // IBL specular relies on sampling across the mip chain for
    // roughness-based filtering.
    desc.mip_policy = MipPolicy::kFullChain;
    desc.mip_filter = MipFilter::kKaiser;
    desc.mip_filter_space = ColorSpace::kLinear;
  };

  if (options.layout == Layout::kEquirectangular) {
    TextureImportDesc desc;
    BuildDesc(desc);

    auto equirect_result = ImportCubeMapFromEquirect(img_path,
      static_cast<uint32_t>(options.cube_face_size), desc,
      D3D12PackingPolicy::Instance());

    if (!equirect_result.has_value()) {
      result.status_message = to_string(equirect_result.error());
      co_return result;
    }
    cooked_result = std::move(*equirect_result);
  } else {
    TextureImportDesc desc;
    BuildDesc(desc);

    auto layout_result = ImportCubeMapFromLayoutImage(
      img_path, desc, D3D12PackingPolicy::Instance());

    if (!layout_result.has_value()) {
      result.status_message = to_string(layout_result.error());
      co_return result;
    }
    cooked_result = std::move(*layout_result);
  }

  if (!cooked_result.has_value()) {
    result.status_message = "Failed to cook cubemap";
    co_return result;
  }

  const auto& payload = cooked_result->payload;

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

  current_resource_key_ = asset_loader_->MintSyntheticTextureKey();
  result.resource_key = current_resource_key_;

  std::vector<std::uint8_t> packed;
  packed.resize(sizeof(TextureResourceDesc) + payload.payload.size());
  std::memcpy(packed.data(), &pak_desc, sizeof(TextureResourceDesc));
  std::memcpy(packed.data() + sizeof(TextureResourceDesc),
    payload.payload.data(), payload.payload.size());

  auto tex
    = co_await asset_loader_->LoadResourceAsync<oxygen::data::TextureResource>(
      oxygen::content::CookedResourceData<oxygen::data::TextureResource> {
        .key = current_resource_key_,
        .bytes = std::span<const std::uint8_t>(packed.data(), packed.size()),
      });

  if (!tex) {
    result.status_message = "Skybox texture decode failed";
    co_return result;
  }

  result.success = true;
  result.face_size = static_cast<int>(payload.desc.width);
  result.status_message = std::string("Loaded (") + format_name
    + (should_tonemap_hdr_to_ldr ? ", HDR->LDR" : "")
    + (tonemap_forced ? " [auto]" : "")
    + ", mips=" + std::to_string(payload.desc.mip_levels) + ")";

  co_return result;
}

auto SkyboxManager::ApplyToScene(const SkyLightParams& params) -> void
{
  if (!scene_ || current_resource_key_ == oxygen::content::ResourceKey { 0U }) {
    return;
  }

  auto env = scene_->GetEnvironment();
  if (!env) {
    auto new_env = std::make_unique<scene::SceneEnvironment>();

    auto& sky = new_env->AddSystem<scene::environment::SkySphere>();
    sky.SetEnabled(true);
    sky.SetSource(scene::environment::SkySphereSource::kCubemap);
    sky.SetCubemapResource(current_resource_key_);

    auto& sky_light = new_env->AddSystem<scene::environment::SkyLight>();
    sky_light.SetEnabled(true);
    sky_light.SetSource(scene::environment::SkyLightSource::kSpecifiedCubemap);
    sky_light.SetCubemapResource(current_resource_key_);
    sky_light.SetIntensity(params.intensity);
    sky_light.SetDiffuseIntensity(params.diffuse_intensity);
    sky_light.SetSpecularIntensity(params.specular_intensity);
    sky_light.SetTintRgb(params.tint_rgb);

    scene_->SetEnvironment(std::move(new_env));
    return;
  }

  auto sky = env->TryGetSystem<scene::environment::SkySphere>();
  if (!sky) {
    auto& sky_ref = env->AddSystem<scene::environment::SkySphere>();
    sky = observer_ptr<scene::environment::SkySphere>(&sky_ref);
  }
  sky->SetEnabled(true);
  sky->SetSource(scene::environment::SkySphereSource::kCubemap);
  sky->SetCubemapResource(current_resource_key_);

  auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
  if (!sky_light) {
    auto& sky_light_ref = env->AddSystem<scene::environment::SkyLight>();
    sky_light = observer_ptr<scene::environment::SkyLight>(&sky_light_ref);
  }
  sky_light->SetEnabled(true);
  sky_light->SetSource(scene::environment::SkyLightSource::kSpecifiedCubemap);
  sky_light->SetCubemapResource(current_resource_key_);
  sky_light->SetIntensity(params.intensity);
  sky_light->SetDiffuseIntensity(params.diffuse_intensity);
  sky_light->SetSpecularIntensity(params.specular_intensity);
  sky_light->SetTintRgb(params.tint_rgb);
}

auto SkyboxManager::UpdateSkyLightParams(const SkyLightParams& params) -> void
{
  if (!scene_) {
    return;
  }

  auto env = scene_->GetEnvironment();
  if (!env) {
    return;
  }

  auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
  if (!sky_light) {
    return;
  }

  sky_light->SetIntensity(params.intensity);
  sky_light->SetDiffuseIntensity(params.diffuse_intensity);
  sky_light->SetSpecularIntensity(params.specular_intensity);
  sky_light->SetTintRgb(params.tint_rgb);
}

} // namespace oxygen::examples::render_scene
