//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/Lighting/Passes/DeferredLightPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace oxygen::vortex::lighting {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

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
    glm::uvec4 shadow_info { 0U };
    glm::vec4 atmosphere_transmittance_and_padding { 1.0F, 1.0F, 1.0F, 0.0F };
    std::uint32_t light_type { 0U };
    std::uint32_t light_geometry_vertices_srv {
      kInvalidShaderVisibleIndex.get()
    };
    std::uint32_t light_geometry_vertex_count { 0U };
    std::uint32_t _padding0 { 0U };
  };

  struct DeferredLightDraw {
    internal::DeferredLightPacket packet {};
    DeferredLightKind kind { DeferredLightKind::kDirectional };
    DeferredLocalLightDrawMode draw_mode {
      DeferredLocalLightDrawMode::kOutsideVolume
    };
    ShaderVisibleIndex geometry_srv { kInvalidShaderVisibleIndex };
    std::uint32_t geometry_vertex_count { 0U };
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

  auto RegisterBufferViewIndex(Graphics& gfx, graphics::Buffer& buffer,
    const graphics::BufferViewDescription& desc) -> ShaderVisibleIndex
  {
    auto& registry = gfx.GetResourceRegistry();
    CHECK_F(registry.Contains(buffer),
      "DeferredLightPass: buffer '{}' must be registered before view lookup",
      buffer.GetName());
    if (const auto existing = registry.FindShaderVisibleIndex(buffer, desc);
      existing.has_value()) {
      return *existing;
    }

    auto& allocator = gfx.GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
    CHECK_F(handle.IsValid(),
      "DeferredLightPass: failed to allocate {} view for '{}'",
      graphics::to_string(desc.view_type), buffer.GetName());
    const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(buffer, std::move(handle), desc);
    CHECK_F(view->IsValid(),
      "DeferredLightPass: failed to register {} view for '{}'",
      graphics::to_string(desc.view_type), buffer.GetName());
    return shader_visible_index;
  }

  auto RequireKnownPersistentState(graphics::CommandRecorder& recorder,
    const graphics::Texture& texture) -> void
  {
    if (!recorder.AdoptKnownResourceState(texture)) {
      auto initial = texture.GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = texture.GetDescriptor().is_render_target
          ? graphics::ResourceStates::kRenderTarget
          : graphics::ResourceStates::kShaderResource;
      }
      recorder.BeginTrackingResourceState(texture, initial);
    }
  }

  auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
    const RenderContext& ctx, const SceneTextures& scene_textures) -> void
  {
    const auto extent = scene_textures.GetExtent();
    if (ctx.current_view.resolved_view != nullptr) {
      const auto clamped
        = oxygen::vortex::internal::ResolveClampedViewportState(
          ctx.current_view.resolved_view->Viewport(),
          ctx.current_view.resolved_view->Scissor(), extent.x, extent.y);
      recorder.SetViewport(clamped.viewport);
      recorder.SetScissors(clamped.scissors);
      return;
    }

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
      return IsCameraInsidePointLightVolume(camera, near_clip,
               DeferredLightConstants {
                 .light_position_and_radius
                 = draw.packet.light_position_and_radius,
                 .light_direction_and_falloff
                 = draw.packet.light_direction_and_falloff,
                 .spot_angles = draw.packet.spot_angles,
               })
        ? DeferredLocalLightDrawMode::kCameraInsideVolume
        : DeferredLocalLightDrawMode::kOutsideVolume;
    case DeferredLightKind::kSpot:
      return IsCameraInsideSpotLightVolume(camera, near_clip,
               DeferredLightConstants {
                 .light_position_and_radius
                 = draw.packet.light_position_and_radius,
                 .light_direction_and_falloff
                 = draw.packet.light_direction_and_falloff,
                 .spot_angles = draw.packet.spot_angles,
               })
        ? DeferredLocalLightDrawMode::kCameraInsideVolume
        : DeferredLocalLightDrawMode::kOutsideVolume;
    case DeferredLightKind::kDirectional:
      break;
    }
    return DeferredLocalLightDrawMode::kOutsideVolume;
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

  auto AddBooleanDefine(const bool enabled, std::string_view name,
    std::vector<graphics::ShaderDefine>& defines) -> void
  {
    if (enabled) {
      defines.push_back(graphics::ShaderDefine {
        .name = std::string(name),
        .value = std::string("1"),
      });
    }
  }

  [[nodiscard]] auto IsDirectionalDebugMode(const ShaderDebugMode mode) -> bool
  {
    using enum ShaderDebugMode;
    switch (mode) {
    case kDirectLightingOnly:
    case kDirectLightingFull:
    case kDirectLightGates:
    case kDirectBrdfCore:
      return true;
    default:
      return false;
    }
  }

  [[nodiscard]] auto ShouldSkipLocalLightsForDirectionalDebug(
    const ShaderDebugMode mode) -> bool
  {
    using enum ShaderDebugMode;
    switch (mode) {
    case kDirectLightingOnly:
    case kDirectLightGates:
    case kDirectBrdfCore:
      return true;
    default:
      return false;
    }
  }

  auto BuildDirectionalFramebuffer(const SceneTextures& scene_textures)
    -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = scene_textures.GetSceneColorResource(),
      .format = scene_textures.GetSceneColor().GetDescriptor().format,
    });
    return desc;
  }

  auto BuildLocalFramebuffer(const SceneTextures& scene_textures)
    -> graphics::FramebufferDesc
  {
    auto desc = BuildDirectionalFramebuffer(scene_textures);
    desc.SetDepthAttachment({
      .texture = scene_textures.GetSceneDepthResource(),
      .format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .is_read_only = true,
    });
    return desc;
  }

  auto NeedsDirectionalFramebufferRebuild(
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

  auto NeedsLocalFramebufferRebuild(
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

  auto BuildDeferredDirectionalPipelineDesc(const SceneTextures& scene_textures,
    const ShaderDebugMode debug_mode) -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    auto pixel_defines = std::vector<graphics::ShaderDefine> {};
    AddBooleanDefine(IsDirectionalDebugMode(debug_mode),
      GetShaderDebugDefineName(debug_mode), pixel_defines);
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
      .defines = std::move(pixel_defines),
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

  auto GenerateSphereVertices() -> std::vector<glm::vec4>
  {
    constexpr auto kSlices = 16U;
    constexpr auto kStacks = 8U;
    constexpr auto kTwoPi = std::numbers::pi_v<float> * 2.0F;

    const auto ring_vertex
      = [](const std::uint32_t ring, const std::uint32_t slice) -> glm::vec4 {
      const auto phi = std::numbers::pi_v<float>
        * static_cast<float>(ring) / static_cast<float>(kStacks);
      const auto theta
        = kTwoPi * static_cast<float>(slice) / static_cast<float>(kSlices);
      const auto sin_phi = std::sin(phi);
      return glm::vec4(sin_phi * std::cos(theta), std::cos(phi),
        sin_phi * std::sin(theta), 1.0F);
    };

    auto vertices = std::vector<glm::vec4> {};
    vertices.reserve(6U * kSlices * (kStacks - 1U));
    const auto north = glm::vec4(0.0F, 1.0F, 0.0F, 1.0F);
    const auto south = glm::vec4(0.0F, -1.0F, 0.0F, 1.0F);

    for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
      const auto next_slice = (slice + 1U) % kSlices;
      vertices.push_back(north);
      vertices.push_back(ring_vertex(1U, next_slice));
      vertices.push_back(ring_vertex(1U, slice));
    }

    for (std::uint32_t ring = 1U; ring < kStacks - 1U; ++ring) {
      for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
        const auto next_slice = (slice + 1U) % kSlices;
        const auto v00 = ring_vertex(ring, slice);
        const auto v01 = ring_vertex(ring, next_slice);
        const auto v10 = ring_vertex(ring + 1U, slice);
        const auto v11 = ring_vertex(ring + 1U, next_slice);
        vertices.insert(vertices.end(), { v00, v10, v01, v10, v11, v01 });
      }
    }

    for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
      const auto next_slice = (slice + 1U) % kSlices;
      vertices.push_back(south);
      vertices.push_back(ring_vertex(kStacks - 1U, slice));
      vertices.push_back(ring_vertex(kStacks - 1U, next_slice));
    }

    return vertices;
  }

  auto GenerateConeVertices() -> std::vector<glm::vec4>
  {
    constexpr auto kSlices = 24U;
    constexpr auto kTwoPi = std::numbers::pi_v<float> * 2.0F;
    const auto ring_vertex = [](const std::uint32_t slice) -> glm::vec4 {
      const auto theta
        = kTwoPi * static_cast<float>(slice) / static_cast<float>(kSlices);
      return glm::vec4(std::cos(theta), -1.0F, std::sin(theta), 1.0F);
    };

    auto vertices = std::vector<glm::vec4> {};
    vertices.reserve(6U * kSlices);
    const auto apex = glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
    const auto base_center = glm::vec4(0.0F, -1.0F, 0.0F, 1.0F);
    for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
      const auto next_slice = (slice + 1U) % kSlices;
      vertices.insert(
        vertices.end(), { apex, ring_vertex(next_slice), ring_vertex(slice) });
    }
    for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
      const auto next_slice = (slice + 1U) % kSlices;
      vertices.insert(vertices.end(),
        { base_center, ring_vertex(slice), ring_vertex(next_slice) });
    }
    return vertices;
  }

} // namespace

