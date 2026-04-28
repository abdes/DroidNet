//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Environment/Internal/StaticSkyLightProcessor.h>
#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploadHelpers.h>

namespace oxygen::vortex::environment::internal {

namespace {

  [[nodiscard]] auto MipFaceSize(const std::uint32_t base_face_size,
    const std::uint32_t mip) noexcept -> std::uint32_t
  {
    return (std::max)(1U, base_face_size >> mip);
  }

  [[nodiscard]] auto FaceMipElementCount(const std::uint32_t base_face_size,
    const std::uint32_t mip_count) noexcept -> std::size_t
  {
    auto total = std::size_t { 0U };
    for (std::uint32_t mip = 0U; mip < mip_count; ++mip) {
      const auto mip_size
        = static_cast<std::size_t>(MipFaceSize(base_face_size, mip));
      total += mip_size * mip_size;
    }
    return total;
  }

  [[nodiscard]] auto ProcessedMipOffset(const std::uint32_t face,
    const std::uint32_t mip, const std::uint32_t base_face_size,
    const std::uint32_t mip_count) noexcept -> std::size_t
  {
    auto offset = static_cast<std::size_t>(face)
      * FaceMipElementCount(base_face_size, mip_count);
    for (std::uint32_t current_mip = 0U; current_mip < mip; ++current_mip) {
      const auto mip_size
        = static_cast<std::size_t>(MipFaceSize(base_face_size, current_mip));
      offset += mip_size * mip_size;
    }
    return offset;
  }

  auto ResetResource(Graphics& gfx, std::shared_ptr<graphics::Texture>& texture)
    -> void
  {
    if (texture == nullptr) {
      return;
    }
    auto& registry = gfx.GetResourceRegistry();
    if (registry.Contains(*texture)) {
      registry.UnRegisterResource(*texture);
    }
    gfx.GetDeferredReclaimer().RegisterDeferredRelease(std::move(texture));
  }

  auto ResetResource(Graphics& gfx, std::shared_ptr<graphics::Buffer>& buffer)
    -> void
  {
    if (buffer == nullptr) {
      return;
    }
    auto& registry = gfx.GetResourceRegistry();
    if (registry.Contains(*buffer)) {
      registry.UnRegisterResource(*buffer);
    }
    gfx.GetDeferredReclaimer().RegisterDeferredRelease(std::move(buffer));
  }

