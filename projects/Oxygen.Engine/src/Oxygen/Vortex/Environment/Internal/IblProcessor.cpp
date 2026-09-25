//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cmath>
#include <tuple>
#include <utility>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/CapturedSkySource.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>

namespace oxygen::vortex::environment::internal {

namespace {
  auto MakePublishedState(const IblGpuProducts& product,
    const StaticSkyLightProductKey& key, const bool enabled)
    -> EnvironmentProbeState
  {
    auto result = EnvironmentProbeState {};
    result.probes = { .environment_map_srv = product.processed_srv,
      .diffuse_sh_srv = product.diffuse_sh_srv,
      .prefiltered_map_srv = product.specular_srv,
      .probe_revision = product.revision,
      .product_metadata_srv = product.metadata_srv };
    result.static_sky_light = { .key = key,
      .processed_cubemap_srv = product.processed_srv,
      .diffuse_irradiance_sh_srv = product.diffuse_sh_srv,
      .prefiltered_cubemap_srv = product.specular_srv,
      .processed_cubemap_max_mip = product.maximum_mip,
      .prefiltered_cubemap_max_mip = product.maximum_mip,
      .product_revision = product.revision,
      .status = enabled ? StaticSkyLightProductStatus::kValidCurrentKey
                        : StaticSkyLightProductStatus::kDisabled };
    result.valid = true;
    result.flags = kEnvironmentProbeStateFlagResourcesValid;
    return result;
  }
} // namespace

struct IblProcessor::Cache {
  std::unique_ptr<IblGpuProcessor> processor;
  std::unique_ptr<IblBrdfResources> brdf;
  std::unique_ptr<CapturedSkySource> captured;
  std::shared_ptr<const IblGpuProducts> published;
  StaticSkyLightProductKey key;
  std::uint32_t revision {};
};

IblProcessor::IblProcessor(Renderer& renderer)
  : renderer_(renderer)
  , cache_(std::make_unique<Cache>())
{
}
IblProcessor::~IblProcessor() = default;

auto IblProcessor::RefreshPersistentProbes(
  const EnvironmentProbeState& current, const bool changed) -> RefreshState
{
  const auto result = IblProbePass {}.Refresh(current, changed);
  return { result.requested, result.refreshed, result.probe_state };
}

auto IblProcessor::GetPublishedProducts() const
  -> std::shared_ptr<const IblGpuProducts>
{
  return cache_->published;
}

auto IblProcessor::RefreshSkyLightProducts(const EnvironmentProbeState& current,
  RenderContext& ctx, const StableAtmosphereState& stable,
  const GpuFogParams& fog,
  const std::shared_ptr<resources::TextureBinder>& binder) -> RefreshState
{
  auto& cache = *cache_;
  const auto& light = stable.view_products.sky_light;
  auto next = EnvironmentProbeState {};
  next.probes.probe_revision = current.probes.probe_revision;
  const auto unavailable = [&](const StaticSkyLightUnavailableReason reason) {
    next.static_sky_light.status = StaticSkyLightProductStatus::kUnavailable;
    next.static_sky_light.unavailable_reason = reason;
    next.flags = kEnvironmentProbeStateFlagUnavailable;
    return RefreshState { true, false, next };
  };
  auto key = StaticSkyLightProductKey {
    .source_cubemap = light.cubemap_resource,
    .source_revision = light.enabled ? HashSkyCaptureInputs(stable) : 0U,
    .output_face_size = 128U,
    .source_format_class = light.enabled ? light.source : 0xFFFFFFFFU,
    .source_rotation_radians
    = light.enabled && light.source == kSkyLightSourceSpecifiedCubemap
      ? light.source_cubemap_angle_radians
      : 0.0F,
    .lower_hemisphere_solid_color = light.lower_hemisphere_is_solid_color,
    .lower_hemisphere_color = light.lower_hemisphere_color,
    .lower_hemisphere_blend_alpha = light.lower_hemisphere_blend_alpha,
  };
  if (!light.enabled)
    key = { .output_face_size = 128U, .source_format_class = 0xFFFFFFFFU };
  struct ResidentOwner {
    std::shared_ptr<resources::TextureBinder> binder;
    std::shared_ptr<const resources::TextureBinder::ReadyTexture> texture;
  };
  std::shared_ptr<const resources::TextureBinder::ReadyTexture> resident;
  if (light.enabled && light.source == kSkyLightSourceSpecifiedCubemap) {
    if (light.cubemap_resource.get() == 0U)
      return unavailable(StaticSkyLightUnavailableReason::kMissingCubemap);
    if (!binder)
      return unavailable(
        StaticSkyLightUnavailableReason::kResourceResolveFailed);
    std::ignore = binder->GetOrAllocate(light.cubemap_resource);
    resident = binder->AcquireReadyTexture(light.cubemap_resource);
    if (!resident)
      return unavailable(binder->HasResourceFailed(light.cubemap_resource)
          ? StaticSkyLightUnavailableReason::kResourceResolveFailed
          : StaticSkyLightUnavailableReason::kGpuProductsPending);
    const auto& source_desc = resident->texture->GetDescriptor();
    if (source_desc.texture_type != TextureType::kTextureCube
      || source_desc.array_size != 6U)
      return unavailable(StaticSkyLightUnavailableReason::kNotTextureCube);
    switch (source_desc.format) {
    case Format::kRGBA16Float:
    case Format::kRGBA32Float:
    case Format::kBC6HFloatU:
    case Format::kBC6HFloatS:
    case Format::kR11G11B10Float:
    case Format::kR9G9B9E5Float:
      break;
    default:
      return unavailable(StaticSkyLightUnavailableReason::kUnsupportedFormat);
    }
    key.source_revision = binder->GetContentRevision(resident->srv);
    key.output_face_size = std::bit_floor(source_desc.width);
  }
  const bool changed = !cache.published || cache.key != key;
  if (changed) {
    auto graphics = renderer_.GetGraphics();
    if (!graphics)
      return unavailable(StaticSkyLightUnavailableReason::kProcessingFailed);
    if (!cache.processor) {
      cache.processor = std::make_unique<IblGpuProcessor>(renderer_);
      cache.brdf = std::make_unique<IblBrdfResources>(*graphics);
      cache.captured = std::make_unique<CapturedSkySource>(renderer_);
    }
    const auto brdf = cache.brdf->Prepare();
    if (!brdf)
      return unavailable(StaticSkyLightUnavailableReason::kProcessingFailed);
    const auto settings = IblProcessSettings {
      .face_size = key.output_face_size,
      .source_rotation_radians = key.source_rotation_radians,
      .lower_hemisphere_solid_color = key.lower_hemisphere_solid_color,
      .lower_hemisphere_color = { key.lower_hemisphere_color.x,
        key.lower_hemisphere_color.y, key.lower_hemisphere_color.z },
      .lower_hemisphere_blend_alpha = key.lower_hemisphere_blend_alpha,
    };
    const auto revision
      = cache.revision == UINT32_MAX ? 1U : cache.revision + 1U;
    const auto result = !light.enabled
      ? cache.processor->ProcessSky({}, *brdf, settings, revision)
      : resident
      ? cache.processor->ProcessCubeView(resident->texture, resident->srv,
          std::make_shared<ResidentOwner>(ResidentOwner { binder, resident }),
          *brdf, settings, revision)
      : cache.captured->Process(
          ctx, stable, fog, *cache.processor, *brdf, settings, revision);
    if (!result) {
      return unavailable(StaticSkyLightUnavailableReason::kProcessingFailed);
    }
    cache.published = *result;
    cache.key = key;
    cache.revision = revision;
  }
  next = MakePublishedState(*cache.published, key, light.enabled);
  return { changed, changed, next };
}
} // namespace oxygen::vortex::environment::internal