DeferredLightPass::DeferredLightPass(Renderer& renderer)
  : renderer_(renderer)
{
}

DeferredLightPass::~DeferredLightPass()
{
  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (deferred_light_constants_buffer_ != nullptr
    && deferred_light_constants_mapped_ptr_ != nullptr) {
    deferred_light_constants_buffer_->UnMap();
    deferred_light_constants_mapped_ptr_ = nullptr;
  }
  for (auto* buffer : { deferred_light_constants_buffer_.get(),
         point_geometry_buffer_.get(), spot_geometry_buffer_.get() }) {
    if (buffer != nullptr && registry.Contains(*buffer)) {
      registry.UnRegisterResource(*buffer);
    }
  }
}

auto DeferredLightPass::Record(RenderContext& ctx,
  const SceneTextures& scene_textures,
  const internal::DeferredLightPacketSet& packets,
  const ShadowFrameBindings* directional_shadow_bindings,
  const graphics::Texture* directional_shadow_surface) -> ExecutionState
{
  auto state = ExecutionState {};
  if (ctx.view_constants == nullptr) {
    return state;
  }
  if (!packets.directional.has_value() && packets.local_lights.empty()) {
    return state;
  }
  if (directional_shadow_bindings != nullptr
    && directional_shadow_bindings->HasDirectionalConventionalShadow()) {
    state.consumed_directional_shadow_product = true;
    state.directional_shadow_cascade_count
      = directional_shadow_bindings->cascade_count;
    state.directional_shadow_surface_srv
      = directional_shadow_bindings->conventional_shadow_surface_handle;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return state;
  }

  const auto ensure_geometry_buffer
    = [this, &gfx](std::shared_ptr<graphics::Buffer>& buffer,
        ShaderVisibleIndex& srv, std::uint32_t& vertex_count,
        std::string_view debug_name,
        std::span<const glm::vec4> vertices) -> void {
    if (buffer == nullptr || vertex_count != vertices.size()) {
      auto desc = graphics::BufferDesc {};
      desc.size_bytes = vertices.size_bytes();
      desc.usage = graphics::BufferUsage::kVertex;
      desc.memory = graphics::BufferMemory::kUpload;
      desc.debug_name = std::string(debug_name);
      buffer = gfx->CreateBuffer(desc);
      CHECK_NOTNULL_F(buffer.get(),
        "DeferredLightPass: failed to create geometry buffer '{}'", debug_name);
      auto& registry = gfx->GetResourceRegistry();
      registry.Register(buffer);
      buffer->Update(vertices.data(), vertices.size_bytes(), 0U);
      srv = RegisterBufferViewIndex(*gfx, *buffer,
        graphics::BufferViewDescription {
          .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
          .visibility = graphics::DescriptorVisibility::kShaderVisible,
          .range = {},
          .stride = static_cast<std::uint32_t>(sizeof(glm::vec4)),
        });
      vertex_count = static_cast<std::uint32_t>(vertices.size());
    }
  };

  if (point_geometry_buffer_ == nullptr) {
    const auto point_vertices = GenerateSphereVertices();
    ensure_geometry_buffer(point_geometry_buffer_, point_geometry_srv_,
      point_geometry_vertex_count_, "LightingService.PointProxyGeometry",
      std::span(point_vertices));
  }
  if (spot_geometry_buffer_ == nullptr) {
    const auto spot_vertices = GenerateConeVertices();
    ensure_geometry_buffer(spot_geometry_buffer_, spot_geometry_srv_,
      spot_geometry_vertex_count_, "LightingService.SpotProxyGeometry",
      std::span(spot_vertices));
  }

  auto draws = std::vector<DeferredLightDraw> {};
  if (packets.directional.has_value()) {
    draws.push_back(DeferredLightDraw {
      .kind = DeferredLightKind::kDirectional,
      .geometry_vertex_count = 3U,
    });
    ++state.directional_draw_count;
  }
  const auto skip_local_lights
    = ShouldSkipLocalLightsForDirectionalDebug(ctx.shader_debug_mode);
  if (!skip_local_lights) {
    for (const auto& packet : packets.local_lights) {
      const auto kind = packet.kind == LocalLightKind::kPoint
        ? DeferredLightKind::kPoint
        : DeferredLightKind::kSpot;
      draws.push_back(DeferredLightDraw {
        .packet = packet,
        .kind = kind,
        .geometry_srv = kind == DeferredLightKind::kPoint ? point_geometry_srv_
                                                          : spot_geometry_srv_,
        .geometry_vertex_count = kind == DeferredLightKind::kPoint
          ? point_geometry_vertex_count_
          : spot_geometry_vertex_count_,
      });
      if (kind == DeferredLightKind::kPoint) {
        ++state.point_light_count;
      } else {
        ++state.spot_light_count;
      }
    }
  }
  state.local_light_count = state.point_light_count + state.spot_light_count;
  state.consumed_packets = !draws.empty();
  state.used_service_owned_geometry = state.local_light_count > 0U;

  for (auto& draw : draws) {
    if (draw.kind == DeferredLightKind::kDirectional) {
      continue;
    }
    draw.draw_mode = ResolveLocalLightDrawMode(ctx, draw);
  }

  const auto ensure_pass_constants
    = [&](const std::uint32_t required_slots) -> void {
    CHECK_F(required_slots > 0U,
      "DeferredLightPass: deferred lighting requires at least one constants "
      "slot");
    if (deferred_light_constants_buffer_ != nullptr
      && deferred_light_constants_slot_count_ >= required_slots) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    if (deferred_light_constants_buffer_ != nullptr
      && deferred_light_constants_mapped_ptr_ != nullptr) {
      deferred_light_constants_buffer_->UnMap();
      deferred_light_constants_mapped_ptr_ = nullptr;
    }
    if (deferred_light_constants_buffer_ != nullptr
      && registry.Contains(*deferred_light_constants_buffer_)) {
      registry.UnRegisterResource(*deferred_light_constants_buffer_);
    }
    deferred_light_constants_buffer_.reset();
    deferred_light_constants_indices_.clear();

    auto desc = graphics::BufferDesc {};
    desc.size_bytes = static_cast<std::uint64_t>(required_slots)
      * kDeferredLightConstantsStride;
    desc.usage = graphics::BufferUsage::kConstant;
    desc.memory = graphics::BufferMemory::kUpload;
    desc.debug_name = "LightingService.DeferredLight.Constants";
    deferred_light_constants_buffer_ = gfx->CreateBuffer(desc);
    CHECK_NOTNULL_F(deferred_light_constants_buffer_.get(),
      "DeferredLightPass: failed to create deferred-light constants buffer");
    registry.Register(deferred_light_constants_buffer_);
    deferred_light_constants_mapped_ptr_
      = deferred_light_constants_buffer_->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(deferred_light_constants_mapped_ptr_,
      "DeferredLightPass: failed to map deferred-light constants buffer");

    deferred_light_constants_indices_.reserve(required_slots);
    for (std::uint32_t i = 0U; i < required_slots; ++i) {
      deferred_light_constants_indices_.push_back(
        RegisterBufferViewIndex(*gfx, *deferred_light_constants_buffer_,
          graphics::BufferViewDescription {
            .view_type = graphics::ResourceViewType::kConstantBuffer,
            .visibility = graphics::DescriptorVisibility::kShaderVisible,
            .range
            = { static_cast<std::uint64_t>(i) * kDeferredLightConstantsStride,
              kDeferredLightConstantsStride },
          }));
    }
    deferred_light_constants_slot_count_ = required_slots;
  };
  ensure_pass_constants(static_cast<std::uint32_t>(draws.size()));

  if (NeedsDirectionalFramebufferRebuild(
        directional_framebuffer_, scene_textures)) {
    directional_framebuffer_
      = gfx->CreateFramebuffer(BuildDirectionalFramebuffer(scene_textures));
  }
  if (state.local_light_count > 0U
    && NeedsLocalFramebufferRebuild(local_framebuffer_, scene_textures)) {
    local_framebuffer_
      = gfx->CreateFramebuffer(BuildLocalFramebuffer(scene_textures));
  }

  auto pass_constants_indices = std::vector<ShaderVisibleIndex> {};
  pass_constants_indices.reserve(draws.size());
  auto* mapped_bytes
    = static_cast<std::byte*>(deferred_light_constants_mapped_ptr_);
  for (std::size_t i = 0; i < draws.size(); ++i) {
    auto constants = DeferredLightConstants {};
    const auto& draw = draws[i];
    if (draw.kind == DeferredLightKind::kDirectional
      && packets.directional.has_value()) {
      constants.light_color_and_intensity = glm::vec4(
        packets.directional->color, packets.directional->illuminance_lux);
      constants.light_direction_and_falloff
        = glm::vec4(packets.directional->direction, 1.0F);
      constants.shadow_info.x = directional_shadow_bindings != nullptr
        ? directional_shadow_bindings->cascade_count
        : 0U;
      constants.shadow_info.y = packets.directional->light_flags;
      constants.shadow_info.z = packets.directional->atmosphere_light_slot;
      constants.shadow_info.w = packets.directional->atmosphere_mode_flags;
      constants.atmosphere_transmittance_and_padding
        = glm::vec4(packets.directional->transmittance_toward_sun_rgb, 0.0F);
      constants.light_type
        = static_cast<std::uint32_t>(DeferredLightKind::kDirectional);
    } else {
      constants.light_position_and_radius
        = draw.packet.light_position_and_radius;
      constants.light_color_and_intensity
        = draw.packet.light_color_and_intensity;
      constants.light_direction_and_falloff
        = draw.packet.light_direction_and_falloff;
      constants.spot_angles = draw.packet.spot_angles;
      constants.light_world_matrix = draw.packet.light_world_matrix;
      constants.light_type = static_cast<std::uint32_t>(draw.kind);
      constants.light_geometry_vertices_srv = draw.geometry_srv.get();
      constants.light_geometry_vertex_count = draw.geometry_vertex_count;
    }
    std::memcpy(mapped_bytes + i * kDeferredLightConstantsStride, &constants,
      sizeof(DeferredLightConstants));
    pass_constants_indices.push_back(deferred_light_constants_indices_[i]);
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(
    queue_key, "LightingService DeferredLighting");
  if (!recorder) {
    return state;
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
  if (directional_shadow_surface != nullptr
    && state.consumed_directional_shadow_product) {
    if (!recorder->AdoptKnownResourceState(*directional_shadow_surface)) {
      auto initial = directional_shadow_surface->GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kShaderResource;
      }
      recorder->BeginTrackingResourceState(
        *directional_shadow_surface, initial);
    }
  }

  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);
  const auto reverse_z = IsReverseZ(ctx);
  const auto bind_common_root_parameters = [&](const ShaderVisibleIndex index) {
    recorder->SetGraphicsRootConstantBufferView(
      view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
    recorder->SetGraphicsRoot32BitConstant(root_constants_param, 0U, 0U);
    recorder->SetGraphicsRoot32BitConstant(
      root_constants_param, index.get(), 1U);
  };

  for (std::size_t i = 0; i < draws.size(); ++i) {
    const auto& draw = draws[i];
    const auto pass_index = pass_constants_indices[i];

    if (draw.kind == DeferredLightKind::kDirectional) {
      graphics::GpuEventScope light_scope(*recorder,
        "Vortex.Stage12.DirectionalLight",
        profiling::ProfileGranularity::kDiagnostic,
        profiling::ProfileCategory::kPass);
      if (directional_shadow_surface != nullptr
        && state.consumed_directional_shadow_product) {
        recorder->RequireResourceState(*directional_shadow_surface,
          graphics::ResourceStates::kShaderResource);
      }
      recorder->RequireResourceState(scene_textures.GetSceneColor(),
        graphics::ResourceStates::kRenderTarget);
      recorder->FlushBarriers();
      recorder->BindFrameBuffer(*directional_framebuffer_);
      SetViewportAndScissor(*recorder, ctx, scene_textures);
      recorder->SetPipelineState(BuildDeferredDirectionalPipelineDesc(
        scene_textures, ctx.shader_debug_mode));
      bind_common_root_parameters(pass_index);
      recorder->Draw(3U, 1U, 0U, 0U);
      state.accumulated_into_scene_color = true;
      continue;
    }

    graphics::GpuEventScope local_scope(*recorder,
      draw.kind == DeferredLightKind::kPoint ? "Vortex.Stage12.PointLight"
                                             : "Vortex.Stage12.SpotLight",
      profiling::ProfileGranularity::kDiagnostic,
      profiling::ProfileCategory::kPass);
    CHECK_NOTNULL_F(local_framebuffer_.get(),
      "DeferredLightPass: local framebuffer must exist before local-light "
      "draws");
    recorder->RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
    recorder->RequireResourceState(
      scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
    recorder->FlushBarriers();
    recorder->BindFrameBuffer(*local_framebuffer_);
    SetViewportAndScissor(*recorder, ctx, scene_textures);
    recorder->SetPipelineState(BuildDeferredLocalPipelineDesc(
      scene_textures, draw.kind, reverse_z, draw.draw_mode));
    bind_common_root_parameters(pass_index);
    recorder->Draw(draw.geometry_vertex_count, 1U, 0U, 0U);
    ++state.local_light_draw_count;
    if (draw.draw_mode == DeferredLocalLightDrawMode::kCameraInsideVolume) {
      ++state.camera_inside_local_light_count;
      state.used_camera_inside_local_lights = true;
    } else if (draw.draw_mode == DeferredLocalLightDrawMode::kNonPerspective) {
      ++state.non_perspective_local_light_count;
      state.used_non_perspective_local_lights = true;
    } else {
      ++state.outside_volume_local_light_count;
      state.used_outside_volume_local_lights = true;
    }
    state.accumulated_into_scene_color = true;
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
  if (directional_shadow_surface != nullptr
    && state.consumed_directional_shadow_product) {
    recorder->RequireResourceStateFinal(
      *directional_shadow_surface, graphics::ResourceStates::kShaderResource);
  }

  return state;
}

} // namespace oxygen::vortex::lighting
