//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/ext/vector_uint3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
// Completes the traversal returned by Scene::Traverse().
#include <Oxygen/Scene/SceneTraversal.h> // IWYU pragma: keep
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Scene/Types/Traversal.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLightTranslation.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/SceneBackground.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h>
#include <Oxygen/Vortex/Internal/PerViewScope.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Passes/GroundGridPass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextureLeasePool.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Translucency/TranslucencyModule.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/ViewFeatureProfile.h>

namespace oxygen::vortex {

namespace {
  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr std::uint32_t kDirectionalLightFlagAffectsWorld = 1U << 0U;
  constexpr std::uint32_t kDirectionalLightFlagEnvContribution = 1U << 3U;
  constexpr std::uint32_t kDirectionalLightFlagSunLight = 1U << 4U;
  constexpr std::uint32_t kDirectionalLightFlagPerPixelAtmosphereTransmittance
    = 1U << 5U;

  constexpr SceneRenderer::StageOrder kAuthoredStageOrder {
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
  };

  auto RangeTypeToViewType(const bindless_d3d12::RangeType type)
    -> graphics::ResourceViewType
  {
    using graphics::ResourceViewType;

    switch (type) {
    case bindless_d3d12::RangeType::SRV:
      return ResourceViewType::kRawBuffer_SRV;
    case bindless_d3d12::RangeType::Sampler:
      return ResourceViewType::kSampler;
    case bindless_d3d12::RangeType::UAV:
      return ResourceViewType::kRawBuffer_UAV;
    default:
      return ResourceViewType::kNone;
    }
  }

  auto BuildVortexRootBindings() -> std::vector<graphics::RootBindingItem>
  {
    std::vector<graphics::RootBindingItem> bindings;
    bindings.reserve(bindless_d3d12::kRootParamTableCount);

    for (std::uint32_t index = 0; index < bindless_d3d12::kRootParamTableCount;
      ++index) {
      const auto& desc = bindless_d3d12::kRootParamTable.at(index);
      graphics::RootBindingDesc binding {};
      binding.binding_slot_desc.register_index = desc.shader_register;
      binding.binding_slot_desc.register_space = desc.register_space;
      binding.visibility = graphics::ShaderStageFlags::kAll;

      switch (desc.kind) {
      case bindless_d3d12::RootParamKind::DescriptorTable: {
        graphics::DescriptorTableBinding table {};
        if (desc.ranges_count > 0U && desc.ranges.data() != nullptr) {
          const auto& range = desc.ranges.front();
          table.view_type = RangeTypeToViewType(
            static_cast<bindless_d3d12::RangeType>(range.range_type));
          table.base_index = range.base_register;
          table.count = range.num_descriptors
              == (std::numeric_limits<std::uint32_t>::max)()
            ? (std::numeric_limits<std::uint32_t>::max)()
            : range.num_descriptors;
        }
        binding.data = table;
        break;
      }
      case bindless_d3d12::RootParamKind::CBV:
        binding.data = graphics::DirectBufferBinding {};
        break;
      case bindless_d3d12::RootParamKind::RootConstants:
        binding.data
          = graphics::PushConstantsBinding { .size = desc.constants_count };
        break;
      }

      bindings.emplace_back(binding);
    }

    return bindings;
  }

  auto AddBooleanDefine(const bool enabled, std::string_view name,
    std::vector<graphics::ShaderDefine>& defines) -> void
  {
    if (enabled) {
      defines.push_back(
        graphics::ShaderDefine { .name = std::string(name), .value = "1" });
    }
  }

  auto RequireKnownPersistentState(
    graphics::CommandRecorder& recorder, graphics::Texture& texture) -> void
  {
    CHECK_F(recorder.IsResourceTracked(texture)
        || recorder.AdoptKnownResourceState(texture),
      "SceneRenderer: missing authoritative incoming state for '{}'",
      texture.GetName());
  }

  auto ResolveFramebufferColorTexture(
    const observer_ptr<const graphics::Framebuffer> framebuffer)
    -> std::shared_ptr<graphics::Texture>
  {
    if (framebuffer == nullptr) {
      return {};
    }
    const auto& desc = framebuffer->GetDescriptor();
    if (desc.color_attachments.empty()) {
      return {};
    }
    return desc.color_attachments.front().texture;
  }

  auto TrackAuxiliaryColorTexture(Graphics& gfx,
    graphics::CommandRecorder& recorder, const graphics::Texture& texture)
    -> void
  {
    auto& registry = gfx.GetResourceRegistry();
    CHECK_F(registry.Contains(texture),
      "SceneRenderer: auxiliary texture '{}' must be registered before "
      "same-frame consumption",
      texture.GetDescriptor().debug_name);
    if (recorder.IsResourceTracked(texture)) {
      return;
    }
    if (recorder.AdoptKnownResourceState(texture)) {
      return;
    }

    auto initial = texture.GetDescriptor().initial_state;
    if ((initial == graphics::ResourceStates::kUnknown
          || initial == graphics::ResourceStates::kUndefined)
      && texture.GetDescriptor().is_render_target) {
      initial = graphics::ResourceStates::kRenderTarget;
    }
    CHECK_F(initial != graphics::ResourceStates::kUnknown
        && initial != graphics::ResourceStates::kUndefined,
      "SceneRenderer: auxiliary texture '{}' must have a known state",
      texture.GetDescriptor().debug_name);
    recorder.BeginTrackingResourceState(texture, initial, false);
  }

  auto BuildAuxiliaryConsumerViewport(const graphics::Texture& target)
    -> ViewPort
  {
    const auto& desc = target.GetDescriptor();
    const auto width = static_cast<float>(
      std::clamp(desc.width / 3U, 1U, std::min(desc.width, 256U)));
    const auto height = static_cast<float>(
      std::clamp(desc.height / 3U, 1U, std::min(desc.height, 256U)));
    return ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = width,
      .height = height,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
  }

  auto CopyAuxiliaryTextureToRegion(graphics::CommandRecorder& recorder,
    graphics::Texture& source, graphics::Texture& target,
    const ViewPort& viewport) -> void
  {
    CHECK_F(recorder.IsResourceTracked(source),
      "SceneRenderer: auxiliary copy source '{}' must be tracked",
      source.GetDescriptor().debug_name);
    CHECK_F(recorder.IsResourceTracked(target),
      "SceneRenderer: auxiliary copy target '{}' must be tracked",
      target.GetDescriptor().debug_name);

    const auto& src_desc = source.GetDescriptor();
    const auto& dst_desc = target.GetDescriptor();
    CHECK_F(src_desc.format == dst_desc.format,
      "SceneRenderer: auxiliary copy requires matching formats (source '{}' "
      "target '{}')",
      source.GetDescriptor().debug_name, target.GetDescriptor().debug_name);

    const auto dst_x = static_cast<std::uint32_t>(std::clamp(
      viewport.top_left_x, 0.0F, static_cast<float>(dst_desc.width)));
    const auto dst_y = static_cast<std::uint32_t>(std::clamp(
      viewport.top_left_y, 0.0F, static_cast<float>(dst_desc.height)));
    const auto max_dst_w = dst_desc.width > dst_x ? dst_desc.width - dst_x : 0U;
    const auto max_dst_h
      = dst_desc.height > dst_y ? dst_desc.height - dst_y : 0U;
    const auto requested_w = static_cast<std::uint32_t>(
      std::max(0.0F, std::min(viewport.width, static_cast<float>(max_dst_w))));
    const auto requested_h = static_cast<std::uint32_t>(
      std::max(0.0F, std::min(viewport.height, static_cast<float>(max_dst_h))));
    const auto copy_width = std::min(src_desc.width, requested_w);
    const auto copy_height = std::min(src_desc.height, requested_h);
    if (copy_width == 0U || copy_height == 0U) {
      return;
    }

    recorder.RequireResourceState(
      source, graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(target, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();

    const graphics::TextureSlice src_slice {
      .x = 0,
      .y = 0,
      .z = 0,
      .width = copy_width,
      .height = copy_height,
      .depth = 1,
    };
    const graphics::TextureSlice dst_slice {
      .x = dst_x,
      .y = dst_y,
      .z = 0,
      .width = copy_width,
      .height = copy_height,
      .depth = 1,
    };
    constexpr graphics::TextureSubResourceSet subresources {
      .base_mip_level = 0,
      .num_mip_levels = 1,
      .base_array_slice = 0,
      .num_array_slices = 1,
    };
    recorder.CopyTexture(
      source, src_slice, subresources, target, dst_slice, subresources);
  }

  auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
    const RenderContext& ctx, const SceneTextures& scene_textures) -> void
  {
    if (ctx.current_view.resolved_view != nullptr) {
      recorder.SetViewport(ctx.current_view.resolved_view->Viewport());
      recorder.SetScissors(ctx.current_view.resolved_view->Scissor());
      return;
    }

    const auto extent = scene_textures.GetExtent();
    recorder.SetViewport({
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(extent.x),
      .height = static_cast<float>(extent.y),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
    recorder.SetScissors({
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(extent.x),
      .bottom = static_cast<std::int32_t>(extent.y),
    });
  }

  auto IsReverseZ(const RenderContext& ctx) -> bool
  {
    return ctx.current_view.resolved_view == nullptr
      || ctx.current_view.resolved_view->ReverseZ();
  }

  auto ResolveScreenHzbRequest(const RenderContext& ctx,
    const ShadingMode shading_mode) -> RenderContext::ScreenHzbRequest
  {
    auto request = RenderContext::ScreenHzbRequest {};
    const auto* active_view = ctx.GetActiveViewEntry();
    const auto is_scene_view = active_view != nullptr
      ? active_view->is_scene_view
      : ctx.current_view.resolved_view != nullptr;
    if (!is_scene_view) {
      return request;
    }

    if (shading_mode == ShadingMode::kDeferred) {
      request.current_closest = true;
    }
    if (shading_mode == ShadingMode::kDeferred
      || ctx.current_view.with_local_fog) {
      request.current_furthest = true;
      request.publish_previous_furthest = true;
    }
    return request;
  }

  auto RetireExtractTexture(
    Graphics& /*gfx*/, std::shared_ptr<graphics::Texture>& texture) -> void
  {
    // Artifact ownership schedules GPU-safe descriptor retirement on last use.
    texture.reset();
  }

  auto ResolveViewOutputTarget(const RenderContext& ctx)
    -> observer_ptr<const graphics::Framebuffer>
  {
    if (const auto* active_view = ctx.GetActiveViewEntry();
      active_view != nullptr) {
      if (active_view->composite_source != nullptr) {
        return observer_ptr<const graphics::Framebuffer> {
          active_view->composite_source.get()
        };
      }
      if (active_view->primary_target != nullptr) {
        return observer_ptr<const graphics::Framebuffer> {
          active_view->primary_target.get()
        };
      }
    }

    return ctx.pass_target;
  }

  auto ShaderVisibleDescriptor(const ShaderVisibleIndex slot) -> std::string
  {
    if (slot == kInvalidShaderVisibleIndex) {
      return "invalid";
    }
    return "bindless:" + std::to_string(slot.get());
  }

  auto SceneTextureDescriptor(const std::uint32_t slot) -> std::string
  {
    if (slot == SceneTextureBindings::kInvalidIndex) {
      return "invalid";
    }
    return "bindless:" + std::to_string(slot);
  }

  auto OcclusionStatsDescriptor(const OcclusionStats& stats) -> std::string
  {
    return "draws=" + std::to_string(stats.draw_count)
      + " candidates=" + std::to_string(stats.candidate_count)
      + " submitted=" + std::to_string(stats.submitted_count)
      + " visible=" + std::to_string(stats.visible_count)
      + " occluded=" + std::to_string(stats.occluded_count)
      + " overflow_visible=" + std::to_string(stats.overflow_visible_count)
      + " fallback=" + std::string { to_string(stats.fallback_reason) }
    + " hzb=" + (stats.current_furthest_hzb_available ? "1" : "0")
      + " prev=" + (stats.previous_results_valid ? "1" : "0")
      + " valid=" + (stats.results_valid ? "1" : "0");
  }

  auto BasePassDrawDescriptor(const std::uint32_t draw_count,
    const std::uint32_t occlusion_culled_draw_count) -> std::string
  {
    return "draws=" + std::to_string(draw_count)
      + " occlusion_culled=" + std::to_string(occlusion_culled_draw_count);
  }

  auto TranslucencySkipReasonName(const TranslucencySkipReason reason)
    -> std::string_view
  {
    switch (reason) {
    case TranslucencySkipReason::kNone:
      return "none";
    case TranslucencySkipReason::kNotRequested:
      return "not_requested";
    case TranslucencySkipReason::kNoDraws:
      return "no_draws";
    case TranslucencySkipReason::kMissingGraphics:
      return "missing_graphics";
    case TranslucencySkipReason::kMissingViewConstants:
      return "missing_view_constants";
    case TranslucencySkipReason::kRecorderUnavailable:
      return "recorder_unavailable";
    }
    return "unknown";
  }

  auto TranslucencyDrawDescriptor(const TranslucencyExecutionResult& result)
    -> std::string
  {
    return "draws=" + std::to_string(result.draw_count) + " skip="
      + std::string { TranslucencySkipReasonName(result.skip_reason) };
  }

  auto TranslucencyMissingInputs(const TranslucencyExecutionResult& result,
    const bool module_available) -> std::vector<std::string>
  {
    if (!module_available) {
      return { "TranslucencyModule" };
    }
    if (result.skip_reason == TranslucencySkipReason::kMissingGraphics) {
      return { "Graphics" };
    }
    if (result.skip_reason == TranslucencySkipReason::kMissingViewConstants) {
      return { "ViewConstants" };
    }
    if (result.skip_reason == TranslucencySkipReason::kRecorderUnavailable) {
      return { "CommandRecorder" };
    }
    return {};
  }

  auto RecordDiagnosticsPass(Renderer& renderer, DiagnosticsPassRecord record)
    -> void
  {
    renderer.GetDiagnosticsService().RecordPass(std::move(record));
  }

  auto RecordDiagnosticsProduct(
    Renderer& renderer, DiagnosticsProductRecord record) -> void
  {
    renderer.GetDiagnosticsService().RecordProduct(std::move(record));
  }

  auto RecordDiagnosticsViewProduct(Renderer& renderer, std::string name,
    std::string producer_pass, const ShaderVisibleIndex slot) -> void
  {
    RecordDiagnosticsProduct(renderer,
      DiagnosticsProductRecord {
        .name = std::move(name),
        .producer_pass = std::move(producer_pass),
        .descriptor = ShaderVisibleDescriptor(slot),
        .published = slot != kInvalidShaderVisibleIndex,
        .valid = slot != kInvalidShaderVisibleIndex,
      });
  }

  auto ResolveWorldRotation(
    const scene::Scene& scene, const scene::SceneNodeImpl& node) -> glm::quat
  {
    const auto& transform
      = node.GetComponent<scene::detail::TransformComponent>();
    const auto ignore_parent = node.GetFlags().GetEffectiveValue(
      scene::SceneNodeFlags::kIgnoreParentTransform);
    auto rotation = transform.GetLocalRotation();
    if (const auto parent = node.AsGraphNode().GetParent();
      parent.IsValid() && !ignore_parent) {
      rotation
        = ResolveWorldRotation(scene, scene.GetNodeImplRef(parent)) * rotation;
    }
    return rotation;
  }

  auto ResolveWorldMatrix(
    const scene::Scene& scene, const scene::SceneNodeImpl& node) -> glm::mat4
  {
    const auto& transform
      = node.GetComponent<scene::detail::TransformComponent>();
    const auto ignore_parent = node.GetFlags().GetEffectiveValue(
      scene::SceneNodeFlags::kIgnoreParentTransform);
    auto world = transform.GetLocalMatrix();
    if (const auto parent = node.AsGraphNode().GetParent();
      parent.IsValid() && !ignore_parent) {
      world = ResolveWorldMatrix(scene, scene.GetNodeImplRef(parent)) * world;
    }
    return world;
  }

  auto ResolveWorldPosition(
    const scene::Scene& scene, const scene::SceneNodeImpl& node) -> glm::vec3
  {
    const auto world = ResolveWorldMatrix(scene, node);
    return glm::vec3(world[3]);
  }

  auto ComputeDirectionWs(
    const scene::Scene& scene, const scene::SceneNodeImpl& node) -> glm::vec3
  {
    const auto direction
      = ResolveWorldRotation(scene, node) * space::move::Forward;
    const auto length_sq = glm::dot(direction, direction);
    if (length_sq <= math::EpsilonDirection) {
      return space::move::Forward;
    }
    return glm::normalize(direction);
  }

  auto BuildFrameLightSelection(const scene::Scene& scene_ref,
    const scene::DirectionalLightResolver& resolver,
    const std::uint64_t selection_epoch) -> FrameLightSelection
  {
    auto selection = FrameLightSelection {};
    selection.selection_epoch = selection_epoch;
    selection.scene_generation = scene_ref.GetLifetimeId().get();
    const auto environment = scene_ref.GetEnvironment();
    const auto* const atmosphere = environment != nullptr
      ? environment->TryGetSystem<scene::environment::SkyAtmosphere>().get()
      : nullptr;
    const auto& atmosphere_lights = resolver.ResolveAtmosphereLights();
    if (atmosphere_lights.slots[0].has_value()) {
      const auto& primary = *atmosphere_lights.slots[0];
      const auto csm = scene::CanonicalizeCascadedShadowSettings(
        primary.Light().CascadedShadows());
      const auto primary_atmosphere_light
        = environment::internal::BuildAtmosphereLightModel(
          primary, 0U, atmosphere);
      auto atmosphere_mode_flags = kDirectionalLightAtmosphereModeFlagAuthority;
      if ((primary_atmosphere_light.direct_light_authority_flags
            & environment::kAtmosphereDirectLightFlagPerPixelTransmittance)
        != 0U) {
        atmosphere_mode_flags
          |= kDirectionalLightAtmosphereModeFlagPerPixelTransmittance;
      }
      if ((primary_atmosphere_light.direct_light_authority_flags
            & environment::
              kAtmosphereDirectLightFlagHasBakedGroundTransmittance)
        != 0U) {
        atmosphere_mode_flags
          |= kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance;
      }
      selection.directional_light = FrameDirectionalLightSelection {
        .direction = primary_atmosphere_light.direction_to_light_ws,
        .source_radius = primary_atmosphere_light.angular_size_radians,
        .color = primary.Light().Common().color_rgb,
        .illuminance_lux = primary.Light().GetIntensityLux(),
        .exposure_compensation_ev
        = primary.Light().Common().exposure_compensation_ev,
        .transmittance_toward_sun_rgb
        = primary_atmosphere_light.transmittance_toward_sun_rgb,
        .atmosphere_light_slot = 0U,
        .atmosphere_mode_flags = atmosphere_mode_flags,
        .shadow_flags = (primary.Light().Common().casts_shadows
                            ? kDirectionalLightShadowFlagCastsShadows
                            : 0U)
          | (primary.Light().Common().shadow.contact_shadows
              ? kLightFlagContactShadows
              : 0U),
        .cascade_count
        = primary.Light().Common().casts_shadows ? csm.cascade_count : 0U,
        .cascade_split_mode
        = csm.split_mode == scene::DirectionalCsmSplitMode::kManualDistances
          ? FrameDirectionalCsmSplitMode::kManualDistances
          : FrameDirectionalCsmSplitMode::kGenerated,
        .max_shadow_distance = csm.max_shadow_distance,
        .cascade_distances = csm.cascade_distances,
        .distribution_exponent = csm.distribution_exponent,
        .transition_fraction = csm.transition_fraction,
        .distance_fadeout_fraction = csm.distance_fadeout_fraction,
        .shadow_bias = primary.Light().Common().shadow.bias,
        .shadow_normal_bias = primary.Light().Common().shadow.normal_bias,
        .shadow_resolution_hint = static_cast<std::uint32_t>(
          primary.Light().Common().shadow.resolution_hint),
      };
    }

    const auto visitor
      = [&selection, &scene_ref](const scene::ConstVisitedNode& visited,
          const bool dry_run) -> scene::VisitResult {
      std::ignore = dry_run;

      const auto& node = *visited.node_impl;
      if (!node.HasComponent<scene::detail::TransformComponent>()) {
        return scene::VisitResult::kContinue;
      }

      if (node.HasComponent<scene::DirectionalLight>()) {
        return scene::VisitResult::kContinue;
      }

      if (node.HasComponent<scene::PointLight>()) {
        const auto& light = node.GetComponent<scene::PointLight>();
        if (!light.Common().affects_world) {
          return scene::VisitResult::kContinue;
        }

        selection.local_lights.push_back(FrameLocalLightSelection {
          .kind = LocalLightKind::kPoint,
          .position = ResolveWorldPosition(scene_ref, node),
          .range = light.GetRange(),
          .color = light.Common().color_rgb,
          .luminous_flux_lm = light.GetLuminousFluxLm(),
          .exposure_compensation_ev = light.Common().exposure_compensation_ev,
          .direction = ComputeDirectionWs(scene_ref, node),
          .source_radius = light.GetSourceRadius(),
          .flags
          = (light.Common().casts_shadows ? kLocalLightFlagCastsShadows : 0U)
            | (light.Common().shadow.contact_shadows ? kLightFlagContactShadows
                                                     : 0U),
          .shadow_bias = light.Common().shadow.bias,
          .shadow_normal_bias = light.Common().shadow.normal_bias,
          .shadow_resolution_hint
          = static_cast<std::uint32_t>(light.Common().shadow.resolution_hint),
        });
        return scene::VisitResult::kContinue;
      }

      if (node.HasComponent<scene::SpotLight>()) {
        const auto& light = node.GetComponent<scene::SpotLight>();
        if (!light.Common().affects_world) {
          return scene::VisitResult::kContinue;
        }

        selection.local_lights.push_back(FrameLocalLightSelection {
          .kind = LocalLightKind::kSpot,
          .position = ResolveWorldPosition(scene_ref, node),
          .range = light.GetRange(),
          .color = light.Common().color_rgb,
          .luminous_flux_lm = light.GetLuminousFluxLm(),
          .exposure_compensation_ev = light.Common().exposure_compensation_ev,
          .direction = ComputeDirectionWs(scene_ref, node),
          .inner_cone_half_angle_radians = light.GetInnerConeAngleRadians(),
          .outer_cone_half_angle_radians = light.GetOuterConeAngleRadians(),
          .source_radius = light.GetSourceRadius(),
          .flags
          = (light.Common().casts_shadows ? kLocalLightFlagCastsShadows : 0U)
            | (light.Common().shadow.contact_shadows ? kLightFlagContactShadows
                                                     : 0U),
          .shadow_bias = light.Common().shadow.bias,
          .shadow_normal_bias = light.Common().shadow.normal_bias,
          .shadow_resolution_hint
          = static_cast<std::uint32_t>(light.Common().shadow.resolution_hint),
        });
      }

      return scene::VisitResult::kContinue;
    };

    [[maybe_unused]] const auto traversal_result
      = scene_ref.Traverse().Traverse(
        visitor, scene::TraversalOrder::kPreOrder, scene::VisibleFilter {});
    return selection;
  }

  auto CollectLightingViewInputs(const RenderContext& ctx,
    const InitViewsModule* init_views,
    std::vector<PreparedViewLightingInput>& out) -> void
  {
    out.clear();
    if (init_views == nullptr) {
      return;
    }

    for (const auto& view : ctx.frame_views) {
      if (!view.is_scene_view) {
        continue;
      }
      out.push_back(PreparedViewLightingInput {
        .view_id = view.view_id,
        .prepared_scene = observer_ptr<const PreparedSceneFrame> {
          init_views->GetPreparedSceneFrame(view.view_id),
        },
        .resolved_view = view.resolved_view,
        .composition_view = view.composition_view,
      });
    }

    if (out.empty() && ctx.current_view.view_id != kInvalidViewId) {
      out.push_back(PreparedViewLightingInput {
        .view_id = ctx.current_view.view_id,
        .prepared_scene = ctx.current_view.prepared_frame,
        .resolved_view = ctx.current_view.resolved_view,
        .composition_view = ctx.current_view.composition_view,
      });
    }
  }

  auto CollectCurrentShadowViewInput(const RenderContext& ctx,
    const InitViewsModule* init_views,
    std::vector<PreparedViewShadowInput>& out) -> void
  {
    out.clear();
    if (init_views == nullptr) {
      return;
    }

    if (ctx.current_view.view_id != kInvalidViewId) {
      out.push_back(PreparedViewShadowInput {
        .view_id = ctx.current_view.view_id,
        .prepared_scene = ctx.current_view.prepared_frame != nullptr
          ? ctx.current_view.prepared_frame
          : observer_ptr<const PreparedSceneFrame> {
              init_views->GetPreparedSceneFrame(ctx.current_view.view_id),
            },
        .resolved_view = ctx.current_view.resolved_view,
        .view_constants = observer_ptr<const graphics::Buffer> {
          ctx.view_constants.get(),
        },
        .composition_view = ctx.current_view.composition_view,
      });
    }
  }

  auto BuildDirectionalLightFramebuffer(const SceneTextures& scene_textures)
    -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = scene_textures.GetSceneColorResource(),
      .format = scene_textures.GetSceneColor().GetDescriptor().format,
    });
    return desc;
  }

