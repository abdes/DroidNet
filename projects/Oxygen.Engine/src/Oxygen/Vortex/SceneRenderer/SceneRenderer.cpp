//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Types/Traversal.h>
#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLightTranslation.h>
#include <Oxygen/Vortex/Internal/PerViewScope.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Passes/GroundGridPass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Translucency/TranslucencyModule.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Types/PassMask.h>

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

  constexpr std::uint32_t kDeferredLightPointSlices = 16U;
  constexpr std::uint32_t kDeferredLightPointStacks = 8U;
  constexpr std::uint32_t kDeferredLightPointVertexCount
    = 6U * kDeferredLightPointSlices * (kDeferredLightPointStacks - 1U);
  constexpr std::uint32_t kDeferredLightSpotSlices = 24U;
  constexpr std::uint32_t kDeferredLightSpotVertexCount
    = 6U * kDeferredLightSpotSlices;
  constexpr std::uint32_t kDeferredLightConstantsStride
    = packing::kConstantBufferAlignment;

  enum class DeferredLightKind : std::uint32_t {
    kDirectional = 0U,
    kPoint = 1U,
    kSpot = 2U,
  };

  enum class DeferredLocalLightDrawMode : std::uint8_t {
    kOutsideVolume = 0U,
    kCameraInsideVolume = 1U,
    kNonPerspective = 2U,
  };

  struct alignas(packing::kShaderDataFieldAlignment) DeferredLightConstants {
    glm::vec4 light_position_and_radius { 0.0F };
    glm::vec4 light_color_and_intensity { 0.0F };
    glm::vec4 light_direction_and_falloff { 0.0F };
    glm::vec4 spot_angles { 0.0F };
    glm::mat4 light_world_matrix { 1.0F };
    std::uint32_t light_type { 0U };
    std::uint32_t _padding0 { 0U };
    std::uint32_t _padding1 { 0U };
    std::uint32_t _padding2 { 0U };
  };
  static_assert(
    sizeof(DeferredLightConstants) % packing::kShaderDataFieldAlignment == 0U);

  struct DeferredLightDraw {
    DeferredLightConstants constants {};
    DeferredLightKind kind { DeferredLightKind::kDirectional };
    DeferredLocalLightDrawMode draw_mode {
      DeferredLocalLightDrawMode::kOutsideVolume
    };
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
    CHECK_F(recorder.AdoptKnownResourceState(texture),
      "SceneRenderer: missing authoritative incoming state for '{}'",
      texture.GetName());
  }

  auto RegisterBufferViewIndex(Graphics& gfx, graphics::Buffer& buffer,
    const graphics::BufferViewDescription& desc) -> ShaderVisibleIndex
  {
    auto& registry = gfx.GetResourceRegistry();
    CHECK_F(registry.Contains(buffer),
      "SceneRenderer: deferred-light buffer '{}' must be registered before "
      "view lookup",
      buffer.GetName());
    if (const auto existing = registry.FindShaderVisibleIndex(buffer, desc);
      existing.has_value()) {
      return *existing;
    }

    auto& allocator = gfx.GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
    CHECK_F(handle.IsValid(),
      "SceneRenderer: failed to allocate deferred-light {} view for '{}'",
      graphics::to_string(desc.view_type), buffer.GetName());
    const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(buffer, std::move(handle), desc);
    CHECK_F(view->IsValid(),
      "SceneRenderer: failed to register deferred-light {} view for '{}'",
      graphics::to_string(desc.view_type), buffer.GetName());
    return shader_visible_index;
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

  auto ResolveLateOverlayTarget(const RenderContext& ctx)
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
    auto selection = FrameLightSelection { .selection_epoch = selection_epoch };
    const auto environment = scene_ref.GetEnvironment();
    const auto atmosphere = environment != nullptr
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
        .transmittance_toward_sun_rgb
        = primary_atmosphere_light.transmittance_toward_sun_rgb,
        .diffuse_scale = 1.0F,
        .specular_scale = 1.0F,
        .atmosphere_light_slot = 0U,
        .atmosphere_mode_flags = atmosphere_mode_flags,
        .shadow_flags = primary.Light().Common().casts_shadows
          ? kDirectionalLightShadowFlagCastsShadows
          : 0U,
        .light_function_atlas_index = 0xFFFFFFFFU,
        .cascade_count
        = primary.Light().Common().casts_shadows ? csm.cascade_count : 0U,
        .light_flags = kDirectionalLightFlagAffectsWorld
          | (primary.Light().GetEnvironmentContribution()
              ? kDirectionalLightFlagEnvContribution
              : 0U)
          | (primary.Light().IsSunLight() ? kDirectionalLightFlagSunLight : 0U)
          | (primary.Light().GetUsePerPixelAtmosphereTransmittance()
              ? kDirectionalLightFlagPerPixelAtmosphereTransmittance
              : 0U),
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
      static_cast<void>(dry_run);

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
          .range = (std::max)(light.GetRange(), 0.001F),
          .color = light.Common().color_rgb,
          .intensity = light.GetLuminousFluxLm(),
          .direction = ComputeDirectionWs(scene_ref, node),
          .decay_exponent = light.GetDecayExponent(),
          .inner_cone_cos = 1.0F,
          .outer_cone_cos = 0.0F,
          .source_radius = light.GetSourceRadius(),
          .flags = light.Common().casts_shadows ? kLocalLightFlagCastsShadows
                                                : 0U,
          .shadow_bias = light.Common().shadow.bias,
          .shadow_normal_bias = light.Common().shadow.normal_bias,
          .shadow_resolution_hint = static_cast<std::uint32_t>(
            light.Common().shadow.resolution_hint),
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
          .range = (std::max)(light.GetRange(), 0.001F),
          .color = light.Common().color_rgb,
          .intensity = light.GetLuminousFluxLm(),
          .direction = ComputeDirectionWs(scene_ref, node),
          .decay_exponent = light.GetDecayExponent(),
          .inner_cone_cos = std::cos(light.GetInnerConeAngleRadians()),
          .outer_cone_cos = std::cos(light.GetOuterConeAngleRadians()),
          .source_radius = light.GetSourceRadius(),
          .flags = light.Common().casts_shadows ? kLocalLightFlagCastsShadows
                                                : 0U,
          .shadow_bias = light.Common().shadow.bias,
          .shadow_normal_bias = light.Common().shadow.normal_bias,
          .shadow_resolution_hint = static_cast<std::uint32_t>(
            light.Common().shadow.resolution_hint),
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

  auto CollectShadowViewInputs(const RenderContext& ctx,
    const InitViewsModule* init_views,
    std::vector<PreparedViewShadowInput>& out) -> void
  {
    out.clear();
    if (init_views == nullptr) {
      return;
    }

    for (const auto& view : ctx.frame_views) {
      if (!view.is_scene_view) {
        continue;
      }
      out.push_back(PreparedViewShadowInput {
        .view_id = view.view_id,
        .prepared_scene = observer_ptr<const PreparedSceneFrame> {
          init_views->GetPreparedSceneFrame(view.view_id),
        },
        .resolved_view = view.resolved_view,
        .view_constants = view.view_id == ctx.current_view.view_id
          ? observer_ptr<const graphics::Buffer> { ctx.view_constants.get() }
          : observer_ptr<const graphics::Buffer> {},
        .composition_view = view.composition_view,
      });
    }

    if (out.empty() && ctx.current_view.view_id != kInvalidViewId) {
      out.push_back(PreparedViewShadowInput {
        .view_id = ctx.current_view.view_id,
        .prepared_scene = ctx.current_view.prepared_frame,
        .resolved_view = ctx.current_view.resolved_view,
        .view_constants = observer_ptr<const graphics::Buffer> {
          ctx.view_constants.get(),
        },
        .composition_view = ctx.current_view.composition_view,
      });
    }
  }

  auto MakeScaleMatrix(const glm::vec3 scale) -> glm::mat4
  {
    return glm::scale(glm::mat4 { 1.0F }, scale);
  }

  auto BuildDeferredLightWorldMatrix(const glm::vec3 position,
    const glm::quat& rotation, const glm::vec3 scale) -> glm::mat4
  {
    return glm::translate(glm::mat4 { 1.0F }, position)
      * glm::mat4_cast(rotation) * MakeScaleMatrix(scale);
  }

  auto IsPerspectiveProjection(const ResolvedView& view) -> bool
  {
    return std::abs(view.ProjectionMatrix()[2][3]) > 0.5F;
  }

  auto IsCameraInsidePointLightVolume(const glm::vec3 camera,
    const float near_clip, const DeferredLightConstants& constants) -> bool
  {
    const auto position = glm::vec3 { constants.light_position_and_radius };
    const auto radius
      = constants.light_position_and_radius.w * 1.05F + near_clip;
    const auto delta = camera - position;
    return glm::dot(delta, delta) < radius * radius;
  }

  auto IsCameraInsideSpotLightVolume(const glm::vec3 camera,
    const float near_clip, const DeferredLightConstants& constants) -> bool
  {
    const auto position = glm::vec3 { constants.light_position_and_radius };
    const auto direction
      = glm::normalize(glm::vec3 { constants.light_direction_and_falloff });
    const auto range
      = (std::max)(constants.light_position_and_radius.w, 0.001F);
    const auto outer_cosine
      = std::clamp(constants.spot_angles.y, 0.001F, 0.999999F);
    const auto outer_sine
      = std::sqrt((std::max)(0.0F, 1.0F - outer_cosine * outer_cosine));
    const auto outer_tangent = outer_sine / (std::max)(outer_cosine, 1.0e-4F);
    const auto to_camera = camera - position;
    const auto axial_distance = glm::dot(to_camera, direction);
    if (axial_distance < -near_clip || axial_distance > range + near_clip) {
      return false;
    }

    const auto radial_sq = (std::max)(glm::dot(to_camera, to_camera)
        - axial_distance * axial_distance,
      0.0F);
    const auto expanded_radius
      = (std::max)(axial_distance + near_clip, 0.0F) * outer_tangent
      + near_clip;
    return radial_sq <= expanded_radius * expanded_radius;
  }

  auto ResolveLocalLightDrawMode(const RenderContext& ctx,
    const DeferredLightDraw& draw) -> DeferredLocalLightDrawMode
  {
    const auto* resolved_view = ctx.current_view.resolved_view.get();
    if (resolved_view == nullptr || !IsPerspectiveProjection(*resolved_view)) {
      return DeferredLocalLightDrawMode::kNonPerspective;
    }

    const auto near_clip
      = (std::max)(resolved_view->NearPlane(), 0.001F) * 2.0F;
    const auto camera = resolved_view->CameraPosition();

    switch (draw.kind) {
    case DeferredLightKind::kPoint:
      return IsCameraInsidePointLightVolume(camera, near_clip, draw.constants)
        ? DeferredLocalLightDrawMode::kCameraInsideVolume
        : DeferredLocalLightDrawMode::kOutsideVolume;
    case DeferredLightKind::kSpot:
      return IsCameraInsideSpotLightVolume(camera, near_clip, draw.constants)
        ? DeferredLocalLightDrawMode::kCameraInsideVolume
        : DeferredLocalLightDrawMode::kOutsideVolume;
    case DeferredLightKind::kDirectional:
      break;
    }

    return DeferredLocalLightDrawMode::kOutsideVolume;
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

  auto BuildLocalLightFramebuffer(const SceneTextures& scene_textures)
    -> graphics::FramebufferDesc
  {
    auto desc = BuildDirectionalLightFramebuffer(scene_textures);
    desc.SetDepthAttachment({
      .texture = scene_textures.GetSceneDepthResource(),
      .format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .is_read_only = true,
    });
    return desc;
  }

  auto NeedsDirectionalLightFramebufferRebuild(
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

  auto NeedsLocalLightFramebufferRebuild(
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
      || desc.depth_attachment.texture.get()
      != scene_textures.GetSceneDepthResource().get()
      || !desc.depth_attachment.is_read_only;
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

  auto MakeAdditiveBlendTarget() -> graphics::BlendTargetDesc
  {
    return {
      .blend_enable = true,
      .src_blend = graphics::BlendFactor::kOne,
      .dest_blend = graphics::BlendFactor::kOne,
      .blend_op = graphics::BlendOp::kAdd,
      .src_blend_alpha = graphics::BlendFactor::kOne,
      .dest_blend_alpha = graphics::BlendFactor::kOne,
      .blend_op_alpha = graphics::BlendOp::kAdd,
      .write_mask = graphics::ColorWriteMask::kAll,
    };
  }

  auto MakeDisabledColorWriteTarget() -> graphics::BlendTargetDesc
  {
    return {
      .blend_enable = false,
      .write_mask = graphics::ColorWriteMask::kNone,
    };
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

  auto BuildDeferredDirectionalPipelineDesc(const SceneTextures& scene_textures)
    -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
        .entry_point = "DeferredLightDirectionalVS",
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Services/Lighting/DeferredLightDirectional.hlsl",
        .entry_point = "DeferredLightDirectionalPS",
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(graphics::DepthStencilStateDesc::Disabled())
      .SetBlendState({ MakeAdditiveBlendTarget() })
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
      .SetDebugName("Vortex.DeferredLight.Directional")
      .Build();
  }

  auto BuildDeferredLocalPipelineDesc(const SceneTextures& scene_textures,
    const DeferredLightKind light_kind, const bool reverse_z,
    const DeferredLocalLightDrawMode draw_mode)
    -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    const auto direct_local_light
      = draw_mode != DeferredLocalLightDrawMode::kOutsideVolume;
    const auto source_path = light_kind == DeferredLightKind::kPoint
      ? "Vortex/Services/Lighting/DeferredLightPoint.hlsl"
      : "Vortex/Services/Lighting/DeferredLightSpot.hlsl";
    const auto vertex_entry = light_kind == DeferredLightKind::kPoint
      ? "DeferredLightPointVS"
      : "DeferredLightSpotVS";
    const auto pixel_entry = light_kind == DeferredLightKind::kPoint
      ? "DeferredLightPointPS"
      : "DeferredLightSpotPS";

    auto depth_stencil = graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = false,
      .depth_func = graphics::CompareOp::kAlways,
      .stencil_enable = false,
      .stencil_read_mask = 0xFF,
      .stencil_write_mask = 0x00,
    };
    if (!direct_local_light) {
      depth_stencil.depth_func = reverse_z
        ? graphics::CompareOp::kGreaterOrEqual
        : graphics::CompareOp::kLessOrEqual;
    }

    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = source_path,
        .entry_point = vertex_entry,
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = source_path,
        .entry_point = pixel_entry,
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(direct_local_light
          ? graphics::RasterizerStateDesc::FrontFaceCulling()
          : graphics::RasterizerStateDesc::BackFaceCulling())
      .SetDepthStencilState(depth_stencil)
      .SetBlendState({ MakeAdditiveBlendTarget() })
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = {
          scene_textures.GetSceneColor().GetDescriptor().format,
        },
        .depth_stencil_format = scene_textures.GetSceneDepth().GetDescriptor().format,
        .sample_count = scene_textures.GetSceneColor().GetDescriptor().sample_count,
        .sample_quality
        = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(light_kind == DeferredLightKind::kPoint
          ? (draw_mode == DeferredLocalLightDrawMode::kCameraInsideVolume
                ? "Vortex.DeferredLight.Point.InsideVolumeLighting"
                : (draw_mode == DeferredLocalLightDrawMode::kNonPerspective
                      ? "Vortex.DeferredLight.Point.NonPerspectiveLighting"
                      : "Vortex.DeferredLight.Point.Lighting"))
          : (draw_mode == DeferredLocalLightDrawMode::kCameraInsideVolume
                ? "Vortex.DeferredLight.Spot.InsideVolumeLighting"
                : (draw_mode == DeferredLocalLightDrawMode::kNonPerspective
                      ? "Vortex.DeferredLight.Spot.NonPerspectiveLighting"
                      : "Vortex.DeferredLight.Spot.Lighting")))
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

  auto ResolveAuthoredExposureValue(
    const scene::environment::PostProcessVolume& post_process,
    const RenderContext& ctx) -> float
  {
    if (!post_process.GetExposureEnabled()) {
      return 1.0F;
    }

    float ev = post_process.GetManualExposureEv();
    if (post_process.GetExposureMode() == engine::ExposureMode::kManualCamera
      && ctx.current_view.resolved_view != nullptr
      && ctx.current_view.resolved_view->CameraEv().has_value()) {
      ev = *ctx.current_view.resolved_view->CameraEv();
    }

    return engine::ExposureScaleFromEv100(ev,
      post_process.GetExposureCompensationEv(), post_process.GetExposureKey());
  }

  auto ResolveAuthoredPostProcessConfig(const RenderContext& ctx)
    -> PostProcessConfig
  {
    auto config = PostProcessConfig {};
    const auto* scene = ctx.GetScene().get();
    if (scene != nullptr) {
      const auto environment = scene->GetEnvironment();
      if (environment != nullptr) {
        const auto post_process
          = environment->TryGetSystem<scene::environment::PostProcessVolume>();
        if (post_process) {
          config.enable_bloom = post_process->GetBloomIntensity() > 0.0F;
          config.bloom_intensity = post_process->GetBloomIntensity();
          config.bloom_threshold = post_process->GetBloomThreshold();
          config.tone_mapper = post_process->GetToneMapper();
          config.enable_auto_exposure = post_process->GetExposureEnabled()
            && post_process->GetExposureMode() == engine::ExposureMode::kAuto;
          config.fixed_exposure
            = ResolveAuthoredExposureValue(*post_process, ctx);
          config.gamma = post_process->GetDisplayGamma();
          config.metering_mode = post_process->GetAutoExposureMeteringMode();
          config.auto_exposure_speed_up
            = post_process->GetAutoExposureSpeedUp();
          config.auto_exposure_speed_down
            = post_process->GetAutoExposureSpeedDown();
          config.auto_exposure_low_percentile
            = post_process->GetAutoExposureLowPercentile();
          config.auto_exposure_high_percentile
            = post_process->GetAutoExposureHighPercentile();
          config.auto_exposure_min_ev = post_process->GetAutoExposureMinEv();
          config.auto_exposure_max_ev = post_process->GetAutoExposureMaxEv();
          config.auto_exposure_min_log_luminance
            = post_process->GetAutoExposureMinLogLuminance();
          config.auto_exposure_log_luminance_range
            = post_process->GetAutoExposureLogLuminanceRange();
          config.auto_exposure_target_luminance
            = post_process->GetAutoExposureTargetLuminance()
            * engine::ExposureBiasScale(
              post_process->GetExposureCompensationEv(),
              post_process->GetExposureKey());
          config.auto_exposure_spot_meter_radius
            = post_process->GetAutoExposureSpotMeterRadius();
        }
      }
    }

    if (ctx.shader_debug_mode != ShaderDebugMode::kDisabled
      || ctx.render_mode == RenderMode::kWireframe) {
      config.enable_auto_exposure = false;
      config.fixed_exposure = 1.0F;
      config.tone_mapper = engine::ToneMapper::kNone;
      config.enable_bloom = false;
      config.bloom_intensity = 0.0F;
      config.bloom_threshold = 0.0F;
    }

    return config;
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

SceneRenderer::SceneRenderer(Renderer& renderer, Graphics& gfx,
  const SceneTexturesConfig config, const ShadingMode default_shading_mode)
  : renderer_(renderer)
  , gfx_(gfx)
  , scene_textures_(gfx, config)
  , scene_texture_pool_(gfx, config)
  , active_scene_textures_(&scene_textures_)
  , inspected_scene_textures_(&scene_textures_)
  , default_shading_mode_(default_shading_mode)
{
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)) {
    init_views_ = std::make_unique<InitViewsModule>(renderer_);
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
  if (renderer_.HasCapability(
        RendererCapabilityFamily::kFinalOutputComposition)) {
    post_process_ = std::make_unique<PostProcessService>(renderer_);
  }
}

SceneRenderer::~SceneRenderer() { ResetExtractArtifacts(); }

void SceneRenderer::OnFrameStart(const engine::FrameContext& frame)
{
  if (const auto frame_extent = ResolveFrameViewportExtent(frame);
    frame_extent.has_value() && *frame_extent != scene_textures_.GetExtent()) {
    ResizeSceneTextureFamily(*frame_extent);
  }

  setup_mode_.Reset();
  scene_texture_bindings_.Invalidate();
  ResetExtractArtifacts();
  InvalidatePublishedViewFrameBindings();
  deferred_lighting_state_ = {};
  environment_lighting_state_ = {};
  frame_light_selection_ = {};
  frame_lighting_views_.clear();
  frame_shadow_views_.clear();
  lighting_grid_built_sequence_ = frame::SequenceNumber {};
  shadow_depths_built_sequence_ = frame::SequenceNumber {};
  if (lighting_ != nullptr) {
    lighting_->OnFrameStart(
      frame.GetFrameSequenceNumber(), frame.GetFrameSlot());
  }
  if (shadows_ != nullptr) {
    shadows_->OnFrameStart(
      frame.GetFrameSequenceNumber(), frame.GetFrameSlot());
  }
  if (environment_ != nullptr) {
    environment_->OnFrameStart(
      frame.GetFrameSequenceNumber(), frame.GetFrameSlot());
  }
  if (post_process_ != nullptr) {
    post_process_->OnFrameStart(
      frame.GetFrameSequenceNumber(), frame.GetFrameSlot());
  }
}

void SceneRenderer::OnPreRender(const engine::FrameContext& /*frame*/) { }

void SceneRenderer::PrimePreparedViews(RenderContext& ctx)
{
  if (init_views_ != nullptr) {
    init_views_->Execute(ctx, scene_textures_);
  }
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
  PrimePreparedViews(ctx);
  for (std::size_t view_index = 0U; view_index < ctx.frame_views.size();
       ++view_index) {
    const auto& entry = ctx.frame_views[view_index];
    if (!entry.is_scene_view) {
      continue;
    }

    internal::PerViewScope view_scope { ctx, view_index };
    auto scene_texture_lease
      = scene_texture_pool_.Acquire(BuildSceneTextureLeaseKey(ctx));
    auto& leased_scene_textures = scene_texture_lease.GetSceneTextures();
    active_scene_textures_ = &leased_scene_textures;
    inspected_scene_textures_ = &leased_scene_textures;
    auto restore_scene_texture_family = ScopeGuard([this]() noexcept {
      active_scene_textures_ = &scene_textures_;
    });
    BindPreparedView(ctx);
    ResetPerViewSceneProducts();
    renderer_.DispatchViewExtensionsOnViewSetup(ctx);
    renderer_.PublishCurrentViewPreSceneFrameBindings(ctx, *this);
    renderer_.DispatchViewExtensionsOnPreRenderViewGpu(ctx);
    RenderCurrentView(ctx);
    renderer_.PublishCurrentViewPostSceneFrameBindings(ctx, *this);
    renderer_.DispatchViewExtensionsOnPostRenderViewGpu(ctx);
  }
}

void SceneRenderer::OnRender(RenderContext& ctx)
{
  if (!ctx.frame_views.empty()
    && ctx.current_view.view_id == kInvalidViewId
    && !ctx.per_view_scope_active_) {
    RenderViewFamily(ctx);
    return;
  }

  RenderCurrentView(ctx);
}

void SceneRenderer::RenderCurrentView(RenderContext& ctx)
{
  deferred_lighting_state_ = {};
  auto& scene_textures = ActiveSceneTextures();
  if (const auto target_extent = ResolveRenderContextTargetExtent(ctx);
    target_extent.has_value()
    && *target_extent != scene_textures.GetExtent()) {
    ResizeSceneTextureFamily(*target_extent);
  }

  const auto shading_mode = ResolveShadingModeForCurrentView(ctx);
  const auto wireframe_only = ctx.render_mode == RenderMode::kWireframe;
  ctx.current_view.screen_hzb_request = wireframe_only
    ? RenderContext::ScreenHzbRequest {}
    : ResolveScreenHzbRequest(ctx, shading_mode);
  if (wireframe_only) {
    ctx.current_view.depth_prepass_mode = DepthPrePassMode::kDisabled;
  }
  ctx.current_view.scene_depth_product_valid = false;
  // Renderer Core materializes the eligible views and selects the current
  // scene-view cursor in RenderContext. SceneRenderer owns the stage chain for
  // that selected current view only.

  // Stage 2: InitViews
  if (ctx.current_view.prepared_frame == nullptr) {
    PrimePreparedView(ctx);
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage2.InitViews",
      .kind = DiagnosticsPassKind::kCpuOnly,
      .executed = ctx.current_view.prepared_frame != nullptr,
      .outputs = { "Vortex.PreparedSceneFrame" },
      .missing_inputs = ctx.current_view.prepared_frame == nullptr
        ? std::vector<std::string> { "PreparedSceneFrame" }
        : std::vector<std::string> {},
    });

  if (lighting_ != nullptr
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
    CollectLightingViewInputs(ctx, init_views_.get(), frame_lighting_views_);
    lighting_->BuildLightGrid(FrameLightingInputs {
      .frame_light_set = &frame_light_selection_,
      .active_views = std::span(frame_lighting_views_),
    });
    lighting_grid_built_sequence_ = ctx.frame_sequence;
  }
  if (lighting_ != nullptr) {
    published_view_frame_bindings_.lighting_frame_slot
      = lighting_->ResolveLightingFrameSlot(ctx.current_view.view_id);
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
  if (depth_prepass_ != nullptr) {
    depth_prepass_->SetConfig(DepthPrepassConfig {
      .mode = ctx.current_view.depth_prepass_mode,
      .write_velocity = scene_textures.GetVelocity() != nullptr,
    });
    depth_prepass_->Execute(ctx, scene_textures);
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
      .outputs = { "Vortex.SceneDepth", "Vortex.PartialDepth" },
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
    screen_hzb_->Execute(ctx, scene_textures);
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
      .outputs = { "Vortex.ScreenHzb" },
      .missing_inputs = ctx.current_view.CanBuildScreenHzb()
          && !ctx.current_view.scene_depth_product_valid
        ? std::vector<std::string> { "Vortex.SceneDepth" }
        : std::vector<std::string> {},
    });
  RecordDiagnosticsViewProduct(renderer_, "Vortex.ScreenHzb",
    "Vortex.Stage5.ScreenHzbBuild",
    published_view_frame_bindings_.screen_hzb_frame_slot);

  if (occlusion_ != nullptr) {
    occlusion_->SetConfig(OcclusionConfig {
      .enabled = renderer_.GetOcclusionEnabled(),
      .max_candidate_count = renderer_.GetOcclusionMaxCandidateCount(),
    });
    occlusion_->Execute(ctx, scene_textures);
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
  if (shadows_ != nullptr
    && shadow_depths_built_sequence_ != ctx.frame_sequence) {
    CollectShadowViewInputs(ctx, init_views_.get(), frame_shadow_views_);
    shadows_->RenderShadowDepths(FrameShadowInputs {
      .frame_light_set = &frame_light_selection_,
      .active_views = std::span(frame_shadow_views_),
    });
    shadow_depths_built_sequence_ = ctx.frame_sequence;
  }
  if (shadows_ != nullptr) {
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
  if (environment_ != nullptr) {
    published_view_frame_bindings_.environment_frame_slot
      = environment_->PublishEnvironmentBindings(ctx,
        kInvalidShaderVisibleIndex, kInvalidShaderVisibleIndex, false,
        &scene_textures);
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
  if (shadows_ != nullptr || environment_ != nullptr) {
    renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
  }

  // Stage 9: Base pass
  auto base_pass_published = false;
  auto base_pass_draw_count = std::uint32_t { 0U };
  auto base_pass_occlusion_culled_draw_count = std::uint32_t { 0U };
  if (base_pass_ != nullptr) {
    base_pass_->SetConfig(BasePassConfig {
      .write_velocity = scene_textures.GetVelocity() != nullptr,
      .early_z_pass_done = ctx.current_view.IsEarlyDepthComplete(),
      .shading_mode = shading_mode,
      .render_mode = ctx.render_mode,
    });
    const auto base_pass_result = base_pass_->Execute(ctx, scene_textures);
    base_pass_published = base_pass_result.published_base_pass_products;
    base_pass_draw_count = base_pass_result.draw_count;
    base_pass_occlusion_culled_draw_count
      = base_pass_result.occlusion_culled_draw_count;
    if (base_pass_result.published_base_pass_products
      && base_pass_result.completed_velocity_for_dynamic_geometry) {
      PublishBasePassVelocity();
    }
    if (base_pass_result.published_base_pass_products) {
      PublishDeferredBasePassSceneTextures(ctx);
      renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
    }
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage9.BasePass",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = base_pass_published,
      .inputs = ctx.current_view.occlusion_results.get() != nullptr
        ? std::vector<std::string> { "Vortex.PreparedSceneFrame",
            "Vortex.OcclusionFrameResults" }
        : std::vector<std::string> { "Vortex.PreparedSceneFrame" },
      .outputs = { "Vortex.SceneColor", "Vortex.GBuffer" },
    });
  if (base_pass_published) {
    RecordDiagnosticsProduct(renderer_,
      DiagnosticsProductRecord {
        .name = "Vortex.BasePassDrawCommands",
        .producer_pass = "Vortex.Stage9.BasePass",
        .descriptor = BasePassDrawDescriptor(
          base_pass_draw_count, base_pass_occlusion_culled_draw_count),
        .published = true,
        .valid = true,
      });
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
  const auto rendered_debug_visualization
    = RenderDebugVisualization(ctx, scene_textures);
  if (!rendered_debug_visualization) {
    RenderDeferredLighting(ctx, scene_textures);
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = rendered_debug_visualization ? "Vortex.Stage12.DebugVisualization"
                                           : "Vortex.Stage12.DeferredLighting",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = true,
      .inputs = { "Vortex.SceneColor", "Vortex.GBuffer",
        "Vortex.LightingFrameBindings", "Vortex.ShadowFrameBindings" },
      .outputs = { "Vortex.SceneColor" },
    });

  // Stage 13: reserved - IndirectLightingService

  // Stage 14: reserved - EnvironmentLightingService volumetrics

  // Stage 15: Sky / atmosphere / fog
  if (environment_ != nullptr && !wireframe_only
    && !IsNonIblDebugMode(ctx.shader_debug_mode)) {
    environment_->RenderSkyAndFog(ctx, scene_textures);
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
          "Vortex.ShadowFrameBindings" },
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
          "Vortex.Environment.IntegratedLightScattering" },
        .outputs = { "Vortex.SceneColor" },
      });
  }

  // Stage 16: reserved - WaterService

  // Stage 17: reserved - post-opaque extensions

  // Stage 18: Translucency
  auto translucency_result = TranslucencyExecutionResult {};
  if (translucency_ != nullptr && !wireframe_only
    && !IsNonIblDebugMode(ctx.shader_debug_mode)) {
    translucency_result = translucency_->Execute(ctx, scene_textures);
  }
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage18.Translucency",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = translucency_result.executed,
      .inputs = { "Vortex.PreparedSceneFrame", "Vortex.SceneColor",
        "Vortex.SceneDepth", "Vortex.LightingFrameBindings",
        "Vortex.ShadowFrameBindings", "Vortex.EnvironmentFrameBindings" },
      .outputs = { "Vortex.SceneColor" },
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

  if (base_pass_ != nullptr && ctx.render_mode == RenderMode::kOverlayWireframe
    && !wireframe_only) {
    const auto overlay_draws
      = base_pass_->ExecuteWireframeOverlay(ctx, scene_textures);
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage20.WireframeOverlay",
        .kind = DiagnosticsPassKind::kGraphics,
        .executed = overlay_draws > 0U,
        .inputs = { "Vortex.SceneColor", "Vortex.SceneDepth",
          "Vortex.PreparedSceneFrame" },
        .outputs = { "Vortex.SceneColor" },
      });
  }

  // Maintain late scene-texture publication before the output handoff stages.
  PublishCustomDepthProducts();

  // Stage 21: Resolve scene color
  ResolveSceneColor(ctx);
  RecordDiagnosticsPass(renderer_,
    DiagnosticsPassRecord {
      .name = "Vortex.Stage21.ResolveSceneColor",
      .kind = DiagnosticsPassKind::kGraphics,
      .executed = scene_texture_extracts_.resolved_scene_color.valid,
      .inputs = { "Vortex.SceneColor" },
      .outputs = { "Vortex.ResolvedSceneColor" },
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
  if (post_process_ != nullptr) {
    post_process_->SetConfig(ResolveAuthoredPostProcessConfig(ctx));
    auto post_target = observer_ptr<const graphics::Framebuffer> {};
    if (const auto* active_view = ctx.GetActiveViewEntry();
      active_view != nullptr) {
      if (active_view->composite_source != nullptr) {
        post_target = observer_ptr<const graphics::Framebuffer> {
          active_view->composite_source.get()
        };
      } else if (active_view->primary_target != nullptr) {
        post_target = observer_ptr<const graphics::Framebuffer> {
          active_view->primary_target.get()
        };
      }
    }
    if (post_target == nullptr) {
      post_target = ctx.pass_target;
    }

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
    };
    post_process_->Execute(
      ctx.current_view.view_id, ctx, scene_textures, post_process_inputs);
    published_view_frame_bindings_.post_process_frame_slot
      = post_process_->ResolveBindingSlot(ctx.current_view.view_id);
    RecordDiagnosticsPass(renderer_,
      DiagnosticsPassRecord {
        .name = "Vortex.Stage22.PostProcess",
        .kind = DiagnosticsPassKind::kGraphics,
        .executed = published_view_frame_bindings_.post_process_frame_slot
          != kInvalidShaderVisibleIndex,
        .inputs = { std::string { "Vortex." } + std::string(scene_signal_kind),
          "Vortex.SceneDepth" },
        .outputs = { "Vortex.PostProcessFrameBindings" },
      });
    RecordDiagnosticsViewProduct(renderer_, "Vortex.PostProcessFrameBindings",
      "Vortex.Stage22.PostProcess",
      published_view_frame_bindings_.post_process_frame_slot);
    renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
  }

  // Stage 20: Ground grid
  if (ground_grid_pass_ != nullptr && !wireframe_only) {
    static_cast<void>(ground_grid_pass_->Record(
      ctx, scene_textures, ResolveLateOverlayTarget(ctx)));
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
  PostRenderCleanup(ctx);
}

void SceneRenderer::OnCompositing(RenderContext& /*ctx*/)
{
  // Phase 2 explicitly preserves the seam while Renderer retains composition
  // planning, queueing, target resolution, and presentation ownership.
}

void SceneRenderer::OnFrameEnd(const engine::FrameContext& /*frame*/) { }

void SceneRenderer::RemoveViewState(const ViewId view_id,
  const CompositionView::ViewStateHandle view_state_handle)
{
  InvalidatePublishedViewFrameBindings();
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
  auto& registry = gfx_.GetResourceRegistry();
  const auto reset_artifact
    = [this, &registry](std::shared_ptr<graphics::Texture>& texture) -> void {
    if (!texture) {
      return;
    }
    if (registry.Contains(*texture)) {
      gfx_.ForgetKnownResourceState(*texture);
      registry.UnRegisterResource(*texture);
    }
    gfx_.RegisterDeferredRelease(std::move(texture));
  };

  scene_texture_extracts_.Reset();
  reset_artifact(resolved_scene_color_artifact_.texture);
  reset_artifact(resolved_scene_depth_artifact_.texture);
  reset_artifact(prev_scene_depth_artifact_.texture);
  reset_artifact(prev_velocity_artifact_.texture);
}

auto SceneRenderer::EnsureArtifactTexture(ExtractArtifact& artifact,
  std::string_view debug_name, const graphics::Texture& source)
  -> graphics::Texture*
{
  const auto& source_desc = source.GetDescriptor();
  const auto requires_reallocation = [&]() -> bool {
    if (artifact.texture == nullptr) {
      return true;
    }
    const auto& current_desc = artifact.texture->GetDescriptor();
    return current_desc.width != source_desc.width
      || current_desc.height != source_desc.height
      || current_desc.format != source_desc.format
      || current_desc.sample_count != source_desc.sample_count;
  }();

  if (requires_reallocation) {
    auto artifact_desc = source_desc;
    artifact_desc.debug_name = std::string(debug_name);
    artifact_desc.is_render_target = false;
    artifact_desc.is_uav = false;
    artifact_desc.use_clear_value = false;
    artifact_desc.clear_value = {};
    artifact_desc.initial_state = graphics::ResourceStates::kCommon;
    auto& registry = gfx_.GetResourceRegistry();
    if (artifact.texture != nullptr && registry.Contains(*artifact.texture)) {
      gfx_.ForgetKnownResourceState(*artifact.texture);
      registry.UnRegisterResource(*artifact.texture);
      gfx_.RegisterDeferredRelease(std::move(artifact.texture));
    }
    artifact.texture = gfx_.CreateTexture(artifact_desc);
  }

  if (artifact.texture != nullptr) {
    auto& registry = gfx_.GetResourceRegistry();
    if (!registry.Contains(*artifact.texture)) {
      registry.Register(artifact.texture);
    }
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

auto SceneRenderer::RenderDebugVisualization(
  RenderContext& ctx, const SceneTextures& scene_textures) -> bool
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

  const auto queue_key = gfx_.QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder
    = gfx_.AcquireCommandRecorder(queue_key, "Vortex DebugVisualization");
  if (!recorder) {
    return false;
  }

  graphics::GpuEventScope debug_scope(*recorder,
    fmt::format(
      "Vortex.DebugVisualization.{}", GetDeferredDebugVisualizationName(mode)),
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);

  RequireKnownPersistentState(*recorder, scene_textures.GetSceneColor());
  if (requires_scene_depth) {
    RequireKnownPersistentState(*recorder, scene_textures.GetSceneDepth());
  }
  if (requires_gbuffer) {
    RequireKnownPersistentState(*recorder, scene_textures.GetGBufferNormal());
    RequireKnownPersistentState(*recorder, scene_textures.GetGBufferMaterial());
    RequireKnownPersistentState(
      *recorder, scene_textures.GetGBufferBaseColor());
    RequireKnownPersistentState(
      *recorder, scene_textures.GetGBufferCustomData());
  }

  recorder->RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  if (requires_scene_depth) {
    recorder->RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  }
  if (requires_gbuffer) {
    recorder->RequireResourceState(scene_textures.GetGBufferNormal(),
      graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceState(scene_textures.GetGBufferMaterial(),
      graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceState(scene_textures.GetGBufferBaseColor(),
      graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceState(scene_textures.GetGBufferCustomData(),
      graphics::ResourceStates::kShaderResource);
  }
  recorder->FlushBarriers();
  recorder->BindFrameBuffer(*debug_visualization_framebuffer_);
  SetViewportAndScissor(*recorder, ctx, scene_textures);
  recorder->SetPipelineState(
    BuildDebugVisualizationPipelineDesc(scene_textures, mode));
  recorder->SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants->GetGPUVirtualAddress());
  recorder->Draw(3U, 1U, 0U, 0U);

  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  if (requires_scene_depth) {
    recorder->RequireResourceStateFinal(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  }
  if (requires_gbuffer) {
    recorder->RequireResourceStateFinal(scene_textures.GetGBufferNormal(),
      graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceStateFinal(scene_textures.GetGBufferMaterial(),
      graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceStateFinal(scene_textures.GetGBufferBaseColor(),
      graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceStateFinal(scene_textures.GetGBufferCustomData(),
      graphics::ResourceStates::kShaderResource);
  }

  return true;
}

void SceneRenderer::RenderDeferredLighting(
  RenderContext& ctx, const SceneTextures& scene_textures)
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
  lighting_->RenderDeferredLighting(ctx, scene_textures, frame_light_selection_,
    shadow_bindings != nullptr ? &shadow_bindings->bindings : nullptr,
    shadow_surface, spot_shadow_surface, point_shadow_surface);
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
  deferred_lighting_state_.accumulated_into_scene_color
    = lighting_state.accumulated_into_scene_color;
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
