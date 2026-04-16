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
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Types/Traversal.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/Types/PassMask.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h>

namespace oxygen::vortex {

namespace {
  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

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
  static_assert(sizeof(DeferredLightConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

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
          table.count
            = range.num_descriptors
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

  auto RequireKnownPersistentState(graphics::CommandRecorder& recorder,
    graphics::Texture& texture) -> void
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

  auto ResolveWorldRotation(const scene::Scene& scene,
    const scene::SceneNodeImpl& node) -> glm::quat
  {
    const auto& transform = node.GetComponent<scene::detail::TransformComponent>();
    const auto ignore_parent = node.GetFlags().GetEffectiveValue(
      scene::SceneNodeFlags::kIgnoreParentTransform);
    auto rotation = transform.GetLocalRotation();
    if (const auto parent = node.AsGraphNode().GetParent();
      parent.IsValid() && !ignore_parent) {
      rotation = ResolveWorldRotation(scene, scene.GetNodeImplRef(parent))
        * rotation;
    }
    return rotation;
  }

  auto ResolveWorldMatrix(const scene::Scene& scene,
    const scene::SceneNodeImpl& node) -> glm::mat4
  {
    const auto& transform = node.GetComponent<scene::detail::TransformComponent>();
    const auto ignore_parent = node.GetFlags().GetEffectiveValue(
      scene::SceneNodeFlags::kIgnoreParentTransform);
    auto world = transform.GetLocalMatrix();
    if (const auto parent = node.AsGraphNode().GetParent();
      parent.IsValid() && !ignore_parent) {
      world = ResolveWorldMatrix(scene, scene.GetNodeImplRef(parent)) * world;
    }
    return world;
  }

  auto ResolveWorldPosition(const scene::Scene& scene,
    const scene::SceneNodeImpl& node) -> glm::vec3
  {
    const auto world = ResolveWorldMatrix(scene, node);
    return glm::vec3(world[3]);
  }

  auto ComputeDirectionWs(const scene::Scene& scene,
    const scene::SceneNodeImpl& node) -> glm::vec3
  {
    const auto direction = ResolveWorldRotation(scene, node) * space::move::Forward;
    const auto length_sq = glm::dot(direction, direction);
    if (length_sq <= math::EpsilonDirection) {
      return space::move::Forward;
    }
    return glm::normalize(direction);
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
    const auto radius = constants.light_position_and_radius.w * 1.05F + near_clip;
    const auto delta = camera - position;
    return glm::dot(delta, delta) < radius * radius;
  }

  auto IsCameraInsideSpotLightVolume(const glm::vec3 camera,
    const float near_clip, const DeferredLightConstants& constants) -> bool
  {
    const auto position = glm::vec3 { constants.light_position_and_radius };
    const auto direction
      = glm::normalize(glm::vec3 { constants.light_direction_and_falloff });
    const auto range = (std::max)(constants.light_position_and_radius.w, 0.001F);
    const auto outer_cosine
      = std::clamp(constants.spot_angles.y, 0.001F, 0.999999F);
    const auto outer_sine
      = std::sqrt((std::max)(0.0F, 1.0F - outer_cosine * outer_cosine));
    const auto outer_tangent
      = outer_sine / (std::max)(outer_cosine, 1.0e-4F);
    const auto to_camera = camera - position;
    const auto axial_distance = glm::dot(to_camera, direction);
    if (axial_distance < -near_clip || axial_distance > range + near_clip) {
      return false;
    }

    const auto radial_sq
      = (std::max)(glm::dot(to_camera, to_camera)
          - axial_distance * axial_distance,
        0.0F);
    const auto expanded_radius
      = (std::max)(axial_distance + near_clip, 0.0F) * outer_tangent
      + near_clip;
    return radial_sq <= expanded_radius * expanded_radius;
  }

  auto ResolveLocalLightDrawMode(
    const RenderContext& ctx, const DeferredLightDraw& draw)
    -> DeferredLocalLightDrawMode
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
    case ShaderDebugMode::kSceneDepthRaw:
    case ShaderDebugMode::kSceneDepthLinear:
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
    case ShaderDebugMode::kSceneDepthRaw:
      return "SceneDepthRaw";
    case ShaderDebugMode::kSceneDepthLinear:
      return "SceneDepthLinear";
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
    AddBooleanDefine(
      true, GetShaderDebugDefineName(mode), pixel_defines);

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
      depth_stencil.depth_func
        = reverse_z ? graphics::CompareOp::kGreaterOrEqual
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

  auto ResolveFrameViewportExtent(const engine::FrameContext& frame)
    -> std::optional<glm::uvec2>
  {
    // Preserve the future multi-view shell by sizing against the maximum
    // scene-view envelope. Only fall back to non-scene views when no scene
    // views exist.
    auto max_scene_extent = std::optional<glm::uvec2> {};
    auto max_non_scene_extent = std::optional<glm::uvec2> {};

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

    if (max_scene_extent.has_value()) {
      return max_scene_extent;
    }
    return max_non_scene_extent;
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

  auto HasPublishedGBufferBindings(const SceneTextureBindings& bindings) -> bool
  {
    return std::ranges::all_of(
      bindings.gbuffer_srvs, [](const std::uint32_t index) -> bool {
        return index != SceneTextureBindings::kInvalidIndex;
      });
  }

  auto HasAnyPublishedGBufferBinding(const SceneTextureBindings& bindings)
    -> bool
  {
    return std::ranges::any_of(
      bindings.gbuffer_srvs, [](const std::uint32_t index) -> bool {
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
    base_pass_ = std::make_unique<BasePassModule>(
      renderer_, scene_textures_.GetConfig());
  }
}

SceneRenderer::~SceneRenderer()
{
  ResetExtractArtifacts();
  if (deferred_light_constants_buffer_ != nullptr
    && deferred_light_constants_mapped_ptr_ != nullptr) {
    deferred_light_constants_buffer_->UnMap();
    deferred_light_constants_mapped_ptr_ = nullptr;
  }
  if (deferred_light_constants_buffer_ != nullptr) {
    auto& registry = gfx_.GetResourceRegistry();
    if (registry.Contains(*deferred_light_constants_buffer_)) {
      registry.UnRegisterResource(*deferred_light_constants_buffer_);
    }
  }
}

void SceneRenderer::OnFrameStart(const engine::FrameContext& frame)
{
  if (const auto frame_extent = ResolveFrameViewportExtent(frame);
    frame_extent.has_value() && *frame_extent != scene_textures_.GetExtent()) {
    scene_textures_.Resize(*frame_extent);
  }

  setup_mode_.Reset();
  scene_texture_bindings_.Invalidate();
  ResetExtractArtifacts();
  InvalidatePublishedViewFrameBindings();
  deferred_lighting_state_ = {};
}

void SceneRenderer::OnPreRender(const engine::FrameContext& /*frame*/) { }

void SceneRenderer::PrimePreparedView(RenderContext& ctx)
{
  ctx.current_view.prepared_frame.reset(nullptr);
  if (init_views_ != nullptr) {
    init_views_->Execute(ctx, scene_textures_);
    if (ctx.current_view.view_id != kInvalidViewId) {
      ctx.current_view.prepared_frame = observer_ptr<const PreparedSceneFrame> {
        init_views_->GetPreparedSceneFrame(ctx.current_view.view_id)
      };
    }
  }
}

void SceneRenderer::OnRender(RenderContext& ctx)
{
  deferred_lighting_state_ = {};
  [[maybe_unused]] const auto shading_mode
    = ResolveShadingModeForCurrentView(ctx);
  // Renderer Core materializes the eligible views and selects the current
  // scene-view cursor in RenderContext. SceneRenderer owns the stage chain for
  // that selected current view only.

  // Stage 2: InitViews
  if (ctx.current_view.prepared_frame == nullptr) {
    PrimePreparedView(ctx);
  }

  // Stage 3: Depth prepass + early velocity
  if (depth_prepass_ != nullptr) {
    depth_prepass_->SetConfig(DepthPrepassConfig {
      .mode = ctx.current_view.depth_prepass_mode,
      .write_velocity = scene_textures_.GetVelocity() != nullptr,
    });
    depth_prepass_->Execute(ctx, scene_textures_);
    ctx.current_view.depth_prepass_completeness
      = depth_prepass_->GetCompleteness();
  } else {
    ctx.current_view.depth_prepass_completeness
      = DepthPrePassCompleteness::kDisabled;
  }
  if (ctx.current_view.depth_prepass_completeness
    == DepthPrePassCompleteness::kComplete) {
    ApplyStage3DepthPrepassState();
  }

  // Stage 4: reserved - GeometryVirtualizationService

  // Stage 5: Occlusion / HZB

  // Stage 6: Forward light data / light grid

  // Stage 7: reserved - MaterialCompositionService::PreBasePass

  // Stage 8: Shadow depth

  // Stage 9: Base pass
  if (base_pass_ != nullptr) {
    base_pass_->SetConfig(BasePassConfig {
      .write_velocity = scene_textures_.GetVelocity() != nullptr,
      .early_z_pass_done = ctx.current_view.IsEarlyDepthComplete(),
      .shading_mode = shading_mode,
    });
    const auto base_pass_result = base_pass_->Execute(ctx, scene_textures_);
    if (base_pass_result.published_base_pass_products
      && base_pass_result.completed_velocity_for_dynamic_geometry) {
      ApplyStage9BasePassState();
    }
    if (base_pass_result.published_base_pass_products) {
      ApplyStage10RebuildState();
      renderer_.RefreshCurrentViewFrameBindings(ctx, *this);
    }
  }

  // Stage 11: reserved - MaterialCompositionService::PostBasePass

  // Stage 12: Deferred direct lighting
  if (!RenderDebugVisualization(ctx, scene_textures_)) {
    RenderDeferredLighting(ctx, scene_textures_);
  }

  // Stage 13: reserved - IndirectLightingService

  // Stage 14: reserved - EnvironmentLightingService volumetrics

  // Stage 15: Sky / atmosphere / fog

  // Stage 16: reserved - WaterService

  // Stage 17: reserved - post-opaque extensions

  // Stage 18: Translucency

  // Stage 19: reserved - DistortionModule

  // Stage 20: reserved - LightShaftBloomModule

  // Stage 21: Resolve scene color
  ResolveSceneColor(ctx);

  // Stage 22: Post processing
  ApplyStage22PostProcessState();

  // Stage 23: Post-render cleanup / extraction
  PostRenderCleanup(ctx);
}

void SceneRenderer::OnCompositing(RenderContext& /*ctx*/)
{
  // Phase 2 explicitly preserves the seam while Renderer retains composition
  // planning, queueing, target resolution, and presentation ownership.
}

void SceneRenderer::OnFrameEnd(const engine::FrameContext& /*frame*/) { }

void SceneRenderer::ApplyStage3DepthPrepassState()
{
  auto flags = SceneTextureSetupMode::Flag::kSceneDepth
    | SceneTextureSetupMode::Flag::kPartialDepth;
  if (scene_textures_.GetVelocity() != nullptr) {
    flags = flags | SceneTextureSetupMode::Flag::kSceneVelocity;
  }
  setup_mode_.SetFlags(flags);
  RefreshSceneTextureBindings();
}

void SceneRenderer::ApplyStage9BasePassState()
{
  // Stage 9 owns the raw attachment writes, but Stage 10 remains the first
  // truthful publication boundary for SceneColor and the active GBuffers in
  // the standard SceneTextureBindings route.
  if (scene_textures_.GetVelocity() != nullptr) {
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

void SceneRenderer::ApplyStage10RebuildState()
{
  scene_textures_.RebuildWithGBuffers();
  setup_mode_.SetFlags(SceneTextureSetupMode::Flag::kGBuffers
    | SceneTextureSetupMode::Flag::kSceneColor
    | SceneTextureSetupMode::Flag::kStencil);
  RefreshSceneTextureBindings();
  CHECK_F(HasPublishedGBufferBindings(scene_texture_bindings_),
    "SceneRenderer: Stage 10 must publish GBuffer bindings before deferred "
    "lighting or GBuffer debug inspection");
}

void SceneRenderer::ApplyStage22PostProcessState()
{
  if (scene_textures_.GetCustomDepth() != nullptr) {
    setup_mode_.Set(SceneTextureSetupMode::Flag::kCustomDepth);
  }
  RefreshSceneTextureBindings();
}

void SceneRenderer::ApplyStage23ExtractionState()
{
  // Stage 23 is an extraction boundary only; scene-texture bindless
  // availability remains defined by the prior setup milestones.
}

auto SceneRenderer::GetSceneTextures() const -> const SceneTextures&
{
  return scene_textures_;
}

auto SceneRenderer::GetSceneTextures() -> SceneTextures&
{
  return scene_textures_;
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
  published_view_frame_bindings_slot_ = kInvalidShaderVisibleIndex;
}

void SceneRenderer::RefreshSceneTextureBindings()
{
  scene_texture_bindings_.Invalidate();
  if (setup_mode_.GetFlags() == 0U) {
    return;
  }

  scene_texture_bindings_.valid_flags = setup_mode_.GetFlags();

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneDepth)) {
    scene_texture_bindings_.scene_depth_srv
      = RegisterSceneTextureView(scene_textures_.GetSceneDepth(),
        MakeSrvDesc(scene_textures_.GetSceneDepth(),
          ResolveDepthSrvFormat(
            scene_textures_.GetSceneDepth().GetDescriptor().format)));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kPartialDepth)) {
    scene_texture_bindings_.partial_depth_srv
      = RegisterSceneTextureView(scene_textures_.GetPartialDepth(),
        MakeSrvDesc(scene_textures_.GetPartialDepth(),
          scene_textures_.GetPartialDepth().GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneVelocity)
    && scene_textures_.GetVelocity() != nullptr) {
    scene_texture_bindings_.velocity_srv
      = RegisterSceneTextureView(*scene_textures_.GetVelocity(),
        MakeSrvDesc(*scene_textures_.GetVelocity(),
          scene_textures_.GetVelocity()->GetDescriptor().format));
    scene_texture_bindings_.velocity_uav
      = RegisterSceneTextureView(*scene_textures_.GetVelocity(),
        MakeUavDesc(*scene_textures_.GetVelocity(),
          scene_textures_.GetVelocity()->GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneColor)) {
    scene_texture_bindings_.scene_color_srv
      = RegisterSceneTextureView(scene_textures_.GetSceneColor(),
        MakeSrvDesc(scene_textures_.GetSceneColor(),
          scene_textures_.GetSceneColor().GetDescriptor().format));
    scene_texture_bindings_.scene_color_uav
      = RegisterSceneTextureView(scene_textures_.GetSceneColor(),
        MakeUavDesc(scene_textures_.GetSceneColor(),
          scene_textures_.GetSceneColor().GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kStencil)) {
    const auto stencil_view = scene_textures_.GetStencil();
    if (stencil_view.IsValid()) {
      scene_texture_bindings_.stencil_srv
        = RegisterSceneTextureView(*stencil_view.texture,
          MakeSrvDesc(*stencil_view.texture,
            ResolveStencilSrvFormat(
              stencil_view.texture->GetDescriptor().format)));
    }
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kCustomDepth)
    && scene_textures_.GetCustomDepth() != nullptr) {
    scene_texture_bindings_.custom_depth_srv
      = RegisterSceneTextureView(*scene_textures_.GetCustomDepth(),
        MakeSrvDesc(*scene_textures_.GetCustomDepth(),
          ResolveDepthSrvFormat(
            scene_textures_.GetCustomDepth()->GetDescriptor().format)));

    const auto custom_stencil = scene_textures_.GetCustomStencil();
    if (custom_stencil.IsValid()) {
      scene_texture_bindings_.custom_stencil_srv
        = RegisterSceneTextureView(*custom_stencil.texture,
          MakeSrvDesc(*custom_stencil.texture,
            ResolveStencilSrvFormat(
              custom_stencil.texture->GetDescriptor().format)));
    }
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kGBuffers)) {
    scene_texture_bindings_.gbuffer_srvs[0]
      = RegisterSceneTextureView(scene_textures_.GetGBufferNormal(),
        MakeSrvDesc(scene_textures_.GetGBufferNormal(),
          scene_textures_.GetGBufferNormal().GetDescriptor().format));
    scene_texture_bindings_.gbuffer_srvs[1]
      = RegisterSceneTextureView(scene_textures_.GetGBufferMaterial(),
        MakeSrvDesc(scene_textures_.GetGBufferMaterial(),
          scene_textures_.GetGBufferMaterial().GetDescriptor().format));
    scene_texture_bindings_.gbuffer_srvs[2]
      = RegisterSceneTextureView(scene_textures_.GetGBufferBaseColor(),
        MakeSrvDesc(scene_textures_.GetGBufferBaseColor(),
          scene_textures_.GetGBufferBaseColor().GetDescriptor().format));
    scene_texture_bindings_.gbuffer_srvs[3]
      = RegisterSceneTextureView(scene_textures_.GetGBufferCustomData(),
        MakeSrvDesc(scene_textures_.GetGBufferCustomData(),
          scene_textures_.GetGBufferCustomData().GetDescriptor().format));
  }
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
  return scene_textures_.GetVelocity();
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
    || mode == ShaderDebugMode::kMetalness;
  const auto requires_scene_depth = mode == ShaderDebugMode::kSceneDepthRaw
    || mode == ShaderDebugMode::kSceneDepthLinear;

  if (requires_gbuffer && !HasPublishedGBufferBindings(scene_texture_bindings_)) {
    return false;
  }
  if (requires_scene_depth
    && scene_texture_bindings_.scene_depth_srv
      == SceneTextureBindings::kInvalidIndex) {
    return false;
  }

  if (NeedsDebugVisualizationFramebufferRebuild(
        debug_visualization_framebuffer_, scene_textures)) {
    debug_visualization_framebuffer_
      = gfx_.CreateFramebuffer(BuildDebugVisualizationFramebuffer(scene_textures));
  }

  const auto queue_key = gfx_.QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx_.AcquireCommandRecorder(
    queue_key, "Vortex DebugVisualization");
  if (!recorder) {
    return false;
  }

  graphics::GpuEventScope debug_scope(*recorder,
    fmt::format("Vortex.DebugVisualization.{}",
      GetDeferredDebugVisualizationName(mode)),
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);

  RequireKnownPersistentState(*recorder, scene_textures.GetSceneColor());
  if (requires_scene_depth) {
    RequireKnownPersistentState(*recorder, scene_textures.GetSceneDepth());
  }
  if (requires_gbuffer) {
    RequireKnownPersistentState(*recorder, scene_textures.GetGBufferNormal());
    RequireKnownPersistentState(*recorder, scene_textures.GetGBufferMaterial());
    RequireKnownPersistentState(*recorder, scene_textures.GetGBufferBaseColor());
    RequireKnownPersistentState(*recorder, scene_textures.GetGBufferCustomData());
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
  deferred_lighting_state_.consumed_gbuffer_srvs
    = scene_texture_bindings_.gbuffer_srvs;

  auto* scene_mutable = ctx.GetSceneMutable().get();
  if (scene_mutable == nullptr) {
    return;
  }
  scene_mutable->Update(false);
  const auto* scene_ptr = static_cast<const scene::Scene*>(scene_mutable);

  auto deferred_lights = std::vector<DeferredLightDraw> {};
  const auto visitor = [this, &deferred_lights, scene_ptr](
                         const scene::ConstVisitedNode& visited,
                         const bool dry_run) -> scene::VisitResult {
    static_cast<void>(dry_run);

    const auto& node = *visited.node_impl;
    if (!node.HasComponent<scene::detail::TransformComponent>()) {
      return scene::VisitResult::kContinue;
    }

    if (node.HasComponent<scene::DirectionalLight>()) {
      const auto& light = node.GetComponent<scene::DirectionalLight>();
      if (!light.Common().affects_world) {
        return scene::VisitResult::kContinue;
      }

      DeferredLightDraw draw {};
      draw.kind = DeferredLightKind::kDirectional;
      draw.constants.light_color_and_intensity = glm::vec4(
        light.Common().color_rgb, light.GetIntensityLux());
      draw.constants.light_direction_and_falloff = glm::vec4(
        ComputeDirectionWs(*scene_ptr, node), 1.0F);
      draw.constants.light_world_matrix = glm::mat4 { 1.0F };
      draw.constants.light_type
        = static_cast<std::uint32_t>(DeferredLightKind::kDirectional);
      deferred_lights.push_back(draw);
      ++deferred_lighting_state_.directional_light_count;
    } else if (node.HasComponent<scene::PointLight>()) {
      const auto& light = node.GetComponent<scene::PointLight>();
      if (!light.Common().affects_world) {
        return scene::VisitResult::kContinue;
      }

      const auto position = ResolveWorldPosition(*scene_ptr, node);
      const auto radius = (std::max)(light.GetRange(), 0.001F);

      DeferredLightDraw draw {};
      draw.kind = DeferredLightKind::kPoint;
      draw.constants.light_position_and_radius = glm::vec4(position, radius);
      draw.constants.light_color_and_intensity = glm::vec4(
        light.Common().color_rgb, light.GetLuminousFluxLm());
      draw.constants.light_direction_and_falloff
        = glm::vec4(
          ComputeDirectionWs(*scene_ptr, node), light.GetDecayExponent());
      draw.constants.light_world_matrix = BuildDeferredLightWorldMatrix(
        position, glm::quat { 1.0F, 0.0F, 0.0F, 0.0F },
        glm::vec3 { radius });
      draw.constants.light_type
        = static_cast<std::uint32_t>(DeferredLightKind::kPoint);
      deferred_lights.push_back(draw);
      ++deferred_lighting_state_.point_light_count;
      ++deferred_lighting_state_.local_light_count;
    } else if (node.HasComponent<scene::SpotLight>()) {
      const auto& light = node.GetComponent<scene::SpotLight>();
      if (!light.Common().affects_world) {
        return scene::VisitResult::kContinue;
      }

      const auto position = ResolveWorldPosition(*scene_ptr, node);
      const auto rotation = ResolveWorldRotation(*scene_ptr, node);
      const auto range = (std::max)(light.GetRange(), 0.001F);
      const auto outer_angle = std::clamp(
        light.GetOuterConeAngleRadians(), 0.001F, 1.55334F);
      const auto base_radius = (std::max)(range * std::tan(outer_angle), 0.001F);

      DeferredLightDraw draw {};
      draw.kind = DeferredLightKind::kSpot;
      draw.constants.light_position_and_radius = glm::vec4(position, range);
      draw.constants.light_color_and_intensity = glm::vec4(
        light.Common().color_rgb, light.GetLuminousFluxLm());
      draw.constants.light_direction_and_falloff
        = glm::vec4(
          ComputeDirectionWs(*scene_ptr, node), light.GetDecayExponent());
      draw.constants.spot_angles = glm::vec4(
        std::cos(light.GetInnerConeAngleRadians()),
        std::cos(light.GetOuterConeAngleRadians()), 0.0F, 0.0F);
      draw.constants.light_world_matrix = BuildDeferredLightWorldMatrix(
        position, rotation, glm::vec3 { base_radius, range, base_radius });
      draw.constants.light_type
        = static_cast<std::uint32_t>(DeferredLightKind::kSpot);
      deferred_lights.push_back(draw);
      ++deferred_lighting_state_.spot_light_count;
      ++deferred_lighting_state_.local_light_count;
    }

    return scene::VisitResult::kContinue;
  };

  [[maybe_unused]] const auto traversal_result = scene_ptr->Traverse().Traverse(
    visitor, scene::TraversalOrder::kPreOrder, scene::VisibleFilter {});
  if (deferred_lights.empty()) {
    return;
  }

  for (auto& draw : deferred_lights) {
    if (draw.kind == DeferredLightKind::kDirectional) {
      continue;
    }
    draw.draw_mode = ResolveLocalLightDrawMode(ctx, draw);
  }

  const auto ensure_pass_constants = [&](const std::uint32_t required_slots)
    -> void {
    CHECK_F(required_slots > 0U,
      "SceneRenderer: deferred lighting requires at least one constants slot");
    if (deferred_light_constants_buffer_ != nullptr
      && deferred_light_constants_slot_count_ >= required_slots) {
      return;
    }

    if (deferred_light_constants_buffer_ != nullptr
      && deferred_light_constants_mapped_ptr_ != nullptr) {
      deferred_light_constants_buffer_->UnMap();
      deferred_light_constants_mapped_ptr_ = nullptr;
    }
    if (deferred_light_constants_buffer_ != nullptr) {
      auto& registry = gfx_.GetResourceRegistry();
      if (registry.Contains(*deferred_light_constants_buffer_)) {
        registry.UnRegisterResource(*deferred_light_constants_buffer_);
      }
    }
    deferred_light_constants_buffer_.reset();
    deferred_light_constants_indices_.clear();

    auto desc = graphics::BufferDesc {};
    desc.size_bytes = static_cast<std::uint64_t>(required_slots)
      * kDeferredLightConstantsStride;
    desc.usage = graphics::BufferUsage::kConstant;
    desc.memory = graphics::BufferMemory::kUpload;
    desc.debug_name = "Vortex.DeferredLight.Constants";
    deferred_light_constants_buffer_ = gfx_.CreateBuffer(desc);
    CHECK_NOTNULL_F(deferred_light_constants_buffer_.get(),
      "SceneRenderer: failed to create deferred-light constants buffer");
    gfx_.GetResourceRegistry().Register(deferred_light_constants_buffer_);
    deferred_light_constants_mapped_ptr_
      = deferred_light_constants_buffer_->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(deferred_light_constants_mapped_ptr_,
      "SceneRenderer: failed to map deferred-light constants buffer");

    deferred_light_constants_indices_.reserve(required_slots);
    for (std::uint32_t i = 0U; i < required_slots; ++i) {
      const auto view_desc = graphics::BufferViewDescription {
        .view_type = graphics::ResourceViewType::kConstantBuffer,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .range = { static_cast<std::uint64_t>(i)
            * kDeferredLightConstantsStride,
          kDeferredLightConstantsStride },
      };
      deferred_light_constants_indices_.push_back(
        RegisterBufferViewIndex(
          gfx_, *deferred_light_constants_buffer_, view_desc));
    }
    deferred_light_constants_slot_count_ = required_slots;
  };

  const auto has_local_lights = std::ranges::any_of(
    deferred_lights, [](const DeferredLightDraw& draw) -> bool {
      return draw.kind != DeferredLightKind::kDirectional;
    });
  ensure_pass_constants(static_cast<std::uint32_t>(deferred_lights.size()));

  if (NeedsDirectionalLightFramebufferRebuild(
        deferred_light_directional_framebuffer_, scene_textures)) {
    deferred_light_directional_framebuffer_
      = gfx_.CreateFramebuffer(BuildDirectionalLightFramebuffer(scene_textures));
  }
  if (has_local_lights
    && NeedsLocalLightFramebufferRebuild(
      deferred_light_local_framebuffer_, scene_textures)) {
    deferred_light_local_framebuffer_
      = gfx_.CreateFramebuffer(BuildLocalLightFramebuffer(scene_textures));
  }

  auto pass_constants_indices = std::vector<ShaderVisibleIndex> {};
  pass_constants_indices.reserve(deferred_lights.size());
  auto* mapped_bytes
    = static_cast<std::byte*>(deferred_light_constants_mapped_ptr_);
  for (std::size_t i = 0; i < deferred_lights.size(); ++i) {
    std::memcpy(mapped_bytes + i * kDeferredLightConstantsStride,
      &deferred_lights[i].constants, sizeof(DeferredLightConstants));
    pass_constants_indices.push_back(deferred_light_constants_indices_[i]);
  }

  const auto queue_key = gfx_.QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx_.AcquireCommandRecorder(
    queue_key, "Vortex DeferredLighting");
  if (!recorder) {
    return;
  }
  graphics::GpuEventScope stage_scope(*recorder,
    "Vortex.Stage12.DeferredLighting",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

  RequireKnownPersistentState(*recorder, scene_textures.GetSceneColor());
  RequireKnownPersistentState(*recorder, scene_textures.GetSceneDepth());
  RequireKnownPersistentState(*recorder, scene_textures.GetGBufferNormal());
  RequireKnownPersistentState(*recorder, scene_textures.GetGBufferMaterial());
  RequireKnownPersistentState(*recorder, scene_textures.GetGBufferBaseColor());
  RequireKnownPersistentState(*recorder, scene_textures.GetGBufferCustomData());

  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);
  const auto reverse_z = IsReverseZ(ctx);
  const auto bind_common_root_parameters = [&](const ShaderVisibleIndex index) {
    recorder->SetGraphicsRootConstantBufferView(
      view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
    recorder->SetGraphicsRoot32BitConstant(root_constants_param, 0U, 0U);
    recorder->SetGraphicsRoot32BitConstant(root_constants_param, index.get(), 1U);
  };
  const auto draw_local_light = [&](const DeferredLightDraw& draw,
                                  const ShaderVisibleIndex pass_index) -> void {
    const auto kind = draw.kind;
    const auto draw_mode = draw.draw_mode;
    graphics::GpuEventScope local_scope(*recorder,
      kind == DeferredLightKind::kPoint
        ? "Vortex.Stage12.PointLight"
        : "Vortex.Stage12.SpotLight",
      profiling::ProfileGranularity::kDiagnostic,
      profiling::ProfileCategory::kPass);
    CHECK_NOTNULL_F(deferred_light_local_framebuffer_.get(),
      "SceneRenderer: local-light lighting framebuffer must exist before "
      "local-light draws");

    recorder->RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
    recorder->RequireResourceState(
      scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
    recorder->FlushBarriers();
    recorder->BindFrameBuffer(*deferred_light_local_framebuffer_);
    SetViewportAndScissor(*recorder, ctx, scene_textures);
    recorder->SetPipelineState(BuildDeferredLocalPipelineDesc(
      scene_textures, kind, reverse_z, draw_mode));
    bind_common_root_parameters(pass_index);
    recorder->Draw(kind == DeferredLightKind::kPoint
        ? kDeferredLightPointVertexCount
        : kDeferredLightSpotVertexCount,
      1U, 0U, 0U);
    ++deferred_lighting_state_.direct_local_light_pass_count;
    if (draw_mode == DeferredLocalLightDrawMode::kCameraInsideVolume) {
      ++deferred_lighting_state_.camera_inside_local_light_count;
      deferred_lighting_state_.used_camera_inside_local_lights = true;
    } else if (draw_mode == DeferredLocalLightDrawMode::kNonPerspective) {
      ++deferred_lighting_state_.non_perspective_local_light_count;
      deferred_lighting_state_.used_non_perspective_local_lights = true;
    } else {
      ++deferred_lighting_state_.outside_volume_local_light_count;
      deferred_lighting_state_.used_outside_volume_local_lights = true;
    }
    deferred_lighting_state_.accumulated_into_scene_color = true;
  };

  for (std::size_t i = 0; i < deferred_lights.size(); ++i) {
    const auto& draw = deferred_lights[i];
    const auto pass_index = pass_constants_indices[i];

    if (draw.kind == DeferredLightKind::kDirectional) {
      graphics::GpuEventScope light_scope(*recorder,
        "Vortex.Stage12.DirectionalLight",
        profiling::ProfileGranularity::kDiagnostic,
        profiling::ProfileCategory::kPass);
      recorder->RequireResourceState(scene_textures.GetSceneColor(),
        graphics::ResourceStates::kRenderTarget);
      recorder->FlushBarriers();
      recorder->BindFrameBuffer(*deferred_light_directional_framebuffer_);
      SetViewportAndScissor(*recorder, ctx, scene_textures);
      recorder->SetPipelineState(
        BuildDeferredDirectionalPipelineDesc(scene_textures));
      bind_common_root_parameters(pass_index);
      recorder->Draw(3U, 1U, 0U, 0U);
      deferred_lighting_state_.accumulated_into_scene_color = true;
      continue;
    }

    draw_local_light(draw, pass_index);
  }

  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder->RequireResourceStateFinal(scene_textures.GetGBufferNormal(),
    graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(scene_textures.GetGBufferMaterial(),
    graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(scene_textures.GetGBufferBaseColor(),
    graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(scene_textures.GetGBufferCustomData(),
    graphics::ResourceStates::kShaderResource);
}

} // namespace oxygen::vortex