  auto IsDeferredDebugVisualizationMode(const ShaderDebugMode mode) -> bool
  {
    switch (mode) {
    case ShaderDebugMode::kBaseColor:
    case ShaderDebugMode::kWorldNormals:
    case ShaderDebugMode::kRoughness:
    case ShaderDebugMode::kMetalness:
    case ShaderDebugMode::kDirectionalShadowMask:
    case ShaderDebugMode::kSceneDepthRaw:
    case ShaderDebugMode::kSceneDepthLinear:
    case ShaderDebugMode::kMaskedAlphaCoverage:
      return true;
    default:
      return false;
    }
  }

  auto GetDeferredDebugVisualizationName(const ShaderDebugMode mode)
    -> std::string_view
  {
    switch (mode) {
    case ShaderDebugMode::kBaseColor:
      return "BaseColor";
    case ShaderDebugMode::kWorldNormals:
      return "WorldNormals";
    case ShaderDebugMode::kRoughness:
      return "Roughness";
    case ShaderDebugMode::kMetalness:
      return "Metalness";
    case ShaderDebugMode::kDirectionalShadowMask:
      return "DirectionalShadowMask";
    case ShaderDebugMode::kSceneDepthRaw:
      return "SceneDepthRaw";
    case ShaderDebugMode::kSceneDepthLinear:
      return "SceneDepthLinear";
    case ShaderDebugMode::kMaskedAlphaCoverage:
      return "MaskedAlphaCoverage";
    default:
      return "Disabled";
    }
  }