  auto EncodeRgba16(const std::span<const glm::vec4> rgba)
    -> std::vector<std::uint16_t>
  {
    std::vector<std::uint16_t> encoded;
    encoded.resize(rgba.size() * 4U);
    for (std::size_t index = 0U; index < rgba.size(); ++index) {
      encoded[index * 4U + 0U] = data::HalfFloat { rgba[index].r }.get();
      encoded[index * 4U + 1U] = data::HalfFloat { rgba[index].g }.get();
      encoded[index * 4U + 2U] = data::HalfFloat { rgba[index].b }.get();
      encoded[index * 4U + 3U] = data::HalfFloat { rgba[index].a }.get();
    }
    return encoded;
  }

} // namespace

struct IblProcessor::StaticSkyLightProductCache {
  StaticSkyLightProductKey key {};
  StaticSkyLightProducts products {};
  std::shared_ptr<graphics::Texture> processed_cubemap {};
  std::shared_ptr<graphics::Buffer> diffuse_sh_buffer {};
  std::optional<upload::UploadTicket> processed_cubemap_upload {};
  std::optional<upload::UploadTicket> diffuse_sh_upload {};
  bool has_submitted_current_key { false };
};

IblProcessor::IblProcessor(Renderer& renderer)
  : renderer_(renderer)
  , probe_pass_(std::make_unique<environment::IblProbePass>())
  , static_sky_light_cache_(std::make_unique<StaticSkyLightProductCache>())
{
}

IblProcessor::~IblProcessor() = default;

auto IblProcessor::RefreshPersistentProbes(
  const EnvironmentProbeState& current_state,
  const bool environment_source_changed) -> RefreshState
{
  static_cast<void>(renderer_);
  const auto refreshed
    = probe_pass_->Refresh(current_state, environment_source_changed);
  return {
    .requested = refreshed.requested,
    .refreshed = refreshed.refreshed,
    .probe_state = refreshed.probe_state,
  };
}

auto IblProcessor::RefreshStaticSkyLightProducts(
  const EnvironmentProbeState& current_state,
  const SkyLightEnvironmentModel& sky_light) -> RefreshState
{
  auto source_cubemap = std::shared_ptr<data::TextureResource> {};
  if (sky_light.enabled && sky_light.source == kSkyLightSourceSpecifiedCubemap
    && sky_light.cubemap_resource.get() != 0U) {
    if (const auto asset_loader = renderer_.GetAssetLoader();
      asset_loader != nullptr) {
      source_cubemap = asset_loader->GetTexture(sky_light.cubemap_resource);
      if (source_cubemap == nullptr) {
        asset_loader->StartLoadTexture(sky_light.cubemap_resource,
          [](std::shared_ptr<data::TextureResource>) { });
        source_cubemap = asset_loader->GetTexture(sky_light.cubemap_resource);
      }
    }
  }

  return RefreshStaticSkyLightProducts(
    current_state, sky_light, source_cubemap.get());
}

auto IblProcessor::RefreshStaticSkyLightProducts(
  const EnvironmentProbeState& current_state,
  const SkyLightEnvironmentModel& sky_light,
  const data::TextureResource* source_cubemap) -> RefreshState
{
  auto refreshed = probe_pass_->RefreshStaticSkyLight(
    current_state, sky_light, source_cubemap);
  auto& state = refreshed.probe_state;
  auto& cache = *static_sky_light_cache_;

  if (state.static_sky_light.unavailable_reason
      != StaticSkyLightUnavailableReason::kGpuProductsPending
    || source_cubemap == nullptr) {
    if (const auto gfx = renderer_.GetGraphics(); gfx != nullptr) {
      ResetResource(*gfx, cache.processed_cubemap);
      ResetResource(*gfx, cache.diffuse_sh_buffer);
    }
    cache = StaticSkyLightProductCache {};
    return {
      .requested = refreshed.requested,
      .refreshed = refreshed.refreshed,
      .probe_state = state,
    };
  }

  const auto key = state.static_sky_light.key;
  const auto key_changed = !cache.has_submitted_current_key || cache.key != key;
  if (key_changed) {
    if (const auto gfx = renderer_.GetGraphics(); gfx != nullptr) {
      ResetResource(*gfx, cache.processed_cubemap);
      ResetResource(*gfx, cache.diffuse_sh_buffer);
    }
    cache = StaticSkyLightProductCache {};
    cache.key = key;

    const auto cpu_products = ProcessStaticSkyLightCubemapCpu(
      *source_cubemap, sky_light, key.output_face_size);
    if (!cpu_products.has_value()) {
      state.static_sky_light.unavailable_reason
        = StaticSkyLightUnavailableReason::kProcessingFailed;
      return {
        .requested = true,
        .refreshed = true,
        .probe_state = state,
      };
    }

    auto gfx = renderer_.GetGraphics();
    if (gfx == nullptr) {
      state.static_sky_light.unavailable_reason
        = StaticSkyLightUnavailableReason::kProcessingFailed;
      return {
        .requested = true,
        .refreshed = true,
        .probe_state = state,
      };
    }

    const auto face_size = cpu_products->output_face_size;
    const auto mip_count = cpu_products->mip_count;
    auto processed_cubemap = gfx->CreateTexture({
      .width = face_size,
      .height = face_size,
      .depth = 1U,
      .array_size = 6U,
      .mip_levels = mip_count,
      .sample_count = 1U,
      .sample_quality = 0U,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTextureCube,
      .debug_name = "Vortex.StaticSkyLight.ProcessedCubemap",
      .is_shader_resource = true,
      .is_render_target = false,
      .is_uav = false,
      .is_typeless = false,
      .is_shading_rate_surface = false,
      .clear_value = {},
      .use_clear_value = false,
      .initial_state = graphics::ResourceStates::kCommon,
      .cpu_access = graphics::ResourceAccessMode::kImmutable,
    });
    if (processed_cubemap == nullptr) {
      state.static_sky_light.unavailable_reason
        = StaticSkyLightUnavailableReason::kProcessingFailed;
      return {
        .requested = true,
        .refreshed = true,
        .probe_state = state,
      };
    }
    processed_cubemap->SetName("Vortex.StaticSkyLight.ProcessedCubemap");

    auto encoded_rgba16 = EncodeRgba16(cpu_products->processed_rgba);
    auto dst_subresources = std::vector<upload::UploadSubresource> {};
    auto src_view = upload::UploadTextureSourceView {};
    dst_subresources.reserve(6U * mip_count);
    src_view.subresources.reserve(6U * mip_count);
    for (std::uint32_t face = 0U; face < 6U; ++face) {
      for (std::uint32_t mip = 0U; mip < mip_count; ++mip) {
        const auto mip_size = MipFaceSize(face_size, mip);
        const auto element_offset
          = ProcessedMipOffset(face, mip, face_size, mip_count);
        const auto element_count
          = static_cast<std::size_t>(mip_size) * mip_size;
        const auto* bytes = reinterpret_cast<const std::byte*>(
          encoded_rgba16.data() + element_offset * 4U);
        const auto byte_count = element_count * sizeof(std::uint16_t) * 4U;
        dst_subresources.push_back({
          .mip = mip,
          .array_slice = face,
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = 0U,
          .height = 0U,
          .depth = 0U,
        });
        src_view.subresources.push_back({
          .bytes = std::span<const std::byte>(bytes, byte_count),
          .row_pitch = mip_size * sizeof(std::uint16_t) * 4U,
          .slice_pitch = static_cast<std::uint32_t>(byte_count),
        });
      }
    }

    auto texture_upload = upload::UploadRequest {
      .kind = upload::UploadKind::kTexture2D,
      .debug_name = "Vortex.StaticSkyLight.ProcessedCubemap.Upload",
      .desc = upload::UploadTextureDesc {
        .dst = processed_cubemap,
        .width = face_size,
        .height = face_size,
        .depth = 1U,
        .format = Format::kRGBA16Float,
      },
      .subresources = std::move(dst_subresources),
      .data = std::move(src_view),
    };
    auto texture_ticket = renderer_.GetUploadCoordinator().Submit(
      texture_upload, renderer_.GetStagingProvider());
    if (!texture_ticket.has_value()) {
      state.static_sky_light.unavailable_reason
        = StaticSkyLightUnavailableReason::kProcessingFailed;
      return {
        .requested = true,
        .refreshed = true,
        .probe_state = state,
      };
    }

    auto& registry = gfx->GetResourceRegistry();
    registry.Register(processed_cubemap);
    auto texture_srv_handle = gfx->GetDescriptorAllocator().AllocateBindless(
      bindless::generated::kTexturesDomain,
      graphics::ResourceViewType::kTexture_SRV);
    if (!texture_srv_handle.IsValid()) {
      state.static_sky_light.unavailable_reason
        = StaticSkyLightUnavailableReason::kProcessingFailed;
      return {
        .requested = true,
        .refreshed = true,
        .probe_state = state,
      };
    }
    const auto processed_cubemap_srv
      = gfx->GetDescriptorAllocator().GetShaderVisibleIndex(texture_srv_handle);
    registry.RegisterView(*processed_cubemap, std::move(texture_srv_handle),
      graphics::TextureViewDescription {
        .view_type = graphics::ResourceViewType::kTexture_SRV,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .format = Format::kRGBA16Float,
        .dimension = TextureType::kTextureCube,
        .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
        .is_read_only_dsv = false,
      });

    auto diffuse_sh_buffer = std::shared_ptr<graphics::Buffer> {};
    auto diffuse_sh_srv = ShaderVisibleIndex { kInvalidShaderVisibleIndex };
    const auto diffuse_sh_bytes = static_cast<std::uint64_t>(
      cpu_products->diffuse_irradiance_sh.size() * sizeof(glm::vec4));
    const auto buffer_result = upload::internal::EnsureBufferAndSrv(*gfx,
      diffuse_sh_buffer, diffuse_sh_srv, diffuse_sh_bytes, sizeof(glm::vec4),
      "Vortex.StaticSkyLight.DiffuseIrradianceSH");
    if (!buffer_result.has_value()) {
      state.static_sky_light.unavailable_reason
        = StaticSkyLightUnavailableReason::kProcessingFailed;
      return {
        .requested = true,
        .refreshed = true,
        .probe_state = state,
      };
    }

    const auto sh_bytes = std::as_bytes(
      std::span<const glm::vec4, kStaticSkyLightDiffuseShElementCount>(
        cpu_products->diffuse_irradiance_sh));
    auto sh_upload = upload::UploadRequest {
      .kind = upload::UploadKind::kBuffer,
      .debug_name = "Vortex.StaticSkyLight.DiffuseIrradianceSH.Upload",
      .desc = upload::UploadBufferDesc {
        .dst = diffuse_sh_buffer,
        .size_bytes = sh_bytes.size_bytes(),
        .dst_offset = 0U,
      },
      .data = upload::UploadDataView { .bytes = sh_bytes },
    };
    auto sh_ticket = renderer_.GetUploadCoordinator().Submit(
      sh_upload, renderer_.GetStagingProvider());
    if (!sh_ticket.has_value()) {
      state.static_sky_light.unavailable_reason
        = StaticSkyLightUnavailableReason::kProcessingFailed;
      return {
        .requested = true,
        .refreshed = true,
        .probe_state = state,
      };
    }

    cache.products = StaticSkyLightProducts {
      .key = key,
      .source_cubemap_srv = kInvalidShaderVisibleIndex,
      .processed_cubemap_srv = processed_cubemap_srv,
      .diffuse_irradiance_sh_srv = diffuse_sh_srv,
      .prefiltered_cubemap_srv = kInvalidShaderVisibleIndex,
      .brdf_lut_srv = kInvalidShaderVisibleIndex,
      .processed_cubemap_max_mip = mip_count - 1U,
      .prefiltered_cubemap_max_mip = 0U,
      .product_revision = current_state.static_sky_light.product_revision + 1U,
      .source_radiance_scale = cpu_products->source_radiance_scale,
      .average_brightness = cpu_products->average_brightness,
      .status = StaticSkyLightProductStatus::kUnavailable,
      .unavailable_reason
      = StaticSkyLightUnavailableReason::kGpuProductsPending,
    };
    cache.processed_cubemap = std::move(processed_cubemap);
    cache.diffuse_sh_buffer = std::move(diffuse_sh_buffer);
    cache.processed_cubemap_upload = *texture_ticket;
    cache.diffuse_sh_upload = *sh_ticket;
    cache.has_submitted_current_key = true;
    refreshed.requested = true;
    refreshed.refreshed = true;
  }

  auto uploads_complete = false;
  if (cache.processed_cubemap_upload.has_value()
    && cache.diffuse_sh_upload.has_value()) {
    auto& uploader = renderer_.GetUploadCoordinator();
    const auto cubemap_complete
      = uploader.IsComplete(*cache.processed_cubemap_upload);
    const auto sh_complete = uploader.IsComplete(*cache.diffuse_sh_upload);
    uploads_complete = cubemap_complete.has_value() && sh_complete.has_value()
      && *cubemap_complete && *sh_complete;
  }

  if (!uploads_complete) {
    state.static_sky_light = cache.products;
    state.static_sky_light.status
      = StaticSkyLightProductStatus::kRegeneratingCurrentKey;
    state.static_sky_light.unavailable_reason
      = StaticSkyLightUnavailableReason::kNone;
    state.static_sky_light.processed_cubemap_srv = kInvalidShaderVisibleIndex;
    state.static_sky_light.diffuse_irradiance_sh_srv
      = kInvalidShaderVisibleIndex;
    state.static_sky_light.processed_cubemap_max_mip = 0U;
    state.valid = false;
    state.flags = kEnvironmentProbeStateFlagUnavailable;
    return {
      .requested = refreshed.requested,
      .refreshed = refreshed.refreshed,
      .probe_state = state,
    };
  }

  state.static_sky_light = cache.products;
  state.static_sky_light.status = StaticSkyLightProductStatus::kValidCurrentKey;
  state.static_sky_light.unavailable_reason
    = StaticSkyLightUnavailableReason::kNone;
  state.probes.environment_map_srv
    = state.static_sky_light.processed_cubemap_srv;
  state.probes.diffuse_sh_srv
    = state.static_sky_light.diffuse_irradiance_sh_srv;
  state.probes.irradiance_map_srv = kInvalidShaderVisibleIndex;
  state.probes.prefiltered_map_srv = kInvalidShaderVisibleIndex;
  state.probes.brdf_lut_srv = kInvalidShaderVisibleIndex;
  state.valid = true;
  state.flags = kEnvironmentProbeStateFlagResourcesValid;

  const auto products_changed = !current_state.valid
    || current_state.static_sky_light.key != state.static_sky_light.key
    || current_state.static_sky_light.processed_cubemap_srv
      != state.static_sky_light.processed_cubemap_srv
    || current_state.static_sky_light.diffuse_irradiance_sh_srv
      != state.static_sky_light.diffuse_irradiance_sh_srv
    || current_state.static_sky_light.processed_cubemap_max_mip
      != state.static_sky_light.processed_cubemap_max_mip;
  if (products_changed) {
    state.probes.probe_revision += 1U;
    state.static_sky_light.product_revision = state.probes.probe_revision;
    refreshed.requested = true;
    refreshed.refreshed = true;
  } else {
    state.probes.probe_revision = current_state.probes.probe_revision;
    state.static_sky_light.product_revision
      = current_state.static_sky_light.product_revision;
    refreshed.requested = false;
    refreshed.refreshed = false;
  }

  return {
    .requested = refreshed.requested,
    .refreshed = refreshed.refreshed,
    .probe_state = state,
  };
}

} // namespace oxygen::vortex::environment::internal