  auto BuildDebugVisualizationFramebuffer(const SceneTextures& scene_textures)
    -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = scene_textures.GetSceneColorResource(),
      .format = scene_textures.GetSceneColor().GetDescriptor().format,
    });
    return desc;
  }

  auto NeedsDebugVisualizationFramebufferRebuild(
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const SceneTextures& scene_textures) -> bool
  {
    if (!framebuffer) {
      return true;
    }

    const auto& desc = framebuffer->GetDescriptor();
    return desc.color_attachments.size() != 1U
      || desc.color_attachments[0].texture.get()
      != scene_textures.GetSceneColorResource().get()
      || desc.depth_attachment.texture != nullptr;
  }

  auto BuildDebugVisualizationPipelineDesc(const SceneTextures& scene_textures,
    const ShaderDebugMode mode) -> graphics::GraphicsPipelineDesc
  {
    CHECK_F(IsDeferredDebugVisualizationMode(mode),
      "SceneRenderer: unsupported deferred debug visualization mode '{}'",
      to_string(mode));

    auto root_bindings = BuildVortexRootBindings();
    auto pixel_defines = std::vector<graphics::ShaderDefine> {};
    AddBooleanDefine(true, GetShaderDebugDefineName(mode), pixel_defines);

    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Stages/BasePass/BasePassDebugView.hlsl",
        .entry_point = "BasePassDebugViewVS",
        .defines = {},
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Stages/BasePass/BasePassDebugView.hlsl",
        .entry_point = "BasePassDebugViewPS",
        .defines = std::move(pixel_defines),
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(graphics::DepthStencilStateDesc::Disabled())
      .SetBlendState({})
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = {
          scene_textures.GetSceneColor().GetDescriptor().format,
        },
        .sample_count = scene_textures.GetSceneColor().GetDescriptor().sample_count,
        .sample_quality
        = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(fmt::format("Vortex.DebugVisualization.{}",
        GetDeferredDebugVisualizationName(mode)))
      .Build();
  }

  auto ResolveViewportExtent(const engine::ViewContext& view)
    -> std::optional<glm::uvec2>
  {
    if (!view.view.viewport.IsValid()) {
      return std::nullopt;
    }

    return glm::uvec2 {
      std::max(1U, static_cast<std::uint32_t>(view.view.viewport.width)),
      std::max(1U, static_cast<std::uint32_t>(view.view.viewport.height)),
    };
  }

  auto ResolveFramebufferExtent(const graphics::Framebuffer* framebuffer)
    -> std::optional<glm::uvec2>
  {
    if (framebuffer == nullptr) {
      return std::nullopt;
    }

    const auto& desc = framebuffer->GetDescriptor();
    if (!desc.color_attachments.empty()
      && desc.color_attachments.front().texture != nullptr) {
      const auto& texture_desc
        = desc.color_attachments.front().texture->GetDescriptor();
      return glm::uvec2 {
        std::max(1U, texture_desc.width),
        std::max(1U, texture_desc.height),
      };
    }

    if (desc.depth_attachment.IsValid()) {
      const auto& texture_desc = desc.depth_attachment.texture->GetDescriptor();
      return glm::uvec2 {
        std::max(1U, texture_desc.width),
        std::max(1U, texture_desc.height),
      };
    }

    return std::nullopt;
  }

  auto ResolveFrameViewportExtent(const engine::FrameContext& frame)
    -> std::optional<glm::uvec2>
  {
    // Preserve the future multi-view shell by sizing against the maximum
    // scene-view viewport envelope only while views have not materialized
    // targets yet. Once a view has target pointers, frame start deliberately
    // avoids dereferencing them because resize can replace their owners before
    // the view lifecycle republishes fresh frame-context targets. The
    // render-time sync below uses the authoritative framebuffer extent.
    auto max_scene_extent = std::optional<glm::uvec2> {};
    auto max_non_scene_extent = std::optional<glm::uvec2> {};
    auto has_scene_target = false;
    auto has_non_scene_target = false;

    const auto accumulate = [](std::optional<glm::uvec2>& current,
                              const glm::uvec2 candidate) -> void {
      if (!current.has_value()) {
        current = candidate;
        return;
      }
      current->x = std::max(current->x, candidate.x);
      current->y = std::max(current->y, candidate.y);
    };

    for (const auto& view_ref : frame.GetViews()) {
      const auto& view = view_ref.get();
      const auto has_target
        = view.render_target != nullptr || view.composite_source != nullptr;
      if (view.metadata.is_scene_view) {
        has_scene_target = has_scene_target || has_target;
      } else {
        has_non_scene_target = has_non_scene_target || has_target;
      }
      if (has_target) {
        continue;
      }

      const auto viewport_extent = ResolveViewportExtent(view);
      if (!viewport_extent.has_value()) {
        continue;
      }

      if (view.metadata.is_scene_view) {
        accumulate(max_scene_extent, *viewport_extent);
      } else {
        accumulate(max_non_scene_extent, *viewport_extent);
      }
    }

    if (has_scene_target) {
      return std::nullopt;
    }
    if (max_scene_extent.has_value()) {
      return max_scene_extent;
    }
    if (has_non_scene_target) {
      return std::nullopt;
    }
    return max_non_scene_extent;
  }

  auto ResolveRenderContextTargetExtent(const RenderContext& ctx)
    -> std::optional<glm::uvec2>
  {
    auto extent = std::optional<glm::uvec2> {};
    const auto accumulate = [&extent](const graphics::Framebuffer* framebuffer,
                              const char* target_name) -> void {
      const auto candidate = ResolveFramebufferExtent(framebuffer);
      if (!candidate.has_value()) {
        return;
      }
      if (!extent.has_value()) {
        extent = candidate;
        return;
      }

      CHECK_F(*extent == *candidate,
        "SceneRenderer: current view render targets have inconsistent "
        "extents while resolving {} ({}x{} vs {}x{})",
        target_name, extent->x, extent->y, candidate->x, candidate->y);
    };

    if (const auto* active_view = ctx.GetActiveViewEntry();
      active_view != nullptr) {
      accumulate(active_view->render_target.get(), "render_target");
      accumulate(active_view->composite_source.get(), "composite_source");
      accumulate(active_view->primary_target.get(), "primary_target");
    }
    accumulate(ctx.pass_target.get(), "pass_target");

    return extent;
  }

  auto ResolveDepthSrvFormat(const Format texture_format) -> Format
  {
    switch (texture_format) {
    case Format::kDepth32:
      return Format::kR32Float;
    case Format::kDepth32Stencil8:
    case Format::kDepth24Stencil8:
      return texture_format;
    case Format::kDepth16:
      return Format::kR16UNorm;
    default:
      return texture_format;
    }
  }

  auto ResolveStencilSrvFormat(const Format texture_format) -> Format
  {
    switch (texture_format) {
    case Format::kDepth24Stencil8:
      return Format::kDepth24Stencil8;
    case Format::kDepth32Stencil8:
      return Format::kDepth32Stencil8;
    default:
      return texture_format;
    }
  }

  auto MakeSrvDesc(const graphics::Texture& texture, const Format format)
    -> graphics::TextureViewDescription
  {
    return {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    };
  }

  auto MakeUavDesc(const graphics::Texture& texture, const Format format)
    -> graphics::TextureViewDescription
  {
    return {
      .view_type = graphics::ResourceViewType::kTexture_UAV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    };
  }

  auto InitialExposureSceneConfig(
    const Renderer& renderer, SceneTexturesConfig config) -> SceneTexturesConfig
  {
    if (renderer.HasCapability(
          RendererCapabilityFamily::kFinalOutputComposition)) {
      config.scene_color_format = Format::kRGBA32Float;
    }
    return config;
  }

  auto ResolveAuthoredPostProcessConfig(const RenderContext& ctx,
    PostProcessService& service,
    const RenderContext::ViewExecutionEntry* captured_view = nullptr)
    -> ResolvedPostProcessConfig
  {
    auto config = PostProcessConfig {};
    auto requested = scene::ExposureSettings {};
    const auto* scene = ctx.GetScene().get();
    if (scene != nullptr) {
      const auto environment = scene->GetEnvironment();
      if (environment != nullptr) {
        const auto post_process
          = environment->TryGetSystem<scene::environment::PostProcessVolume>();
        if (post_process) {
          requested = post_process->GetExposureSettings();
          config.enable_bloom = post_process->GetBloomIntensity() > 0.0F;
          config.bloom_intensity = post_process->GetBloomIntensity();
          config.bloom_threshold = post_process->GetBloomThreshold();
          config.tone_mapper = post_process->GetToneMapper();
          config.gamma = post_process->GetDisplayGamma();
        }
      }
    }
    const auto& override_settings = captured_view
      ? captured_view->exposure_override
      : ctx.current_view.exposure_override;
    const auto view = captured_view ? captured_view->composition_view
                                    : ctx.current_view.composition_view;
    const auto resolved_view = captured_view ? captured_view->resolved_view
                                             : ctx.current_view.resolved_view;
    if (override_settings.has_value()) {
      requested = *override_settings;
    } else if (view != nullptr && view->render_settings.exposure.has_value()) {
      requested = *view->render_settings.exposure;
    }
    const auto camera_ev = resolved_view != nullptr ? resolved_view->CameraEv()
                                                    : std::optional<float> {};
    const bool diagnostic
      = (captured_view ? captured_view->shader_debug_mode_override.value_or(
                           ctx.shader_debug_mode)
                       : ctx.shader_debug_mode)
        != ShaderDebugMode::kDisabled
      || (captured_view
             ? captured_view->render_mode_override.value_or(ctx.render_mode)
             : ctx.render_mode)
        == RenderMode::kWireframe;
    std::ignore = service.CaptureViewExposureSettings(
      captured_view ? captured_view->view_id : ctx.current_view.view_id,
      captured_view ? captured_view->view_state_handle
                    : ctx.current_view.view_state_handle,
      requested, camera_ev, diagnostic, ctx.GetScene());

    if (diagnostic) {
      config.temporary_unit_exposure = true;
      config.tone_mapper = engine::ToneMapper::kNone;
      config.enable_bloom = false;
      config.bloom_intensity = 0.0F;
      config.bloom_threshold = 0.0F;
    }
    return service.BuildPassConfig(config,
      captured_view ? captured_view->view_id : ctx.current_view.view_id,
      captured_view ? captured_view->view_state_handle
                    : ctx.current_view.view_state_handle);
  }

  auto CollectExposureProducts(const RenderContext& ctx,
    const graphics::Texture& accumulated, ShaderVisibleIndex accumulated_srv,
    const EnvironmentLightingService* environment_service)
    -> std::vector<postprocess::ExposurePass::HdrProduct>
  {
    auto products = std::vector<postprocess::ExposurePass::HdrProduct> {
      {
        .texture = &accumulated,
        .srv = accumulated_srv,
        .id = 11U,
        .metering = true,
        .coverage = environment::ResolveSceneBackground(ctx).has_value(),
        .composed_error = true,
      },
    };
    const auto* radiance = environment_service
      ? environment_service->InspectViewRadianceResources(
          ctx.current_view.view_id)
      : nullptr;
    const auto* published = environment_service
      ? environment_service->InspectEnvironmentViewProducts(
          ctx.current_view.view_id)
      : nullptr;
    if (environment_service) {
      const auto& authored
        = environment_service->InspectAtmosphereState().view_products;
      const bool atmosphere_required = ctx.current_view.with_atmosphere
        && ctx.current_view.feature_mask.Has(
          CompositionView::ViewFeatureMask::kEnvironment)
        && authored.atmosphere.enabled;
      const bool fog_required = ctx.current_view.with_height_fog
        && ctx.current_view.feature_mask.Has(
          CompositionView::ViewFeatureMask::kVolumetrics)
        && authored.volumetric_fog.enabled;
      if (atmosphere_required) {
        products.push_back({
          .texture = radiance ? radiance->sky_view.get() : nullptr,
          .srv = published ? published->sky_view_lut_srv
                           : kInvalidShaderVisibleIndex,
          .id = 5U,
          .transmittance = true,
        });
        products.push_back({
          .texture = radiance ? radiance->aerial_perspective.get() : nullptr,
          .srv = published ? published->camera_aerial_perspective_srv
                           : kInvalidShaderVisibleIndex,
          .id = 6U,
          .transmittance = true,
          .consumer_rgb_gain
          = std::fmax(authored.atmosphere.aerial_scattering_strength, 0.0F),
        });
      }
      if (fog_required) {
        products.push_back({
          .texture = radiance ? radiance->volumetric_fog.get() : nullptr,
          .srv = published ? published->integrated_light_scattering_srv
                           : kInvalidShaderVisibleIndex,
          .id = 10U,
          .transmittance = true,
        });
      }
    }
    // SceneColor already accumulates all raster/additive terms in FP32. Split
    // the narrowing allowance across the actual intermediate allocations and
    // the final resolve, rather than granting each the full image budget.
    const float share = 1.0F / static_cast<float>(products.size());
    for (auto& product : products) {
      product.error_budget_share = share;
    }
    return products;
  }

  auto HasPublishedGBufferBindings(const SceneTextureBindings& bindings) -> bool
  {
    return std::ranges::all_of(bindings.gbuffer_srvs.begin(),
      bindings.gbuffer_srvs.begin()
        + static_cast<std::ptrdiff_t>(GBufferIndex::kActiveCount),
      [](const std::uint32_t index) -> bool {
        return index != SceneTextureBindings::kInvalidIndex;
      });
  }

  auto HasAnyPublishedGBufferBinding(const SceneTextureBindings& bindings)
    -> bool
  {
    return std::ranges::any_of(bindings.gbuffer_srvs.begin(),
      bindings.gbuffer_srvs.begin()
        + static_cast<std::ptrdiff_t>(GBufferIndex::kActiveCount),
      [](const std::uint32_t index) -> bool {
        return index != SceneTextureBindings::kInvalidIndex;
      });
  }

  auto HasPublishedDeferredLightingInputs(const SceneTextureBindings& bindings)
    -> bool
  {
    return bindings.scene_depth_srv != SceneTextureBindings::kInvalidIndex
      && bindings.scene_color_uav != SceneTextureBindings::kInvalidIndex
      && HasPublishedGBufferBindings(bindings);
  }

} // namespace

SceneRenderer::ExtractArtifact::ExtractArtifact() = default;
SceneRenderer::ExtractArtifact::~ExtractArtifact() = default;
SceneRenderer::ExtractArtifact::ExtractArtifact(ExtractArtifact&&) noexcept
  = default;
auto SceneRenderer::ExtractArtifact::operator=(ExtractArtifact&&) noexcept
  -> ExtractArtifact& = default;

SceneRenderer::SceneRenderer(Renderer& renderer, Graphics& gfx,
  const SceneTexturesConfig config, const ShadingMode default_shading_mode)
  : renderer_(renderer)
  , gfx_(gfx)
  , scene_textures_(gfx, InitialExposureSceneConfig(renderer, config))
  , scene_texture_pool_(gfx, InitialExposureSceneConfig(renderer, config))
  , scene_color_pool_(
      std::make_unique<internal::RetainedTexturePool>(renderer.GetGraphics()))
  , active_scene_textures_(&scene_textures_)
  , inspected_scene_textures_(&scene_textures_)
  , default_shading_mode_(default_shading_mode)
{
  if (renderer_.HasCapability(
        RendererCapabilityFamily::kFinalOutputComposition)) {
    post_process_ = std::make_unique<PostProcessService>(renderer_);
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)) {
    init_views_ = std::make_unique<InitViewsModule>(
      renderer_, observer_ptr { post_process_.get() });
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)
    && renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)) {
    depth_prepass_ = std::make_unique<DepthPrepassModule>(
      renderer_, scene_textures_.GetConfig());
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)
    && renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)) {
    screen_hzb_ = std::make_unique<ScreenHzbModule>(
      renderer_, scene_textures_.GetConfig());
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)
    && renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)) {
    occlusion_ = std::make_unique<OcclusionModule>(renderer_);
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)
    && renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)) {
    base_pass_ = std::make_unique<BasePassModule>(
      renderer_, scene_textures_.GetConfig());
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)
    && renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)
    && renderer_.HasCapability(RendererCapabilityFamily::kLightingData)) {
    translucency_ = std::make_unique<TranslucencyModule>(renderer_);
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)
    && renderer_.HasCapability(RendererCapabilityFamily::kLightingData)) {
    lighting_ = std::make_unique<LightingService>(renderer_);
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)
    && renderer_.HasCapability(RendererCapabilityFamily::kLightingData)) {
    shadows_ = std::make_unique<ShadowService>(renderer_);
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting)) {
    environment_ = std::make_unique<EnvironmentLightingService>(renderer_);
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)) {
    ground_grid_pass_ = std::make_unique<GroundGridPass>(renderer_);
  }
}

SceneRenderer::~SceneRenderer() { ResetExtractArtifacts(); }

void SceneRenderer::BeginFrame(const frame::SequenceNumber sequence,
  const frame::Slot slot, const std::optional<glm::uvec2> frame_extent)
{
  if (frame_extent.has_value()
    && *frame_extent != scene_textures_.GetExtent()) {
    ResizeSceneTextureFamily(*frame_extent);
  }

  setup_mode_.Reset();
  scene_texture_bindings_.Invalidate();
  ResetExtractArtifacts();
  scene_color_pool_->OnFrameStart(sequence);
  for (auto* artifact : {
         &resolved_scene_color_artifact_,
         &resolved_scene_depth_artifact_,
         &prev_velocity_artifact_,
       }) {
    if (artifact->pool) {
      artifact->pool->OnFrameStart(sequence);
    }
  }
  InvalidatePublishedViewFrameBindings();
  deferred_lighting_state_ = {};
  environment_lighting_state_ = {};
  frame_light_selection_ = {};
  frame_lighting_views_.clear();
  frame_shadow_views_.clear();
  lighting_grid_built_sequence_ = frame::SequenceNumber {};
  if (lighting_ != nullptr) {
    lighting_->OnFrameStart(sequence, slot);
  }
  if (shadows_ != nullptr) {
    shadows_->OnFrameStart(sequence, slot);
  }
  if (environment_ != nullptr) {
    environment_->OnFrameStart(sequence, slot);
  }
  if (post_process_ != nullptr) {
    post_process_->OnFrameStart(sequence, slot);
  }
  if (screen_hzb_ != nullptr) {
    screen_hzb_->OnFrameStart();
  }
}

void SceneRenderer::OnFrameStart(const engine::FrameContext& frame)
{
  BeginFrame(frame.GetFrameSequenceNumber(), frame.GetFrameSlot(),
    ResolveFrameViewportExtent(frame));
}

void SceneRenderer::OnStandaloneFrameStart(const frame::SequenceNumber sequence,
  const frame::Slot slot, const std::optional<glm::uvec2> frame_extent)
{
  BeginFrame(sequence, slot, frame_extent);
}

void SceneRenderer::OnPreRender(const engine::FrameContext& /*frame*/) { }

void SceneRenderer::PrimePreparedViews(RenderContext& ctx)
{
  if (post_process_) {
    post_process_->CaptureRegisteredExposureControls(ctx);
    // Capture every view before callbacks or draws can mutate scene intent.
    if (ctx.frame_views.empty() && ctx.current_view.view_id != kInvalidViewId) {
      std::ignore = ResolveAuthoredPostProcessConfig(ctx, *post_process_);
    }
    for (const auto& view : ctx.frame_views) {
      if (!view.is_scene_view) {
        continue;
      }
      std::ignore
        = ResolveAuthoredPostProcessConfig(ctx, *post_process_, &view);
    }
    for (const auto& view : ctx.frame_views) {
      if (view.is_scene_view
        && view.exposure_view_state_handle
          != CompositionView::kInvalidViewStateHandle
        && view.exposure_view_state_handle != view.view_state_handle) {
        std::ignore = post_process_->CaptureSharedExposureSource(
          ctx, view.exposure_view_id, view.exposure_view_state_handle);
      }
    }
  }
  if (init_views_ != nullptr) {
    init_views_->Execute(ctx, scene_textures_);
  }
}

auto SceneRenderer::DescribeExposureProductLayout(const RenderContext& ctx)
  -> ExposureProductLayout
{
  auto layout = ExposureProductLayout {};
  layout.fp32_only = ctx.current_view.hdr_fp32_only;
  const auto extent = ResolveRenderContextTargetExtent(ctx).value_or(
    ActiveSceneTextures().GetExtent());
  layout.products[0] = {
    11U,
    extent.x,
    extent.y,
    1U,
    environment::ResolveSceneBackground(ctx).has_value() ? 1U : 0U,
    std::bit_cast<std::uint32_t>(1.0F),
  };
  if (environment_) {
    const auto required = environment_->DescribeViewRadianceLayout(ctx);
    std::size_t index = 1U;
    const auto append
      = [&](const std::uint32_t id, const glm::uvec3 size, float gain) -> void {
      if (size.x != 0U) {
        layout.products[index++] = {
          id,
          size.x,
          size.y,
          size.z,
          2U,
          std::bit_cast<std::uint32_t>(gain),
        };
      }
    };
    append(5U, required.sky_view, 1.0F);
    append(6U, required.aerial_perspective, required.aerial_rgb_gain);
    append(10U, required.volumetric_fog, 1.0F);
  }
  return layout;
}

auto SceneRenderer::PrepareExposureDomain(
  RenderContext& ctx, graphics::CommandRecorder& recorder) -> bool
{
  profiling::CpuProfileScope cpu_scope(
    "Vortex.SceneRenderer.PrepareExposureDomain",
    profiling::ProfileCategory::kPass);
  if (!post_process_
    || !ctx.current_view.feature_mask.Has(
      CompositionView::ViewFeatureMask::kSceneLighting)) {
    return true;
  }
  post_process_->SetResolvedConfig(
    ResolveAuthoredPostProcessConfig(ctx, *post_process_));
  const auto control
    = renderer_.GetDiagnosticsService().GetHdrPrecisionControl();
  ctx.current_view.hdr_fp32_only = control == HdrPrecisionControl::kProduction
    || control == HdrPrecisionControl::kFp32Only;
  auto layout = DescribeExposureProductLayout(ctx);
  auto candidate = postprocess::ExposurePass::StateLease {};
  const auto handle = ctx.current_view.view_state_handle;
  if (handle != CompositionView::kInvalidViewStateHandle) {
    auto& retained = exposure_product_layouts_[handle];
    if (retained.revision == 0U || retained.products != layout.products
      || retained.fp32_only != layout.fp32_only) {
      if (retained.revision != 0U && retained.fp32_only != layout.fp32_only
        && environment_) {
        // Radiance history is rebuilt across the P=1 control boundary. Exposure
        // gain and authored transition state remain with PostProcess.
        environment_->RemoveViewState(ctx.current_view.view_id);
      }
      CHECK_NE_F(
        retained.revision, (std::numeric_limits<std::uint64_t>::max)());
      layout.revision = retained.revision + 1U;
      retained = layout;
    }
    std::uint32_t expected = 0U;
    for (const auto& product : retained.products) {
      if (product[0] != 0U) {
        expected |= 1U << (product[0] - 1U);
      }
    }
    candidate = post_process_->SelectPrecisionCandidate(ctx,
      {
        .product_layout_revision = retained.revision,
        .expected_products = expected,
      });
  }
  // Raw harness views without a retained family cannot export conditional HDR.
  if (!active_scene_texture_lease_) {
    candidate.reset();
  }
  const auto fp32_reference = control == HdrPrecisionControl::kFp32Reference;
  ctx.current_view.hdr_color_format = candidate && !fp32_reference
    ? Format::kRGBA16Float
    : Format::kRGBA32Float;
  ctx.current_view.frame_exposure = post_process_->PrepareFrameExposure(
    ctx, recorder, fp32_reference || !candidate, candidate, fp32_reference);
  return ctx.current_view.frame_exposure != nullptr;
}

void SceneRenderer::BindPreparedView(RenderContext& ctx)
{
  ctx.current_view.prepared_frame.reset(nullptr);
  if (init_views_ != nullptr && ctx.current_view.view_id != kInvalidViewId) {
    ctx.current_view.prepared_frame = observer_ptr<const PreparedSceneFrame> {
      init_views_->GetPreparedSceneFrame(ctx.current_view.view_id)
    };
  }
}

void SceneRenderer::PrimePreparedView(RenderContext& ctx)
{
  PrimePreparedViews(ctx);
  BindPreparedView(ctx);
}

void SceneRenderer::RenderViewFamily(RenderContext& ctx)
{
  struct AuxiliaryProduct {
    std::shared_ptr<graphics::Texture> texture;
  };

  PrimePreparedViews(ctx);
  const auto allocations_before_frame
    = scene_texture_pool_.GetAllocationCount();
  auto rendered_scene_view_count = std::size_t { 0U };
  auto auxiliary_products
    = std::unordered_map<CompositionView::AuxOutputId, AuxiliaryProduct> {};
  for (std::size_t view_index = 0U; view_index < ctx.frame_views.size();
    ++view_index) {
    const auto& entry = ctx.frame_views[view_index];
    if (!entry.is_scene_view) {
      continue;
    }

    ctx.frame_views[view_index].rendered = false;
    if (std::ranges::any_of(entry.resolved_aux_inputs,
          [&auxiliary_products](const auto& input) -> auto {
            return input.valid && input.input.required
              && !auxiliary_products.contains(input.input.id);
          })) {
      continue;
    }
    internal::PerViewScope view_scope { ctx, view_index };
    if (post_process_
      && ctx.current_view.feature_mask.Has(
        CompositionView::ViewFeatureMask::kSceneLighting)) {
      ctx.current_view.hdr_color_format = Format::kRGBA32Float;
    }
    const auto lease_key = BuildSceneTextureLeaseKey(ctx);
    auto color_config = scene_textures_.GetConfig();
    color_config.extent = lease_key.extent;
    color_config.scene_color_format = lease_key.scene_color_format;
    color_config.msaa_sample_count = lease_key.msaa_sample_count;
    auto color = scene_color_pool_->Acquire(ctx.current_view.view_id,
      SceneTextures::SceneColorDescriptor(color_config),
      ctx.current_view.view_state_handle
        != CompositionView::kInvalidViewStateHandle);
    auto scene_texture_lease = std::make_shared<SceneTextureLease>(
      scene_texture_pool_.Acquire(lease_key, std::move(color)));
    active_scene_texture_lease_ = scene_texture_lease;
    auto& leased_scene_textures = scene_texture_lease->GetSceneTextures();
    active_scene_textures_ = &leased_scene_textures;
    inspected_scene_textures_ = &leased_scene_textures;
    auto restore_scene_texture_family = ScopeGuard([this] noexcept -> void {
      active_scene_textures_ = &scene_textures_;
      // Attachment reuse waits for its submitted frame, independently of any
      // retained color reader. No callback reaches the pool after destruction.
      active_scene_texture_lease_->Retire(gfx_);
      active_scene_texture_lease_.reset();
    });
    BindPreparedView(ctx);
    ResetPerViewSceneProducts();
    renderer_.DispatchViewExtensionsOnViewSetup(ctx);
    auto recording = gfx_.AcquireCommandRecorder(
      gfx_.QueueKeyFor(graphics::QueueRole::kGraphics), "Vortex View",
      graphics::SubmissionPolicy::kExplicit);
    if (!recording) {
      continue;
    }
    auto& recorder = *recording;
    renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(recorder);
    recorder.OnSubmission(
      [this](const graphics::SubmissionOutcome outcome) -> void {
        if (outcome == graphics::SubmissionOutcome::kDiscarded) {
          ResetPerViewSceneProducts();
        }
      });
    if (!renderer_.PublishCurrentViewPreSceneFrameBindings(
          ctx, recorder, *this)) {
      continue;
    }
    renderer_.DispatchViewExtensionsOnPreRenderViewGpu(ctx, recorder);
    if (!RenderCurrentView(ctx, recorder)) {
      continue;
    }
    renderer_.PublishCurrentViewPostSceneFrameBindings(ctx, *this);
    renderer_.DispatchViewExtensionsOnPostRenderViewGpu(ctx, recorder);
    if (!recording.Submit()) {
      ResetPerViewSceneProducts();
      continue;
    }
    ctx.frame_views[view_index].rendered = true;
    ++rendered_scene_view_count;

    for (const auto& output : entry.produced_aux_outputs) {
      if (output.kind != CompositionView::AuxOutputKind::kColorTexture) {
        continue;
      }
      auto texture
        = ResolveFramebufferColorTexture(ResolveViewOutputTarget(ctx));
      CHECK_F(static_cast<bool>(texture),
        "SceneRenderer: auxiliary output {} from view {} has no color texture",
        output.id.get(), entry.view_id.get());
      auxiliary_products.insert_or_assign(output.id,
        AuxiliaryProduct {
          .texture = texture,
        });
      LOG_F(INFO,
        "Vortex.AuxView.Extract frame={} aux_id={} producer_view={} "
        "texture='{}' debug_name='{}'",
        ctx.frame_sequence.get(), output.id.get(), entry.view_id.get(),
        texture->GetDescriptor().debug_name, output.debug_name);
    }

    for (const auto& input : entry.resolved_aux_inputs) {
      if (!input.valid
        || input.kind != CompositionView::AuxOutputKind::kColorTexture) {
        continue;
      }
      const auto product_it = auxiliary_products.find(input.input.id);
      if (product_it == auxiliary_products.end()) {
        continue;
      }
      auto target
        = ResolveFramebufferColorTexture(ResolveViewOutputTarget(ctx));
      CHECK_F(static_cast<bool>(target),
        "SceneRenderer: auxiliary consumer view {} has no color target",
        entry.view_id.get());
      CHECK_F(product_it->second.texture.get() != target.get(),
        "SceneRenderer: auxiliary consumer view {} cannot copy from its own "
        "target texture",
        entry.view_id.get());

      const auto queue_key = gfx_.QueueKeyFor(graphics::QueueRole::kGraphics);
      auto auxiliary_recording = gfx_.AcquireCommandRecorder(
        queue_key, "Vortex Auxiliary View Consumption");
      CHECK_F(static_cast<bool>(auxiliary_recording),
        "SceneRenderer: failed to acquire auxiliary consumption recorder");
      renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(
        *auxiliary_recording);
      graphics::GpuEventScope consume_scope(*auxiliary_recording,
        "Vortex.AuxView.Consume", profiling::ProfileGranularity::kTelemetry,
        profiling::ProfileCategory::kPass,
        profiling::Vars(profiling::Var("aux_id", input.input.id.get()),
          profiling::Var("producer_view", input.producer_view_id.get()),
          profiling::Var("consumer_view", entry.view_id.get()),
          profiling::Var("debug_name", input.debug_name)));
      auto& source = *product_it->second.texture;
      TrackAuxiliaryColorTexture(gfx_, *auxiliary_recording, source);
      TrackAuxiliaryColorTexture(gfx_, *auxiliary_recording, *target);
      CopyAuxiliaryTextureToRegion(*auxiliary_recording, source, *target,
        BuildAuxiliaryConsumerViewport(*target));
      auxiliary_recording->RequireResourceStateFinal(
        source, graphics::ResourceStates::kRenderTarget);
      auxiliary_recording->RequireResourceStateFinal(
        *target, graphics::ResourceStates::kRenderTarget);
      auxiliary_recording->FlushBarriers();
      LOG_F(INFO,
        "Vortex.AuxView.Consume frame={} aux_id={} producer_view={} "
        "consumer_view={} texture='{}' target='{}'",
        ctx.frame_sequence.get(), input.input.id.get(),
        input.producer_view_id.get(), entry.view_id.get(),
        source.GetDescriptor().debug_name, target->GetDescriptor().debug_name);
    }
  }

  const auto allocations_after_frame = scene_texture_pool_.GetAllocationCount();
  LOG_F(INFO,
    "Vortex.SceneTextureLeasePool.Churn frame={} scene_views={} "
    "allocations_before={} allocations_after={} allocations_delta={} "
    "live_leases={}",
    ctx.frame_sequence.get(), rendered_scene_view_count,
    allocations_before_frame, allocations_after_frame,
    allocations_after_frame - allocations_before_frame,
    scene_texture_pool_.GetLiveLeaseCount());
}

auto SceneRenderer::OnRender(RenderContext& ctx) -> bool
{
  if (!ctx.frame_views.empty() && ctx.current_view.view_id == kInvalidViewId
    && !ctx.per_view_scope_active_) {
    RenderViewFamily(ctx);
    return std::ranges::any_of(
      ctx.frame_views, [](const auto& view) -> auto { return view.rendered; });
  }
  auto recording = gfx_.AcquireCommandRecorder(
    gfx_.QueueKeyFor(graphics::QueueRole::kGraphics), "Vortex View",
    graphics::SubmissionPolicy::kExplicit);
  if (!recording) {
    return false;
  }
  auto& recorder = *recording;
  renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(recorder);
  recorder.OnSubmission(
    [this](const graphics::SubmissionOutcome outcome) -> void {
      if (outcome == graphics::SubmissionOutcome::kDiscarded) {
        ResetPerViewSceneProducts();
      }
    });
  if (post_process_ && !ctx.current_view.frame_exposure
    && !renderer_.PublishCurrentViewPreSceneFrameBindings(
      ctx, recorder, *this)) {
    return false;
  }
  if (!RenderCurrentView(ctx, recorder) || !recording.Submit()) {
    ResetPerViewSceneProducts();
    return false;
  }
  return true;
}

auto SceneRenderer::RenderCurrentView(
  RenderContext& ctx, graphics::CommandRecorder& recorder) -> bool
{
  deferred_lighting_state_ = {};
  auto& scene_textures = ActiveSceneTextures();
  if (!ctx.current_view.frame_exposure) {
    ctx.current_view.hdr_color_format
      = scene_textures.GetConfig().scene_color_format;
  }
  if (const auto target_extent = ResolveRenderContextTargetExtent(ctx);
    target_extent.has_value() && *target_extent != scene_textures.GetExtent()) {
    ResizeSceneTextureFamily(*target_extent);
  }

  const auto shading_mode = ResolveShadingModeForCurrentView(ctx);
  const auto wireframe_only = ctx.render_mode == RenderMode::kWireframe;
  const auto feature_spec
    = ResolveViewFeatureProfileSpec(ctx.current_view.feature_profile);
  const auto feature_mask = ctx.current_view.feature_mask;
  const auto depth_only_variant = feature_spec.depth_only;
  const auto shadow_only_variant = feature_spec.shadow_only;
  const auto diagnostics_only_variant = feature_spec.diagnostics_only;
  const auto wants_scene_lighting
    = feature_mask.Has(CompositionView::ViewFeatureMask::kSceneLighting)
    && !depth_only_variant && !shadow_only_variant && !diagnostics_only_variant;
  const auto wants_shadow_products
    = feature_mask.Has(CompositionView::ViewFeatureMask::kShadows)
    || shadow_only_variant;
  const auto wants_environment = wants_scene_lighting
    && feature_mask.Has(CompositionView::ViewFeatureMask::kEnvironment);
  const auto wants_translucency = wants_scene_lighting
    && feature_mask.Has(CompositionView::ViewFeatureMask::kTranslucency);
  const auto wants_lighting_selection
    = wants_scene_lighting || wants_shadow_products;
  const auto wants_depth_prepass = wants_scene_lighting || depth_only_variant;
  const auto wants_resolve = wants_scene_lighting || depth_only_variant;
  const auto wants_scene_texture_publication
    = wants_scene_lighting || depth_only_variant;
  ctx.current_view.screen_hzb_request = wireframe_only || !wants_scene_lighting
    ? RenderContext::ScreenHzbRequest {}
    : ResolveScreenHzbRequest(ctx, shading_mode);
  if (depth_only_variant) {
    ctx.current_view.depth_prepass_mode = DepthPrePassMode::kOpaqueAndMasked;
  }
  if (wireframe_only || !wants_depth_prepass) {
    ctx.current_view.depth_prepass_mode = DepthPrePassMode::kDisabled;
  }
  ctx.current_view.scene_depth_product_valid = false;
  // Renderer Core materializes the eligible views and selects the current
  // scene-view cursor in RenderContext. SceneRenderer owns the stage chain for
  // that selected current view only.

  // Select initializer lists, not temporary vectors, in diagnostic aggregates.
  // MSVC 19.51 leaks vector proxies for conditional member initializers.
  // Constructing each member directly from the selected list avoids that
  // lifetime bug and the extra vector move. See the EX07A heap-leak validation
  // note.

  // Stage 2: InitViews
  if (ctx.current_view.prepared_frame == nullptr) {
    PrimePreparedView(ctx);
  }
  if (post_process_ && wants_scene_lighting) {
    std::ignore = ResolveAuthoredPostProcessConfig(ctx, *post_process_);
  }
  ctx.current_view.history_discontinuity
    = renderer_.CapturedViewDiscontinuities(
        ctx.current_view.view_state_handle, ctx.frame_sequence)
    != 0U;
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage2.InitViews",
      .kind = DiagnosticsPassKind::kCpuOnly,
      .executed = ctx.current_view.prepared_frame != nullptr,
      .outputs = { "Vortex.PreparedSceneFrame" },
      .missing_inputs = ctx.current_view.prepared_frame == nullptr
        ? std::initializer_list<std::string> { "PreparedSceneFrame" }
        : std::initializer_list<std::string> {},
    });
  if (diagnostics_only_variant) {
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.FeatureVariant.DiagnosticsOnly",
        .kind = DiagnosticsPassKind::kCpuOnly,
        .executed = true,
        .outputs = { "Vortex.DiagnosticsLedger" },
      });
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.DiagnosticsLedger",
        .producer_pass = "Vortex.FeatureVariant.DiagnosticsOnly",
        .descriptor = "frame-ledger",
        .published = true,
        .valid = true,
      });
  }

  if ((lighting_ != nullptr || shadows_ != nullptr) && wants_lighting_selection
    && lighting_grid_built_sequence_ != ctx.frame_sequence) {
    if (auto* scene_mutable = ctx.GetSceneMutable().get();
      scene_mutable != nullptr) {
      scene_mutable->Update(false);
      auto& resolver = scene_mutable->GetDirectionalLightResolver();
      resolver.Validate();
      frame_light_selection_ = BuildFrameLightSelection(
        *scene_mutable, resolver, ctx.frame_sequence.get());
    } else {
      frame_light_selection_ = FrameLightSelection {
        .selection_epoch = ctx.frame_sequence.get(),
      };
    }
    frame_lighting_views_.clear();
    if (lighting_ != nullptr && wants_scene_lighting) {
      CollectLightingViewInputs(ctx, init_views_.get(), frame_lighting_views_);
      const auto preparation = lighting_->BuildLightGrid(FrameLightingInputs {
        .frame_light_set = &frame_light_selection_,
        .active_views = std::span(frame_lighting_views_),
      });
      if (!preparation) {
        LOG_F(ERROR,
          "Lighting preparation failed: reason={} family={} index={} view={}",
          static_cast<unsigned>(preparation.error().error),
          static_cast<unsigned>(preparation.error().family),
          preparation.error().selection_index.get(),
          preparation.error().view_id.get());
        return false;
      }
    }
    lighting_grid_built_sequence_ = ctx.frame_sequence;
  }
  if (lighting_ != nullptr && wants_scene_lighting) {
    published_view_frame_bindings_.lighting_frame_slot
      = lighting_->ResolveLightingFrameSlot(ctx.current_view.view_id);
    const auto* lighting_bindings
      = lighting_->InspectForwardLightBindings(ctx.current_view.view_id);
    if (lighting_bindings == nullptr) {
      return false;
    }
    published_view_frame_bindings_.lighting_view_generation
      = lighting_bindings->view_generation;
    deferred_lighting_state_.published_lighting_frame_slot
      = published_view_frame_bindings_.lighting_frame_slot;
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage6.ForwardLightData",
        .kind = DiagnosticsPassKind::kCpuOnly,
        .executed = published_view_frame_bindings_.lighting_frame_slot
          != kInvalidShaderVisibleIndex,
        .inputs = { "FrameLightSelection" },
        .outputs = { "Vortex.LightingFrameBindings" },
      });
    RecordDiagnosticsViewProduct(renderer_, "Vortex.LightingFrameBindings",
      "Vortex.Stage6.ForwardLightData",
      published_view_frame_bindings_.lighting_frame_slot);
  }

  // Stage 3: Depth prepass + early velocity
  if (depth_prepass_ != nullptr && wants_depth_prepass) {
    depth_prepass_->SetConfig(DepthPrepassConfig {
      .mode = ctx.current_view.depth_prepass_mode,
      .write_velocity = scene_textures.GetVelocity() != nullptr,
    });
    depth_prepass_->Execute(ctx, recorder, scene_textures);
    ctx.current_view.depth_prepass_completeness
      = depth_prepass_->GetCompleteness();
    ctx.current_view.scene_depth_product_valid
      = depth_prepass_->HasValidDepthProduct();
  } else {
    ctx.current_view.depth_prepass_completeness
      = DepthPrePassCompleteness::kDisabled;
    ctx.current_view.scene_depth_product_valid = false;
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage3.DepthPrepass",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = depth_prepass_ != nullptr
        && ctx.current_view.depth_prepass_completeness
          != DepthPrePassCompleteness::kDisabled,
      .outputs = wants_depth_prepass
        ? std::initializer_list<std::string> { "Vortex.SceneDepth",
            "Vortex.PartialDepth", }
        : std::initializer_list<std::string> {},
    });
  if (ctx.current_view.depth_prepass_completeness
    == DepthPrePassCompleteness::kComplete) {
    PublishDepthPrepassProducts();
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.SceneDepth",
        .producer_pass = "Vortex.Stage3.DepthPrepass",
        .resource_name
        = std::string { scene_textures.GetSceneDepth().GetName() },
        .descriptor
        = SceneTextureDescriptor(scene_texture_bindings_.scene_depth_srv),
        .published = scene_texture_bindings_.scene_depth_srv
          != SceneTextureBindings::kInvalidIndex,
        .valid = ctx.current_view.scene_depth_product_valid,
      });
  }

  // Stage 4: reserved - GeometryVirtualizationService

  // Stage 5: Occlusion / HZB
  published_screen_hzb_bindings_ = {};
  ctx.current_view.screen_hzb_closest_texture.reset(nullptr);
  ctx.current_view.screen_hzb_furthest_texture.reset(nullptr);
  ctx.current_view.screen_hzb_previous_furthest_texture.reset(nullptr);
  ctx.current_view.screen_hzb_frame_slot = kInvalidShaderVisibleIndex;
  ctx.current_view.screen_hzb_previous_furthest_srv
    = kInvalidShaderVisibleIndex;
  ctx.current_view.screen_hzb_width = 0U;
  ctx.current_view.screen_hzb_height = 0U;
  ctx.current_view.screen_hzb_mip_count = 0U;
  ctx.current_view.screen_hzb_available = false;
  ctx.current_view.screen_hzb_has_previous = false;
  ctx.current_view.occlusion_results.reset(nullptr);
  if (screen_hzb_ != nullptr && ctx.current_view.CanBuildScreenHzb()) {
    screen_hzb_->Execute(ctx, recorder, scene_textures);
    const auto& screen_hzb_output = screen_hzb_->GetCurrentOutput();
    published_screen_hzb_bindings_ = screen_hzb_output.bindings;
    if (screen_hzb_output.closest_texture != nullptr) {
      ctx.current_view.screen_hzb_closest_texture
        = observer_ptr<const graphics::Texture> {
            screen_hzb_output.closest_texture.get()
          };
    }
    if (screen_hzb_output.furthest_texture != nullptr) {
      ctx.current_view.screen_hzb_furthest_texture
        = observer_ptr<const graphics::Texture> {
            screen_hzb_output.furthest_texture.get()
          };
    }
    ctx.current_view.screen_hzb_width
      = static_cast<std::uint32_t>(screen_hzb_output.bindings.hzb_size_x);
    ctx.current_view.screen_hzb_height
      = static_cast<std::uint32_t>(screen_hzb_output.bindings.hzb_size_y);
    ctx.current_view.screen_hzb_mip_count
      = screen_hzb_output.bindings.mip_count;
    ctx.current_view.screen_hzb_available = screen_hzb_output.available;
    if (ctx.current_view.screen_hzb_request.WantsPreviousFurthest()) {
      const auto& screen_hzb_previous = screen_hzb_->GetPreviousOutput();
      if (screen_hzb_previous.available
        && screen_hzb_previous.furthest_texture != nullptr) {
        ctx.current_view.screen_hzb_previous_furthest_texture
          = observer_ptr<const graphics::Texture> {
              screen_hzb_previous.furthest_texture.get()
            };
        ctx.current_view.screen_hzb_previous_furthest_srv
          = screen_hzb_previous.bindings.furthest_srv;
        ctx.current_view.screen_hzb_has_previous = true;
      }
    }
    if (screen_hzb_output.available) {
      PublishScreenHzbProducts(ctx);
    }
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage5.ScreenHzbBuild",
      .kind = DiagnosticsPassKind::kCompute,
      .executed = ctx.current_view.screen_hzb_available,
      .inputs = { "Vortex.SceneDepth" },
      .outputs = ctx.current_view.CanBuildScreenHzb()
        ? std::initializer_list<std::string> { "Vortex.ScreenHzb" }
        : std::initializer_list<std::string> {},
      .missing_inputs = ctx.current_view.CanBuildScreenHzb()
          && !ctx.current_view.scene_depth_product_valid
        ? std::initializer_list<std::string> { "Vortex.SceneDepth" }
        : std::initializer_list<std::string> {},
    });
  RecordDiagnosticsViewProduct(renderer_, "Vortex.ScreenHzb",
    "Vortex.Stage5.ScreenHzbBuild",
    published_view_frame_bindings_.screen_hzb_frame_slot);

  if (occlusion_ != nullptr && wants_scene_lighting) {
    occlusion_->SetConfig(OcclusionConfig {
      .enabled = renderer_.GetOcclusionEnabled(),
      .max_candidate_count = renderer_.GetOcclusionMaxCandidateCount(),
    });
    occlusion_->Execute(ctx, recorder, scene_textures);
    const auto& occlusion_stats = occlusion_->GetStats();
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage5.Occlusion",
        .kind = DiagnosticsPassKind::kCpuOnly,
        .executed = occlusion_stats.results_valid,
        .inputs = { "PreparedSceneFrame", "Vortex.ScreenHzb" },
        .outputs = { "Vortex.OcclusionFrameResults" },
      });
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.OcclusionFrameResults",
        .producer_pass = "Vortex.Stage5.Occlusion",
        .descriptor = OcclusionStatsDescriptor(occlusion_stats),
        .published = ctx.current_view.occlusion_results.get() != nullptr,
        .valid = occlusion_stats.results_valid,
      });
  }

  // Stage 6: Forward light data / light grid

  // Stage 7: reserved - MaterialCompositionService::PreBasePass

  // Stage 8: Shadow depth
  if (shadows_ != nullptr && wants_shadow_products) {
    CollectCurrentShadowViewInput(ctx, init_views_.get(), frame_shadow_views_);
    shadows_->RenderShadowDepths(FrameShadowInputs {
      .frame_light_set = &frame_light_selection_,
      .active_views = std::span(frame_shadow_views_),
    });
  }
  if (shadows_ != nullptr && wants_shadow_products) {
    published_view_frame_bindings_.shadow_frame_slot
      = shadows_->ResolveShadowFrameSlot(ctx.current_view.view_id);
    deferred_lighting_state_.published_shadow_frame_slot
      = published_view_frame_bindings_.shadow_frame_slot;
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage8.ShadowDepth",
        .kind = DiagnosticsPassKind::kGraphics,
        .executed = published_view_frame_bindings_.shadow_frame_slot
          != kInvalidShaderVisibleIndex,
        .inputs = { "FrameShadowInputs" },
        .outputs = { "Vortex.ShadowFrameBindings" },
      });
    RecordDiagnosticsViewProduct(renderer_, "Vortex.ShadowFrameBindings",
      "Vortex.Stage8.ShadowDepth",
      published_view_frame_bindings_.shadow_frame_slot);
  }
  if (environment_ != nullptr && wants_environment) {
    const auto enable_static_sky_light_ambient_bridge
      = shading_mode == ShadingMode::kDeferred;
    published_view_frame_bindings_.environment_frame_slot
      = environment_->PublishEnvironmentBindings(ctx, recorder,
        kInvalidShaderVisibleIndex, kInvalidShaderVisibleIndex,
        enable_static_sky_light_ambient_bridge, &scene_textures);
    environment_lighting_state_.published_environment_frame_slot
      = published_view_frame_bindings_.environment_frame_slot;
    environment_lighting_state_.owned_by_environment_service = true;
    const auto* environment_bindings
      = environment_->InspectBindings(ctx.current_view.view_id);
    if (environment_bindings != nullptr) {
      environment_lighting_state_.published_bindings
        = environment_lighting_state_.published_environment_frame_slot
        != kInvalidShaderVisibleIndex;
      environment_lighting_state_.ambient_bridge_published
        = environment_bindings->ambient_bridge.flags != 0U;
      environment_lighting_state_.ambient_bridge_irradiance_srv
        = environment_bindings->ambient_bridge.irradiance_map_srv;
      environment_lighting_state_.probe_revision
        = environment_bindings->probes.probe_revision;
    }
    RecordDiagnosticsViewProduct(renderer_, "Vortex.EnvironmentFrameBindings",
      "Vortex.Environment.PublishBindings",
      published_view_frame_bindings_.environment_frame_slot);
  }
  if ((shadows_ != nullptr && wants_shadow_products)
    || (environment_ != nullptr && wants_environment)
    || (lighting_ != nullptr && wants_scene_lighting)) {
    renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
  }

  // Stage 9: Base pass
  auto base_pass_published = false;
  auto base_pass_wrote_scene_color = false;
  auto base_pass_draw_count = std::uint32_t { 0U };
  auto base_pass_occlusion_culled_draw_count = std::uint32_t { 0U };
  if (base_pass_ != nullptr && wants_scene_lighting) {
    base_pass_->SetConfig(BasePassConfig {
      .write_velocity = scene_textures.GetVelocity() != nullptr,
      .early_z_pass_done = ctx.current_view.IsEarlyDepthComplete(),
      .shading_mode = shading_mode,
      .render_mode = ctx.render_mode,
    });
    const auto base_pass_result
      = base_pass_->Execute(ctx, recorder, scene_textures);
    base_pass_published = base_pass_result.published_base_pass_products;
    base_pass_wrote_scene_color = base_pass_result.wrote_scene_color;
    base_pass_draw_count = base_pass_result.draw_count;
    base_pass_occlusion_culled_draw_count
      = base_pass_result.occlusion_culled_draw_count;
    if (base_pass_result.wrote_scene_color && !wireframe_only) {
      ctx.current_view.scene_depth_product_valid = true;
    }
    if (base_pass_result.published_base_pass_products
      && base_pass_result.completed_velocity_for_dynamic_geometry) {
      PublishBasePassVelocity();
    }
    if (base_pass_result.published_base_pass_products) {
      PublishDeferredBasePassSceneTextures(ctx);
      renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
    } else if (base_pass_result.wrote_scene_color
      && shading_mode == ShadingMode::kForward && !wireframe_only) {
      // Forward base shading writes these attachments without producing
      // GBuffers. Publish them for environment consumers and the HDR resolve.
      setup_mode_.SetFlags(SceneTextureSetupMode::Flag::kSceneColor
        | SceneTextureSetupMode::Flag::kSceneDepth);
      RefreshSceneTextureBindings();
      renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
    }
  }
  auto base_pass_outputs = std::vector<std::string> {};
  if (base_pass_published) {
    base_pass_outputs = { "Vortex.SceneColor", "Vortex.GBuffer" };
  } else if (wants_scene_lighting) {
    base_pass_outputs = { "Vortex.SceneColor" };
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage9.BasePass",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = base_pass_wrote_scene_color,
      .inputs = ctx.current_view.occlusion_results.get() != nullptr
        ? std::initializer_list<std::string> { "Vortex.PreparedSceneFrame",
            "Vortex.OcclusionFrameResults", }
        : std::initializer_list<std::string> { "Vortex.PreparedSceneFrame" },
      .outputs = std::move(base_pass_outputs),
    });
  if (base_pass_wrote_scene_color) {
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.BasePassDrawCommands",
        .producer_pass = "Vortex.Stage9.BasePass",
        .descriptor = BasePassDrawDescriptor(
          base_pass_draw_count, base_pass_occlusion_culled_draw_count),
        .published = true,
        .valid = true,
      });
  }
  if (base_pass_published) {
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.SceneColor",
        .producer_pass = "Vortex.Stage9.BasePass",
        .resource_name
        = std::string { scene_textures.GetSceneColor().GetName() },
        .descriptor
        = SceneTextureDescriptor(scene_texture_bindings_.scene_color_srv),
        .published = scene_texture_bindings_.scene_color_srv
          != SceneTextureBindings::kInvalidIndex,
        .valid = scene_texture_bindings_.scene_color_srv
          != SceneTextureBindings::kInvalidIndex,
      });
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.GBuffer",
        .producer_pass = "Vortex.Stage9.BasePass",
        .descriptor
        = SceneTextureDescriptor(scene_texture_bindings_.gbuffer_srvs[0]),
        .published = HasPublishedGBufferBindings(scene_texture_bindings_),
        .valid = HasPublishedGBufferBindings(scene_texture_bindings_),
      });
  }

  // Stage 11: reserved - MaterialCompositionService::PostBasePass

  // Stage 12: Deferred direct lighting
  const auto rendered_debug_visualization = wants_scene_lighting
    ? RenderDebugVisualization(ctx, recorder, scene_textures)
    : false;
  if (!rendered_debug_visualization) {
    if (wants_scene_lighting) {
      RenderDeferredLighting(ctx, recorder, scene_textures);
    }
  }
  const auto deferred_lighting_executed = rendered_debug_visualization
    || deferred_lighting_state_.consumed_published_scene_textures;
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = rendered_debug_visualization ? "Vortex.Stage12.DebugVisualization"
                                           : "Vortex.Stage12.DeferredLighting",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = deferred_lighting_executed,
      .inputs = { "Vortex.SceneColor", "Vortex.GBuffer",
        "Vortex.LightingFrameBindings", "Vortex.ShadowFrameBindings", },
      .outputs = wants_scene_lighting
        ? std::initializer_list<std::string> { "Vortex.SceneColor" }
        : std::initializer_list<std::string> {},
    });

  // Stage 13: reserved - IndirectLightingService

  // Stage 14: reserved - EnvironmentLightingService volumetrics

  // Stage 15: Sky / atmosphere / fog
  if (post_process_ && ctx.current_view.frame_exposure
    && base_pass_wrote_scene_color && !wireframe_only
    && !rendered_debug_visualization) {
    auto& source = scene_textures.GetSceneColor();
    const auto source_srv = ShaderVisibleIndex { RegisterSceneTextureView(
      source, MakeSrvDesc(source, source.GetDescriptor().format)) };
    std::ignore = post_process_->CapturePreEnvironmentRange(
      ctx, recorder, source, source_srv);
  }
  if (environment_ != nullptr && wants_environment && !wireframe_only
    && !IsNonIblDebugMode(ctx.shader_debug_mode)) {
    environment_->RenderSkyAndFog(ctx, recorder, scene_textures);
    const auto& stage14_state = environment_->GetLastStage14State();
    environment_lighting_state_.stage14_requested = stage14_state.requested;
    environment_lighting_state_.stage14_local_fog_requested
      = stage14_state.local_fog_requested;
    environment_lighting_state_.stage14_local_fog_executed
      = stage14_state.local_fog_executed;
    environment_lighting_state_.stage14_local_fog_hzb_consumed
      = stage14_state.local_fog_hzb_consumed;
    environment_lighting_state_.stage14_local_fog_hzb_unavailable
      = stage14_state.local_fog_hzb_unavailable;
    environment_lighting_state_.stage14_local_fog_buffer_ready
      = stage14_state.local_fog_buffer_ready;
    environment_lighting_state_.stage14_local_fog_skipped
      = stage14_state.local_fog_skipped;
    environment_lighting_state_.stage14_local_fog_instance_count
      = stage14_state.local_fog_instance_count;
    environment_lighting_state_.stage14_local_fog_dispatch_count_x
      = stage14_state.local_fog_dispatch_count_x;
    environment_lighting_state_.stage14_local_fog_dispatch_count_y
      = stage14_state.local_fog_dispatch_count_y;
    environment_lighting_state_.stage14_local_fog_dispatch_count_z
      = stage14_state.local_fog_dispatch_count_z;
    environment_lighting_state_.stage14_volumetric_fog_requested
      = stage14_state.volumetric_fog_requested;
    environment_lighting_state_.stage14_volumetric_fog_executed
      = stage14_state.volumetric_fog_executed;
    environment_lighting_state_.stage14_integrated_light_scattering_valid
      = stage14_state.integrated_light_scattering_valid;
    environment_lighting_state_.stage14_integrated_light_scattering_srv
      = stage14_state.integrated_light_scattering_srv;
    environment_lighting_state_.stage14_volumetric_fog_grid_width
      = stage14_state.volumetric_fog_grid_width;
    environment_lighting_state_.stage14_volumetric_fog_grid_height
      = stage14_state.volumetric_fog_grid_height;
    environment_lighting_state_.stage14_volumetric_fog_grid_depth
      = stage14_state.volumetric_fog_grid_depth;
    environment_lighting_state_.stage14_volumetric_fog_dispatch_count_x
      = stage14_state.volumetric_fog_dispatch_count_x;
    environment_lighting_state_.stage14_volumetric_fog_dispatch_count_y
      = stage14_state.volumetric_fog_dispatch_count_y;
    environment_lighting_state_.stage14_volumetric_fog_dispatch_count_z
      = stage14_state.volumetric_fog_dispatch_count_z;
    environment_lighting_state_
      .stage14_volumetric_fog_height_fog_media_requested
      = stage14_state.volumetric_fog_height_fog_media_requested;
    environment_lighting_state_.stage14_volumetric_fog_height_fog_media_executed
      = stage14_state.volumetric_fog_height_fog_media_executed;
    environment_lighting_state_
      .stage14_volumetric_fog_sky_light_injection_requested
      = stage14_state.volumetric_fog_sky_light_injection_requested;
    environment_lighting_state_
      .stage14_volumetric_fog_sky_light_injection_executed
      = stage14_state.volumetric_fog_sky_light_injection_executed;
    environment_lighting_state_
      .stage14_volumetric_fog_temporal_history_requested
      = stage14_state.volumetric_fog_temporal_history_requested;
    environment_lighting_state_
      .stage14_volumetric_fog_temporal_history_reprojection_executed
      = stage14_state.volumetric_fog_temporal_history_reprojection_executed;
    environment_lighting_state_.stage14_volumetric_fog_temporal_history_reset
      = stage14_state.volumetric_fog_temporal_history_reset;
    environment_lighting_state_
      .stage14_volumetric_fog_local_fog_injection_requested
      = stage14_state.volumetric_fog_local_fog_injection_requested;
    environment_lighting_state_
      .stage14_volumetric_fog_local_fog_injection_executed
      = stage14_state.volumetric_fog_local_fog_injection_executed;
    environment_lighting_state_.stage14_volumetric_fog_local_fog_instance_count
      = stage14_state.volumetric_fog_local_fog_instance_count;
    const auto& stage15_state = environment_->GetLastStage15State();
    environment_lighting_state_.owned_by_environment_service = true;
    environment_lighting_state_.stage15_requested = stage15_state.requested;
    environment_lighting_state_.sky_requested = stage15_state.sky_requested;
    environment_lighting_state_.sky_executed = stage15_state.sky_executed;
    environment_lighting_state_.sky_draw_count = stage15_state.sky_draw_count;
    environment_lighting_state_.atmosphere_requested
      = stage15_state.atmosphere_requested;
    environment_lighting_state_.atmosphere_executed
      = stage15_state.atmosphere_executed;
    environment_lighting_state_.atmosphere_draw_count
      = stage15_state.atmosphere_draw_count;
    environment_lighting_state_.fog_requested = stage15_state.fog_requested;
    environment_lighting_state_.fog_executed = stage15_state.fog_executed;
    environment_lighting_state_.fog_draw_count = stage15_state.fog_draw_count;
    environment_lighting_state_.total_draw_count
      = stage15_state.total_draw_count;
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage14.VolumetricAndLocalFog",
        .kind = DiagnosticsPassKind::kCompute,
        .executed = stage14_state.local_fog_executed
          || stage14_state.volumetric_fog_executed,
        .inputs = { "Vortex.ScreenHzb", "Vortex.EnvironmentFrameBindings",
          "Vortex.ShadowFrameBindings", },
        .outputs = { "Vortex.Environment.IntegratedLightScattering" },
      });
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.Environment.IntegratedLightScattering",
        .producer_pass = "Vortex.Stage14.VolumetricAndLocalFog",
        .descriptor = ShaderVisibleDescriptor(
          stage14_state.integrated_light_scattering_srv),
        .published = stage14_state.integrated_light_scattering_srv
          != kInvalidShaderVisibleIndex,
        .valid = stage14_state.integrated_light_scattering_valid,
      });
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage15.SkyAtmosphereFog",
        .kind = DiagnosticsPassKind::kGraphics,
        .executed = stage15_state.total_draw_count > 0U,
        .inputs = { "Vortex.SceneColor",
          "Vortex.Environment.IntegratedLightScattering", },
        .outputs = { "Vortex.SceneColor" },
      });
  }

  // Stage 16: reserved - WaterService

  // Stage 17: reserved - post-opaque extensions

  // Stage 18: Translucency
  auto translucency_result = TranslucencyExecutionResult {};
  if (translucency_ != nullptr && wants_translucency && !wireframe_only
    && !IsNonIblDebugMode(ctx.shader_debug_mode)) {
    translucency_result = translucency_->Execute(ctx, recorder, scene_textures);
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage18.Translucency",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = translucency_result.executed,
      .inputs = { "Vortex.PreparedSceneFrame", "Vortex.SceneColor",
        "Vortex.SceneDepth", "Vortex.LightingFrameBindings",
        "Vortex.ShadowFrameBindings", "Vortex.EnvironmentFrameBindings", },
      .outputs = wants_translucency
        ? std::initializer_list<std::string> { "Vortex.SceneColor" }
        : std::initializer_list<std::string> {},
      .missing_inputs = TranslucencyMissingInputs(
        translucency_result, translucency_ != nullptr),
    });
  RecordDiagnosticsProduct(renderer_,
    DiagnosticsProductRecord {
      .name = "Vortex.TranslucencyDrawCommands",
      .producer_pass = "Vortex.Stage18.Translucency",
      .descriptor = TranslucencyDrawDescriptor(translucency_result),
      .published = translucency_result.draw_count > 0U,
      .valid = !translucency_result.requested || translucency_result.executed
        || translucency_result.skip_reason == TranslucencySkipReason::kNoDraws,
    });

  // Stage 19: reserved - DistortionModule

  const auto draw_wireframe_overlay
    = [&](const graphics::Framebuffer* target) -> void {
    if (base_pass_ == nullptr || !wants_scene_lighting || wireframe_only
      || ctx.render_mode != RenderMode::kOverlayWireframe) {
      return;
    }
    const auto overlay_draws = base_pass_->ExecuteWireframeOverlay(
      ctx, recorder, scene_textures, target);
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage20.WireframeOverlay",
        .kind = DiagnosticsPassKind::kGraphics,
        .executed = overlay_draws > 0U,
        .inputs = { "Vortex.SceneColor", "Vortex.SceneDepth",
          "Vortex.PreparedSceneFrame", },
        .outputs = { "Vortex.SceneColor" },
      });
  };

  if (!post_process_) {
    draw_wireframe_overlay(nullptr);
  }

  // Maintain late scene-texture publication before the output handoff stages.
  if (wants_scene_texture_publication) {
    PublishCustomDepthProducts();
  }

  // Meter the FP32 accumulation before any resolved-color narrowing. Stage 22
  // consumes this exact result instead of metering the resolved texture again.
  auto prepared_exposure
    = std::optional<PostProcessService::PreparedExposure> {};
  auto precision_products
    = std::vector<postprocess::ExposurePass::HdrProduct> {};
  if (post_process_ != nullptr && wants_scene_lighting) {
    post_process_->SetResolvedConfig(
      ResolveAuthoredPostProcessConfig(ctx, *post_process_));
    auto* accumulated = scene_textures.GetSceneColorResource().get();
    CHECK_NOTNULL_F(accumulated);
    const auto accumulated_srv
      = ShaderVisibleIndex { RegisterSceneTextureView(*accumulated,
        MakeSrvDesc(*accumulated, accumulated->GetDescriptor().format)) };
    if (!ctx.current_view.hdr_fp32_only) {
      precision_products = CollectExposureProducts(
        ctx, *accumulated, accumulated_srv, environment_.get());
    }
    auto layout = ExposureProductLayout {};
    CHECK_LE_F(precision_products.size(), layout.products.size());
    auto expected_products = std::uint32_t { 0U };
    for (std::size_t i = 0; i < precision_products.size(); ++i) {
      const auto& product = precision_products[i];
      const auto desc = product.texture ? product.texture->GetDescriptor()
                                        : graphics::TextureDesc {};
      layout.products[i] = {
        product.id,
        desc.width,
        desc.height,
        desc.depth,
        (product.coverage ? 1U : 0U) | (product.transmittance ? 2U : 0U),
        std::bit_cast<std::uint32_t>(product.consumer_rgb_gain),
      };
      expected_products |= 1U << (product.id - 1U);
    }
    const auto handle = ctx.current_view.view_state_handle;
    if (!ctx.current_view.hdr_fp32_only
      && handle != CompositionView::kInvalidViewStateHandle) {
      auto& retained = exposure_product_layouts_[handle];
      if (retained.revision == 0U || retained.products != layout.products) {
        CHECK_NE_F(
          retained.revision, (std::numeric_limits<std::uint64_t>::max)());
        layout.revision = retained.revision + 1U;
        retained = layout;
      }
      layout = retained;
      // A producer configuration changed or failed after pre-scene selection.
      // Invalidate admission without changing the already-pinned frame domain.
      std::ignore = post_process_->SelectPrecisionCandidate(ctx,
        {
          .product_layout_revision = layout.revision,
          .expected_products = expected_products,
        });
    }
    prepared_exposure = post_process_->PrepareSceneExposure(
      ctx.current_view.view_id, ctx, recorder,
      {
        .scene_signal = accumulated,
        .scene_signal_srv = accumulated_srv,
        .require_scene_range = true,
      });
    if (!prepared_exposure) {
      return false;
    }
  }

  // Certify current/candidate consumer error before checked narrowing.
  if (prepared_exposure && !precision_products.empty()) {
    auto* depth = scene_textures.GetSceneDepthResource().get();
    const auto depth_srv = ShaderVisibleIndex { RegisterSceneTextureView(*depth,
      MakeSrvDesc(
        *depth, ResolveDepthSrvFormat(depth->GetDescriptor().format))) };
    const auto local_instances
      = environment_ && environment_->GetLastStage15State().local_fog_executed
      ? environment_->GetLastStage14State().local_fog_instance_count
      : 0U;
    std::ignore = post_process_->PrepareScenePrecision(ctx, recorder,
      *prepared_exposure, precision_products,
      postprocess::ExposurePass::SceneComposition {
        .opaque_depth = depth,
        .opaque_depth_srv = depth_srv,
        .translucent_triangles = translucency_result.triangle_count,
        .local_fog_instances = local_instances,
        .reverse_z = IsReverseZ(ctx),
      });
  }

  // Stage 21: Resolve scene color
  if (wants_resolve) {
    ResolveSceneColor(
      ctx, recorder, prepared_exposure ? &*prepared_exposure : nullptr);
  }
  if (prepared_exposure && !precision_products.empty()) {
    std::ignore = post_process_->FinalizeScenePrecision(
      ctx, recorder, *prepared_exposure);
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage21.ResolveSceneColor",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = scene_texture_extracts_.resolved_scene_color.valid,
      .inputs = { "Vortex.SceneColor" },
      .outputs = wants_resolve
        ? std::initializer_list<std::string> { "Vortex.ResolvedSceneColor" }
        : std::initializer_list<std::string> {},
      .missing_inputs
      = wants_resolve && !scene_texture_extracts_.resolved_scene_color.valid
        ? std::initializer_list<std::string> { "Vortex.SceneColor" }
        : std::initializer_list<std::string> {},
    });
  if (scene_texture_extracts_.resolved_scene_color.valid
    && scene_texture_extracts_.resolved_scene_color.texture != nullptr) {
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.ResolvedSceneColor",
        .producer_pass = "Vortex.Stage21.ResolveSceneColor",
        .resource_name = std::string {
          scene_texture_extracts_.resolved_scene_color.texture->GetName(),
        },
        .published = true,
        .valid = true,
      });
  }

  // Stage 22: Post processing
  if (post_process_ != nullptr && wants_scene_lighting) {
    const auto post_target = ResolveViewOutputTarget(ctx);

    const auto* scene_signal = scene_textures.GetSceneColorResource().get();
    auto scene_signal_kind = std::string_view { "scene_color" };
    if (scene_texture_extracts_.resolved_scene_color.valid
      && scene_texture_extracts_.resolved_scene_color.texture != nullptr) {
      scene_signal = scene_texture_extracts_.resolved_scene_color.texture;
      scene_signal_kind = "resolved_scene_color";
    }
    CHECK_NOTNULL_F(scene_signal,
      "SceneRenderer Stage 22 requires a valid SceneColor source texture");
    const auto scene_signal_srv = ShaderVisibleIndex { RegisterSceneTextureView(
      *const_cast<graphics::Texture*>(scene_signal),
      MakeSrvDesc(*scene_signal, scene_signal->GetDescriptor().format)) };

    const auto* scene_depth = scene_textures.GetSceneDepthResource().get();
    if (scene_texture_extracts_.resolved_scene_depth.valid
      && scene_texture_extracts_.resolved_scene_depth.texture != nullptr) {
      scene_depth = scene_texture_extracts_.resolved_scene_depth.texture;
    }
    CHECK_NOTNULL_F(scene_depth,
      "SceneRenderer Stage 22 requires a valid SceneDepth source texture");
    const auto scene_depth_srv = ShaderVisibleIndex { RegisterSceneTextureView(
      *const_cast<graphics::Texture*>(scene_depth),
      MakeSrvDesc(*scene_depth, scene_depth->GetDescriptor().format)) };
    CHECK_NOTNULL_F(post_target.get(),
      "SceneRenderer Stage 22 requires a SceneRenderer-supplied post target");

    auto post_process_inputs = PostProcessService::Inputs {
      .scene_signal = scene_signal,
      .scene_depth = scene_depth,
      .scene_velocity = ResolveVelocitySourceTexture(),
      .post_target = post_target,
      .scene_signal_srv = scene_signal_srv,
      .scene_depth_srv = scene_depth_srv,
      .scene_velocity_srv
      = ShaderVisibleIndex { scene_texture_bindings_.velocity_srv },
      .scene_fallback = scene_texture_extracts_.resolved_scene_color.fallback,
      .scene_fallback_srv
      = scene_texture_extracts_.resolved_scene_color.fallback
        ? ShaderVisibleIndex { scene_texture_bindings_.scene_color_srv }
        : kInvalidShaderVisibleIndex,
      .checked_resolution
      = scene_texture_extracts_.resolved_scene_color.fallback
        ? ctx.current_view.frame_exposure
        : nullptr,
    };
    if (!post_process_->Record(ctx.current_view.view_id, ctx, recorder,
          post_process_inputs, &*prepared_exposure)) {
      return false;
    }
    published_view_frame_bindings_.post_process_frame_slot
      = post_process_->ResolveBindingSlot(ctx.current_view.view_id);
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage22.PostProcess",
        .kind = DiagnosticsPassKind::kGraphics,
        .executed = published_view_frame_bindings_.post_process_frame_slot
          != kInvalidShaderVisibleIndex,
        .inputs = { std::string { "Vortex." } + std::string(scene_signal_kind),
          "Vortex.SceneDepth", },
        .outputs = { "Vortex.PostProcessFrameBindings" },
      });
    RecordDiagnosticsViewProduct(renderer_, "Vortex.PostProcessFrameBindings",
      "Vortex.Stage22.PostProcess",
      published_view_frame_bindings_.post_process_frame_slot);
    renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
  }

  if (post_process_) {
    draw_wireframe_overlay(ResolveViewOutputTarget(ctx).get());
  }

  // Stage 20: Ground grid
  if (ground_grid_pass_ != nullptr && wants_scene_lighting && !wireframe_only) {
    std::ignore = ground_grid_pass_->Record(
      ctx, recorder, scene_textures, ResolveViewOutputTarget(ctx));
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage20.GroundGrid",
        .kind = DiagnosticsPassKind::kGraphics,
        .executed = true,
        .inputs = { "Vortex.SceneColor" },
        .outputs = { "Vortex.SceneColor" },
      });
  }

  // Stage 23: Post-render cleanup / extraction
  PostRenderCleanup(ctx, recorder);
  return true;
}

void SceneRenderer::OnCompositing(RenderContext& /*ctx*/)
{
  // Phase 2 explicitly preserves the seam while Renderer retains composition
  // planning, queueing, target resolution, and presentation ownership.
}

void SceneRenderer::OnFrameEnd(const engine::FrameContext& /*frame*/) { }

void SceneRenderer::PreserveRemovedExposureSource(
  std::shared_ptr<const ExposureSourceLoss> loss)
{
  if (post_process_) {
    post_process_->PreserveRemovedExposureSource(std::move(loss));
  }
}

void SceneRenderer::RemoveViewState(const ViewId view_id,
  const CompositionView::ViewStateHandle view_state_handle)
{
  InvalidatePublishedViewFrameBindings();
  exposure_product_layouts_.erase(view_state_handle);
  scene_color_pool_->RemoveView(view_id);
  for (auto* artifact : {
         &resolved_scene_color_artifact_,
         &resolved_scene_depth_artifact_,
         &prev_velocity_artifact_,
       }) {
    if (artifact->pool) {
      artifact->pool->RemoveView(view_id);
    }
  }
  if (environment_) {
    environment_->RemoveViewState(view_id);
  }
  if (screen_hzb_) {
    screen_hzb_->RemoveViewState(view_id);
  }
  if (post_process_ != nullptr) {
    post_process_->RemoveViewState(view_id, view_state_handle);
  }
}

void SceneRenderer::PublishDepthPrepassProducts()
{
  auto& scene_textures = ActiveSceneTextures();
  auto flags = SceneTextureSetupMode::Flag::kSceneDepth
    | SceneTextureSetupMode::Flag::kPartialDepth;
  if (scene_textures.GetVelocity() != nullptr) {
    flags = flags | SceneTextureSetupMode::Flag::kSceneVelocity;
  }
  setup_mode_.SetFlags(flags);
  RefreshSceneTextureBindings();
}

void SceneRenderer::PublishScreenHzbProducts(RenderContext& ctx)
{
  CHECK_F(ctx.current_view.view_id != kInvalidViewId,
    "SceneRenderer: PublishScreenHzbProducts requires a valid current view");
  CHECK_F(ctx.frame_slot != frame::kInvalidSlot,
    "SceneRenderer: PublishScreenHzbProducts requires a valid frame slot");
  renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
  ctx.current_view.screen_hzb_frame_slot
    = published_view_frame_bindings_.screen_hzb_frame_slot;
}

void SceneRenderer::PublishBasePassVelocity()
{
  auto& scene_textures = ActiveSceneTextures();
  // Stage 9 owns the raw attachment writes, but Stage 10 remains the first
  // truthful publication boundary for SceneColor and the active GBuffers in
  // the standard SceneTextureBindings route.
  if (scene_textures.GetVelocity() != nullptr) {
    setup_mode_.Set(SceneTextureSetupMode::Flag::kSceneVelocity);
    RefreshSceneTextureBindings();
  }
  CHECK_F(scene_texture_bindings_.scene_color_srv
        == SceneTextureBindings::kInvalidIndex
      && scene_texture_bindings_.scene_color_uav
        == SceneTextureBindings::kInvalidIndex
      && !HasAnyPublishedGBufferBinding(scene_texture_bindings_),
    "SceneRenderer: Stage 9 must not publish SceneColor or GBuffer bindings "
    "before the Stage 10 rebuild boundary");
}

void SceneRenderer::PublishDeferredBasePassSceneTextures(RenderContext& ctx)
{
  auto& scene_textures = ActiveSceneTextures();
  // SceneRenderer is the sole owner of the deferred base-pass scene-texture
  // publication seam. RebuildWithGBuffers() is only the family-local
  // readiness helper; this method performs promotion, binding refresh, and
  // current-view routing republish.
  CHECK_F(ctx.current_view.view_id != kInvalidViewId,
    "SceneRenderer: PublishDeferredBasePassSceneTextures requires a valid "
    "current view");
  CHECK_F(ctx.current_view.resolved_view != nullptr,
    "SceneRenderer: PublishDeferredBasePassSceneTextures requires a resolved "
    "current view");
  CHECK_F(ctx.frame_slot != frame::kInvalidSlot,
    "SceneRenderer: PublishDeferredBasePassSceneTextures requires a valid "
    "frame slot for publication");
  scene_textures.RebuildWithGBuffers();
  setup_mode_.SetFlags(SceneTextureSetupMode::Flag::kGBuffers
    | SceneTextureSetupMode::Flag::kSceneColor
    | SceneTextureSetupMode::Flag::kSceneDepth
    | SceneTextureSetupMode::Flag::kStencil);
  RefreshSceneTextureBindings();
  renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
  CHECK_F(HasPublishedGBufferBindings(scene_texture_bindings_),
    "SceneRenderer: Stage 10 must publish GBuffer bindings before deferred "
    "lighting or GBuffer debug inspection");
}

void SceneRenderer::PublishCustomDepthProducts()
{
  auto& scene_textures = ActiveSceneTextures();
  if (scene_textures.GetCustomDepth() != nullptr) {
    setup_mode_.Set(SceneTextureSetupMode::Flag::kCustomDepth);
  }
  RefreshSceneTextureBindings();
}

void SceneRenderer::FinalizeSceneTextureExtractions()
{
  // Stage 23 is an extraction boundary only; scene-texture bindless
  // availability remains defined by the prior setup milestones.
}

auto SceneRenderer::GetSceneTextures() const -> const SceneTextures&
{
  return inspected_scene_textures_ != nullptr ? *inspected_scene_textures_
                                              : scene_textures_;
}

auto SceneRenderer::GetSceneTextures() -> SceneTextures&
{
  return inspected_scene_textures_ != nullptr ? *inspected_scene_textures_
                                              : scene_textures_;
}

auto SceneRenderer::GetSceneTextureBindings() const
  -> const SceneTextureBindings&
{
  return scene_texture_bindings_;
}

auto SceneRenderer::GetSceneTextureExtracts() const
  -> const SceneTextureExtracts&
{
  return scene_texture_extracts_;
}

auto SceneRenderer::GetResolvedSceneColorTexture() const
  -> std::shared_ptr<graphics::Texture>
{
  const auto& color = scene_texture_extracts_.resolved_scene_color;
  if (color.fallback && color.source_color) {
    return std::shared_ptr<graphics::Texture>(
      std::make_shared<SceneTextureExtractRef>(color), color.fallback);
  }
  return resolved_scene_color_artifact_.texture;
}

auto SceneRenderer::GetDefaultShadingMode() const -> ShadingMode
{
  return default_shading_mode_;
}

auto SceneRenderer::GetEffectiveShadingMode(const RenderContext& ctx) const
  -> ShadingMode
{
  return ResolveShadingModeForCurrentView(ctx);
}

auto SceneRenderer::GetAuthoredStageOrder() -> const StageOrder&
{
  return kAuthoredStageOrder;
}

auto SceneRenderer::GetPublishedViewFrameBindings() const
  -> const ViewFrameBindings&
{
  return published_view_frame_bindings_;
}

auto SceneRenderer::GetPublishedScreenHzbBindings() const
  -> const ScreenHzbFrameBindings&
{
  return published_screen_hzb_bindings_;
}

auto SceneRenderer::GetPublishedViewFrameBindingsSlot() const
  -> ShaderVisibleIndex
{
  return published_view_frame_bindings_slot_;
}

auto SceneRenderer::GetPublishedViewId() const -> ViewId
{
  return published_view_id_;
}

auto SceneRenderer::GetLastDeferredLightingState() const
  -> const DeferredLightingState&
{
  return deferred_lighting_state_;
}

auto SceneRenderer::InspectExposureSettings(
  const CompositionView::ViewStateHandle handle) const
  -> std::optional<ExposureSettingsStatus>
{
  return post_process_ ? post_process_->InspectExposureSettings(handle)
                       : std::nullopt;
}

auto SceneRenderer::GetLastEnvironmentLightingState() const
  -> const EnvironmentLightingState&
{
  return environment_lighting_state_;
}

void SceneRenderer::PublishViewFrameBindings(const ViewId view_id,
  const ViewFrameBindings& bindings, const ShaderVisibleIndex slot)
{
  published_view_id_ = view_id;
  published_view_frame_bindings_ = bindings;
  published_view_frame_bindings_slot_ = slot;
}

void SceneRenderer::InvalidatePublishedViewFrameBindings()
{
  published_view_id_ = kInvalidViewId;
  published_view_frame_bindings_ = {};
  published_screen_hzb_bindings_ = {};
  published_view_frame_bindings_slot_ = kInvalidShaderVisibleIndex;
}

void SceneRenderer::RefreshSceneTextureBindings()
{
  scene_texture_bindings_.Invalidate();
  if (setup_mode_.GetFlags() == 0U) {
    return;
  }

  auto& scene_textures = ActiveSceneTextures();
  scene_texture_bindings_.valid_flags = setup_mode_.GetFlags();

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneDepth)) {
    scene_texture_bindings_.scene_depth_srv
      = RegisterSceneTextureView(scene_textures.GetSceneDepth(),
        MakeSrvDesc(scene_textures.GetSceneDepth(),
          ResolveDepthSrvFormat(
            scene_textures.GetSceneDepth().GetDescriptor().format)));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kPartialDepth)) {
    scene_texture_bindings_.partial_depth_srv
      = RegisterSceneTextureView(scene_textures.GetPartialDepth(),
        MakeSrvDesc(scene_textures.GetPartialDepth(),
          scene_textures.GetPartialDepth().GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneVelocity)
    && scene_textures.GetVelocity() != nullptr) {
    scene_texture_bindings_.velocity_srv
      = RegisterSceneTextureView(*scene_textures.GetVelocity(),
        MakeSrvDesc(*scene_textures.GetVelocity(),
          scene_textures.GetVelocity()->GetDescriptor().format));
    scene_texture_bindings_.velocity_uav
      = RegisterSceneTextureView(*scene_textures.GetVelocity(),
        MakeUavDesc(*scene_textures.GetVelocity(),
          scene_textures.GetVelocity()->GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneColor)) {
    scene_texture_bindings_.scene_color_srv
      = RegisterSceneTextureView(scene_textures.GetSceneColor(),
        MakeSrvDesc(scene_textures.GetSceneColor(),
          scene_textures.GetSceneColor().GetDescriptor().format));
    scene_texture_bindings_.scene_color_uav
      = RegisterSceneTextureView(scene_textures.GetSceneColor(),
        MakeUavDesc(scene_textures.GetSceneColor(),
          scene_textures.GetSceneColor().GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kStencil)) {
    const auto stencil_view = scene_textures.GetStencil();
    if (stencil_view.IsValid()) {
      scene_texture_bindings_.stencil_srv
        = RegisterSceneTextureView(*stencil_view.texture,
          MakeSrvDesc(*stencil_view.texture,
            ResolveStencilSrvFormat(
              stencil_view.texture->GetDescriptor().format)));
    }
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kCustomDepth)
    && scene_textures.GetCustomDepth() != nullptr) {
    scene_texture_bindings_.custom_depth_srv
      = RegisterSceneTextureView(*scene_textures.GetCustomDepth(),
        MakeSrvDesc(*scene_textures.GetCustomDepth(),
          ResolveDepthSrvFormat(
            scene_textures.GetCustomDepth()->GetDescriptor().format)));

    const auto custom_stencil = scene_textures.GetCustomStencil();
    if (custom_stencil.IsValid()) {
      scene_texture_bindings_.custom_stencil_srv
        = RegisterSceneTextureView(*custom_stencil.texture,
          MakeSrvDesc(*custom_stencil.texture,
            ResolveStencilSrvFormat(
              custom_stencil.texture->GetDescriptor().format)));
    }
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kGBuffers)) {
    for (std::uint32_t i = 0; i < scene_textures.GetGBufferCount(); ++i) {
      const auto gbuffer_index = static_cast<GBufferIndex>(i);
      auto& texture = scene_textures.GetGBuffer(gbuffer_index);
      scene_texture_bindings_.gbuffer_srvs[i] = RegisterSceneTextureView(
        texture, MakeSrvDesc(texture, texture.GetDescriptor().format));
    }
  }
}

void SceneRenderer::ResizeSceneTextureFamily(const glm::uvec2 new_extent)
{
  auto& scene_textures = ActiveSceneTextures();
  if (new_extent == scene_textures.GetExtent()) {
    return;
  }

  LOG_F(INFO, "SceneRenderer resizing scene textures from {}x{} to {}x{}",
    scene_textures.GetExtent().x, scene_textures.GetExtent().y, new_extent.x,
    new_extent.y);
  scene_textures.Resize(new_extent);
  if (&scene_textures == &scene_textures_) {
    inspected_scene_textures_ = &scene_textures_;
  }
  setup_mode_.Reset();
  scene_texture_bindings_.Invalidate();
  ResetExtractArtifacts();
  for (auto* artifact : {
         &resolved_scene_color_artifact_,
         &resolved_scene_depth_artifact_,
         &prev_velocity_artifact_,
       }) {
    if (artifact->pool) {
      artifact->pool->Clear();
    }
  }
}

auto SceneRenderer::ActiveSceneTextures() -> SceneTextures&
{
  return active_scene_textures_ != nullptr ? *active_scene_textures_
                                           : scene_textures_;
}

auto SceneRenderer::ActiveSceneTextures() const -> const SceneTextures&
{
  return active_scene_textures_ != nullptr ? *active_scene_textures_
                                           : scene_textures_;
}

auto SceneRenderer::BuildSceneTextureLeaseKey(const RenderContext& ctx) const
  -> SceneTextureLeaseKey
{
  auto key = SceneTextureLeaseKey::FromConfig(scene_textures_.GetConfig());
  if (post_process_) {
    key.scene_color_format = Format::kRGBA32Float;
  } else if (ctx.current_view.hdr_color_format) {
    key.scene_color_format = *ctx.current_view.hdr_color_format;
  }
  if (const auto target_extent = ResolveRenderContextTargetExtent(ctx);
    target_extent.has_value()) {
    key.extent = *target_extent;
  }
  return key;
}

void SceneRenderer::ResetPerViewSceneProducts()
{
  setup_mode_.Reset();
  scene_texture_bindings_.Invalidate();
  published_screen_hzb_bindings_ = {};
  InvalidatePublishedViewFrameBindings();
  ResetExtractArtifacts();
}

void SceneRenderer::ResetExtractArtifacts()
{
  scene_texture_extracts_.Reset();
  RetireExtractTexture(gfx_, resolved_scene_color_artifact_.texture);
  RetireExtractTexture(gfx_, resolved_scene_depth_artifact_.texture);
  RetireExtractTexture(gfx_, prev_velocity_artifact_.texture);
}

auto SceneRenderer::EnsureArtifactTexture(RenderContext& ctx,
  ExtractArtifact& artifact, std::string_view debug_name,
  const graphics::Texture& source, const std::optional<Format> format)
  -> graphics::Texture*
{
  const auto& source_desc = source.GetDescriptor();
  const auto requires_reallocation = [&] -> bool {
    if (artifact.texture == nullptr) {
      return true;
    }
    const auto& current_desc = artifact.texture->GetDescriptor();
    return current_desc.width != source_desc.width
      || current_desc.height != source_desc.height
      || current_desc.format != format.value_or(source_desc.format)
      || current_desc.sample_count != source_desc.sample_count;
  }();

  if (requires_reallocation) {
    auto artifact_desc = source_desc;
    artifact_desc.debug_name = std::string(debug_name);
    artifact_desc.is_render_target = false;
    artifact_desc.format = format.value_or(source_desc.format);
    artifact_desc.is_uav = format.has_value();
    artifact_desc.use_clear_value = false;
    artifact_desc.clear_value = {};
    artifact_desc.initial_state = graphics::ResourceStates::kCommon;
    RetireExtractTexture(gfx_, artifact.texture);
    if (!artifact.pool) {
      artifact.pool = std::make_unique<internal::RetainedTexturePool>(
        renderer_.GetGraphics());
    }
    artifact.pool->OnFrameStart(ctx.frame_sequence);
    const bool recyclable = ctx.current_view.view_state_handle
      != CompositionView::kInvalidViewStateHandle;
    artifact.texture = artifact.pool->Acquire(
      ctx.current_view.view_id, artifact_desc, recyclable);
  }

  return artifact.texture.get();
}

auto SceneRenderer::ResolveVelocitySourceTexture() const
  -> const graphics::Texture*
{
  return ActiveSceneTextures().GetVelocity();
}

auto SceneRenderer::RegisterSceneTextureView(graphics::Texture& texture,
  const graphics::TextureViewDescription& desc) -> std::uint32_t
{
  auto& registry = gfx_.GetResourceRegistry();
  if (const auto existing_index
    = registry.FindShaderVisibleIndex(texture, desc);
    existing_index.has_value()) {
    return existing_index->get();
  }

  auto& allocator = gfx_.GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
  CHECK_F(handle.IsValid(),
    "SceneRenderer: failed to allocate a {} descriptor for '{}'",
    graphics::to_string(desc.view_type), texture.GetName());

  const auto view = registry.RegisterView(texture, std::move(handle), desc);
  CHECK_F(view->IsValid(),
    "SceneRenderer: failed to register a {} descriptor for '{}'",
    graphics::to_string(desc.view_type), texture.GetName());

  const auto index = registry.FindShaderVisibleIndex(texture, desc);
  CHECK_F(index.has_value(),
    "SceneRenderer: {} descriptor registration for '{}' did not yield a "
    "shader-visible index",
    graphics::to_string(desc.view_type), texture.GetName());
  return index->get();
}

auto SceneRenderer::ResolveShadingModeForCurrentView(
  const RenderContext& ctx) const -> ShadingMode
{
  if (const auto* view = ctx.GetCurrentCompositionView();
    view != nullptr && view->GetShadingMode().has_value()) {
    return view->GetShadingMode().value();
  }
  if (ctx.current_view.shading_mode_override.has_value()) {
    return ctx.current_view.shading_mode_override.value();
  }
  return default_shading_mode_;
}

auto SceneRenderer::RenderDebugVisualization(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& scene_textures)
  -> bool
{
  const auto mode = ctx.shader_debug_mode;
  if (!IsDeferredDebugVisualizationMode(mode)) {
    return false;
  }
  if (ResolveShadingModeForCurrentView(ctx) != ShadingMode::kDeferred) {
    return false;
  }
  if (ctx.view_constants == nullptr) {
    return false;
  }
  if (published_view_id_ == kInvalidViewId
    || published_view_id_ != ctx.current_view.view_id) {
    return false;
  }
  if (published_view_frame_bindings_slot_ == kInvalidShaderVisibleIndex) {
    return false;
  }

  const auto requires_gbuffer = mode == ShaderDebugMode::kBaseColor
    || mode == ShaderDebugMode::kWorldNormals
    || mode == ShaderDebugMode::kRoughness
    || mode == ShaderDebugMode::kMetalness
    || mode == ShaderDebugMode::kDirectionalShadowMask
    || mode == ShaderDebugMode::kMaskedAlphaCoverage;
  const auto requires_scene_depth = mode == ShaderDebugMode::kSceneDepthRaw
    || mode == ShaderDebugMode::kSceneDepthLinear
    || mode == ShaderDebugMode::kDirectionalShadowMask;

  if (requires_gbuffer
    && !HasPublishedGBufferBindings(scene_texture_bindings_)) {
    return false;
  }
  if (requires_scene_depth
    && scene_texture_bindings_.scene_depth_srv
      == SceneTextureBindings::kInvalidIndex) {
    return false;
  }

  if (NeedsDebugVisualizationFramebufferRebuild(
        debug_visualization_framebuffer_, scene_textures)) {
    debug_visualization_framebuffer_ = gfx_.CreateFramebuffer(
      BuildDebugVisualizationFramebuffer(scene_textures));
  }

  graphics::GpuEventScope debug_scope(recorder,
    fmt::format(
      "Vortex.DebugVisualization.{}", GetDeferredDebugVisualizationName(mode)),
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);

  RequireKnownPersistentState(recorder, scene_textures.GetSceneColor());
  if (requires_scene_depth) {
    RequireKnownPersistentState(recorder, scene_textures.GetSceneDepth());
  }
  if (requires_gbuffer) {
    RequireKnownPersistentState(recorder, scene_textures.GetGBufferNormal());
    RequireKnownPersistentState(recorder, scene_textures.GetGBufferMaterial());
    RequireKnownPersistentState(recorder, scene_textures.GetGBufferBaseColor());
    RequireKnownPersistentState(
      recorder, scene_textures.GetGBufferCustomData());
  }

  recorder.RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  if (requires_scene_depth) {
    recorder.RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  }
  if (requires_gbuffer) {
    recorder.RequireResourceState(scene_textures.GetGBufferNormal(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(scene_textures.GetGBufferMaterial(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(scene_textures.GetGBufferBaseColor(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(scene_textures.GetGBufferCustomData(),
      graphics::ResourceStates::kShaderResource);
  }
  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*debug_visualization_framebuffer_);
  SetViewportAndScissor(recorder, ctx, scene_textures);
  recorder.SetPipelineState(
    BuildDebugVisualizationPipelineDesc(scene_textures, mode));
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants->GetGPUVirtualAddress());
  recorder.Draw(3U, 1U, 0U, 0U);

  recorder.RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  if (requires_scene_depth) {
    recorder.RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  }
  if (requires_gbuffer) {
    recorder.RequireResourceState(scene_textures.GetGBufferNormal(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(scene_textures.GetGBufferMaterial(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(scene_textures.GetGBufferBaseColor(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(scene_textures.GetGBufferCustomData(),
      graphics::ResourceStates::kShaderResource);
  }

  return true;
}

void SceneRenderer::RenderDeferredLighting(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& scene_textures)
{
  deferred_lighting_state_ = {};
  deferred_lighting_state_.published_view_id = published_view_id_;
  deferred_lighting_state_.published_view_frame_bindings_slot
    = published_view_frame_bindings_slot_;
  deferred_lighting_state_.published_scene_texture_frame_slot
    = published_view_frame_bindings_.scene_texture_frame_slot;
  deferred_lighting_state_.published_lighting_frame_slot
    = published_view_frame_bindings_.lighting_frame_slot;
  deferred_lighting_state_.published_shadow_frame_slot
    = published_view_frame_bindings_.shadow_frame_slot;

  if (!renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)
    || !renderer_.HasCapability(RendererCapabilityFamily::kLightingData)) {
    return;
  }
  if (ResolveShadingModeForCurrentView(ctx) != ShadingMode::kDeferred) {
    return;
  }
  if (ctx.view_constants == nullptr) {
    return;
  }
  if (published_view_id_ == kInvalidViewId
    || published_view_id_ != ctx.current_view.view_id) {
    return;
  }
  if (published_view_frame_bindings_slot_ == kInvalidShaderVisibleIndex) {
    return;
  }
  if (!HasPublishedDeferredLightingInputs(scene_texture_bindings_)) {
    return;
  }

  deferred_lighting_state_.consumed_published_scene_textures = true;
  deferred_lighting_state_.consumed_scene_depth_srv
    = scene_texture_bindings_.scene_depth_srv;
  deferred_lighting_state_.consumed_scene_color_uav
    = scene_texture_bindings_.scene_color_uav;
  std::copy_n(scene_texture_bindings_.gbuffer_srvs.begin(),
    deferred_lighting_state_.consumed_gbuffer_srvs.size(),
    deferred_lighting_state_.consumed_gbuffer_srvs.begin());

  if (lighting_ == nullptr) {
    return;
  }
  const auto* shadow_bindings = shadows_ != nullptr
    ? shadows_->InspectShadowData(ctx.current_view.view_id)
    : nullptr;
  const auto* shadow_surface = shadows_ != nullptr
    ? shadows_->InspectShadowSurface(ctx.current_view.view_id)
    : nullptr;
  const auto* spot_shadow_surface = shadows_ != nullptr
    ? shadows_->InspectSpotShadowSurface(ctx.current_view.view_id)
    : nullptr;
  const auto* point_shadow_surface = shadows_ != nullptr
    ? shadows_->InspectPointShadowSurface(ctx.current_view.view_id)
    : nullptr;
  lighting_->RenderDeferredLighting(ctx, recorder, scene_textures,
    frame_light_selection_,
    shadow_bindings != nullptr ? &shadow_bindings->bindings : nullptr,
    shadow_surface, spot_shadow_surface, point_shadow_surface,
    environment_lighting_state_.ambient_bridge_published);
  const auto& lighting_state = lighting_->GetLastDeferredLightingState();
  deferred_lighting_state_.owned_by_lighting_service = true;
  deferred_lighting_state_.used_service_owned_local_light_geometry
    = lighting_state.used_service_owned_geometry;
  deferred_lighting_state_.directional_light_count
    = lighting_state.directional_draw_count;
  deferred_lighting_state_.point_light_count = lighting_state.point_light_count;
  deferred_lighting_state_.spot_light_count = lighting_state.spot_light_count;
  deferred_lighting_state_.local_light_count = lighting_state.local_light_count;
  deferred_lighting_state_.outside_volume_local_light_count
    = lighting_state.outside_volume_local_light_count;
  deferred_lighting_state_.camera_inside_local_light_count
    = lighting_state.camera_inside_local_light_count;
  deferred_lighting_state_.direct_local_light_pass_count
    = lighting_state.local_light_draw_count;
  deferred_lighting_state_.non_perspective_local_light_count
    = lighting_state.non_perspective_local_light_count;
  deferred_lighting_state_.used_outside_volume_local_lights
    = lighting_state.used_outside_volume_local_lights;
  deferred_lighting_state_.used_camera_inside_local_lights
    = lighting_state.used_camera_inside_local_lights;
  deferred_lighting_state_.used_non_perspective_local_lights
    = lighting_state.used_non_perspective_local_lights;
  deferred_lighting_state_.consumed_static_sky_light_product
    = lighting_state.consumed_static_sky_light_product;
  deferred_lighting_state_.accumulated_into_scene_color
    = lighting_state.accumulated_into_scene_color;
  deferred_lighting_state_.static_sky_light_draw_count
    = lighting_state.static_sky_light_draw_count;
  deferred_lighting_state_.consumed_directional_shadow_product
    = lighting_state.consumed_directional_shadow_product;
  deferred_lighting_state_.directional_shadow_vsm_active
    = lighting_state.directional_shadow_vsm_active;
  deferred_lighting_state_.directional_shadow_cascade_count
    = lighting_state.directional_shadow_cascade_count;
  deferred_lighting_state_.directional_shadow_surface_srv
    = lighting_state.directional_shadow_surface_srv;
  deferred_lighting_state_.consumed_spot_shadow_product
    = lighting_state.consumed_spot_shadow_product;
  deferred_lighting_state_.spot_shadow_count = lighting_state.spot_shadow_count;
  deferred_lighting_state_.spot_shadow_surface_srv
    = lighting_state.spot_shadow_surface_srv;
  deferred_lighting_state_.consumed_point_shadow_product
    = lighting_state.consumed_point_shadow_product;
  deferred_lighting_state_.point_shadow_count
    = lighting_state.point_shadow_count;
  deferred_lighting_state_.point_shadow_surface_srv
    = lighting_state.point_shadow_surface_srv;
}

} // namespace oxygen::vortex
