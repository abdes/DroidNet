//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightConstantsPublisher.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightProxyGeometry.h>
#include <Oxygen/Vortex/Lighting/Passes/DeferredLightPass.h>
#include <Oxygen/Vortex/Lighting/Types/DeferredLightConstants.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>
#include <Oxygen/Vortex/Upload/Errors.h>

namespace oxygen::vortex::lighting {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  enum class DeferredLightKind : std::uint8_t {
    kDirectional = 0U,
    kPoint = 1U,
    kSpot = 2U,
    kStaticSkyLight = 3U,
  };

  enum class DeferredLocalLightDrawMode : std::uint8_t {
    kOutsideVolume = 0U,
    kCameraInsideVolume = 1U,
    kNonPerspective = 2U,
  };

  struct DeferredLightDraw {
    internal::DeferredLightPacket packet {};
    LightSelectionIndex directional_selection_index {
      kInvalidLightSelectionIndex
    };
    DeferredLightKind kind { DeferredLightKind::kDirectional };
    DeferredLocalLightDrawMode draw_mode {
      DeferredLocalLightDrawMode::kOutsideVolume,
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
    if (!recorder.IsResourceTracked(texture)
      && !recorder.AdoptKnownResourceState(texture)) {
      auto initial = texture.GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = texture.GetDescriptor().is_render_target
          ? graphics::ResourceStates::kRenderTarget
          : graphics::ResourceStates::kShaderResource;
      }
      recorder.BeginTrackingResourceState(texture, initial, false);
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
    const float near_clip, const ForwardLocalLightRecord& light) -> bool
  {
    const auto position = light.position_ws;
    const auto radius
      = ((light.range_m + light.source_radius_m) * 1.05F) + near_clip;
    const auto delta = camera - position;
    return glm::dot(delta, delta) < radius * radius;
  }

  auto IsCameraInsideSpotLightVolume(const glm::vec3 camera,
    const float near_clip, const ForwardLocalLightRecord& light) -> bool
  {
    const auto position = light.position_ws;
    const auto direction = light.emitted_direction_ws;
    const auto range = light.range_m;
    const auto outer_cosine = std::clamp(
      1.0F - (2.0F * light.outer_cone_sin_half_squared), 0.001F, 0.999999F);
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

    if (draw.packet.light == nullptr) {
      return DeferredLocalLightDrawMode::kOutsideVolume;
    }
    const auto inside = draw.packet.spherical_proxy
      ? IsCameraInsidePointLightVolume(camera, near_clip, *draw.packet.light)
      : IsCameraInsideSpotLightVolume(camera, near_clip, *draw.packet.light);
    return inside ? DeferredLocalLightDrawMode::kCameraInsideVolume
                  : DeferredLocalLightDrawMode::kOutsideVolume;
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
    case kIblOnly:
    case kDirectPlusIbl:
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

  [[nodiscard]] auto ShouldSkipDirectLightingForIblDebug(
    const ShaderDebugMode mode) -> bool
  {
    return mode == ShaderDebugMode::kIblOnly;
  }

  [[nodiscard]] auto ShouldDrawStaticSkyLightDiffuse(const ShaderDebugMode mode)
    -> bool
  {
    using enum ShaderDebugMode;
    switch (mode) {
    case kDisabled:
    case kIblOnly:
    case kDirectPlusIbl:
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
    const auto* const source_path = light_kind == DeferredLightKind::kPoint
      ? "Vortex/Services/Lighting/DeferredLightPoint.hlsl"
      : "Vortex/Services/Lighting/DeferredLightSpot.hlsl";
    const auto* const vertex_entry = light_kind == DeferredLightKind::kPoint
      ? "DeferredLightPointVS"
      : "DeferredLightSpotVS";
    const auto* const pixel_entry = light_kind == DeferredLightKind::kPoint
      ? "DeferredLightPointPS"
      : "DeferredLightSpotPS";
    const auto* const outside_volume_name
      = light_kind == DeferredLightKind::kPoint
      ? "Vortex.DeferredLight.Point.Lighting"
      : "Vortex.DeferredLight.Spot.Lighting";
    const auto* const non_perspective_name
      = light_kind == DeferredLightKind::kPoint
      ? "Vortex.DeferredLight.Point.NonPerspectiveLighting"
      : "Vortex.DeferredLight.Spot.NonPerspectiveLighting";
    const auto* const inside_volume_name
      = light_kind == DeferredLightKind::kPoint
      ? "Vortex.DeferredLight.Point.InsideVolumeLighting"
      : "Vortex.DeferredLight.Spot.InsideVolumeLighting";
    const auto* const exterior_lighting_name
      = draw_mode == DeferredLocalLightDrawMode::kNonPerspective
      ? non_perspective_name
      : outside_volume_name;
    const auto* const debug_name
      = draw_mode == DeferredLocalLightDrawMode::kCameraInsideVolume
      ? inside_volume_name
      : exterior_lighting_name;

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

    auto rasterizer = direct_local_light
      ? graphics::RasterizerStateDesc::FrontFaceCulling()
      : graphics::RasterizerStateDesc::BackFaceCulling();
    // A proxy can contain visible receivers while its exit surface lies beyond
    // the camera far plane. Z clipping would remove those lighting fragments.
    // Keep XY/W clipping, face culling and the selected depth test; the shader
    // evaluates the actual receiver against the light's finite support.
    rasterizer.depth_clip_enable = false;

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
    .SetRasterizerState(rasterizer)
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
    .SetDebugName(debug_name)
    .Build();
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
  constants_publisher_.reset();
  for (auto* buffer : {
         point_geometry_buffer_.get(),
         spot_geometry_buffer_.get(),
       }) {
    if (buffer != nullptr && registry.Contains(*buffer)) {
      registry.UnRegisterResource(*buffer);
    }
  }
}

auto DeferredLightPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  if (constants_publisher_) {
    constants_publisher_->OnFrameStart(sequence, slot);
  }
}

auto DeferredLightPass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
  const internal::DeferredLightPacketSet& packets,
  const ShadowFrameData* shadow_data,
  std::span<const std::shared_ptr<graphics::Texture>>
    directional_shadow_surfaces,
  const graphics::Texture* spot_shadow_surface,
  const graphics::Texture* point_shadow_surface,
  const bool static_sky_light_available) -> ExecutionState
{
  // Cache the owning label; steady-state scope entry needs no label allocation.
  static const auto kProfile = profiling::CpuProfileScopeDesc {
    .label = "Vortex.Lighting.RecordDeferred",
    .category = profiling::ProfileCategory::kPass,
  };
  const auto profile = profiling::CpuProfileScope(kProfile);
  auto state = ExecutionState {};
  if (ctx.view_constants == nullptr) {
    return state;
  }
  const auto wants_static_sky_light = static_sky_light_available
    && ShouldDrawStaticSkyLightDiffuse(ctx.shader_debug_mode);
  if (packets.directional.empty() && packets.local_lights.empty()
    && !wants_static_sky_light) {
    return state;
  }
  if (shadow_data != nullptr && !shadow_data->cascades.empty()) {
    state.consumed_directional_shadow_product = true;
    state.directional_shadow_cascade_count
      = static_cast<std::uint32_t>(shadow_data->cascades.size());
    for (const auto& family : shadow_data->directional_records) {
      state.directional_shadow_surface_srvs.push_back(
        shadow_data->cascades.at(family.first_cascade.get()).surface_srv);
    }
  }
  if (shadow_data != nullptr && !shadow_data->projected_local_records.empty()) {
    state.consumed_spot_shadow_product = true;
    state.spot_shadow_count
      = static_cast<std::uint32_t>(shadow_data->projected_local_records.size());
    state.spot_shadow_surface_srv
      = shadow_data->projected_local_records.front().surface_srv;
  }
  if (shadow_data != nullptr && !shadow_data->cube_local_records.empty()) {
    state.consumed_point_shadow_product = true;
    state.point_shadow_count
      = static_cast<std::uint32_t>(shadow_data->cube_local_records.size());
    state.point_shadow_surface_srv
      = shadow_data->cube_local_records.front().surface_srv;
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

  const auto skip_direct_lighting
    = ShouldSkipDirectLightingForIblDebug(ctx.shader_debug_mode);
  if (!skip_direct_lighting && !packets.local_lights.empty()) {
    if (point_geometry_buffer_ == nullptr) {
      const auto point_vertices
        = internal::GeneratePointLightProxySphereVertices();
      ensure_geometry_buffer(point_geometry_buffer_, point_geometry_srv_,
        point_geometry_vertex_count_, "LightingService.PointProxyGeometry",
        std::span(point_vertices));
    }
    if (spot_geometry_buffer_ == nullptr) {
      const auto spot_vertices = internal::GenerateSpotLightProxyConeVertices();
      ensure_geometry_buffer(spot_geometry_buffer_, spot_geometry_srv_,
        spot_geometry_vertex_count_, "LightingService.SpotProxyGeometry",
        std::span(spot_vertices));
    }
  }

  auto draws = std::vector<DeferredLightDraw> {};
  if (!skip_direct_lighting) {
    for (const auto& light : packets.directional) {
      draws.push_back(DeferredLightDraw {
        .directional_selection_index = light.selection_index,
        .kind = DeferredLightKind::kDirectional,
        .geometry_vertex_count = 3U,
      });
      ++state.directional_draw_count;
    }
  }
  const auto skip_local_lights = skip_direct_lighting
    || ShouldSkipLocalLightsForDirectionalDebug(ctx.shader_debug_mode);
  if (!skip_local_lights) {
    for (const auto& packet : packets.local_lights) {
      const auto kind = packet.kind == LocalLightKind::kPoint
        ? DeferredLightKind::kPoint
        : DeferredLightKind::kSpot;
      draws.push_back(DeferredLightDraw {
        .packet = packet,
        .kind = kind,
        .geometry_srv
        = packet.spherical_proxy ? point_geometry_srv_ : spot_geometry_srv_,
        .geometry_vertex_count = packet.spherical_proxy
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
  if (wants_static_sky_light) {
    draws.push_back(DeferredLightDraw {
      .kind = DeferredLightKind::kStaticSkyLight,
      .geometry_vertex_count = 3U,
    });
    ++state.static_sky_light_draw_count;
    state.consumed_static_sky_light_product = true;
  }
  state.local_light_count = state.point_light_count + state.spot_light_count;
  state.consumed_packets = !draws.empty();
  state.used_service_owned_geometry = state.local_light_count > 0U;
  if (draws.empty()) {
    return state;
  }

  for (auto& draw : draws) {
    if (draw.kind == DeferredLightKind::kDirectional
      || draw.kind == DeferredLightKind::kStaticSkyLight) {
      continue;
    }
    draw.draw_mode = ResolveLocalLightDrawMode(ctx, draw);
  }

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

  if (!constants_publisher_) {
    constants_publisher_
      = std::make_unique<internal::DeferredLightConstantsPublisher>(gfx,
        renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() });
  }
  constants_publisher_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
  auto constants_records = std::vector<DeferredLightConstants> {};
  constants_records.reserve(draws.size());
  for (const auto& draw : draws) {
    auto constants = DeferredLightConstants {};
    constants.light_type = static_cast<std::uint32_t>(draw.kind);
    constants.light_geometry_vertices_srv = draw.geometry_srv;
    constants.light_geometry_vertex_count = draw.geometry_vertex_count;
    if (draw.kind == DeferredLightKind::kDirectional) {
      constants.selection_index = draw.directional_selection_index;
    } else if (draw.packet.light != nullptr) {
      constants.selection_index = draw.packet.light->selection_index;
      constants.light_world_matrix = draw.packet.light_world_matrix;
    }
    constants_records.push_back(constants);
  }
  const auto publication = constants_publisher_->Publish(constants_records);
  if (!publication) {
    LOG_F(ERROR, "Deferred draw constants could not be published: {}",
      upload::make_error_code(publication.error()).message());
    state.recording_succeeded = false;
    return state;
  }
  const auto& pass_constants_indices = *publication;

  graphics::GpuEventScope stage_scope(recorder,
    "Vortex.Stage12.DeferredLighting",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

  RequireKnownPersistentState(recorder, scene_textures.GetSceneColor());
  RequireKnownPersistentState(recorder, scene_textures.GetSceneDepth());
  RequireKnownPersistentState(recorder, scene_textures.GetGBufferNormal());
  RequireKnownPersistentState(recorder, scene_textures.GetGBufferMaterial());
  RequireKnownPersistentState(recorder, scene_textures.GetGBufferBaseColor());
  RequireKnownPersistentState(recorder, scene_textures.GetGBufferCustomData());
  if (const auto& frame = ctx.current_view.frame_exposure) {
    const auto& status = *frame->current_state->status_buffer;
    if (!recorder.IsResourceTracked(status)
      && !recorder.AdoptKnownResourceState(status)) {
      recorder.BeginTrackingResourceState(
        status, graphics::ResourceStates::kCommon, false);
    }
    recorder.RequireResourceState(
      status, graphics::ResourceStates::kUnorderedAccess);
  }
  for (const auto& surface : directional_shadow_surfaces) {
    if (!surface) {
      continue;
    }
    if (!recorder.IsResourceTracked(*surface)
      && !recorder.AdoptKnownResourceState(*surface)) {
      auto initial = surface->GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kShaderResource;
      }
      recorder.BeginTrackingResourceState(*surface, initial, false);
    }
  }
  if (spot_shadow_surface != nullptr && state.consumed_spot_shadow_product) {
    if (!recorder.IsResourceTracked(*spot_shadow_surface)
      && !recorder.AdoptKnownResourceState(*spot_shadow_surface)) {
      auto initial = spot_shadow_surface->GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kShaderResource;
      }
      recorder.BeginTrackingResourceState(*spot_shadow_surface, initial, false);
    }
  }
  if (point_shadow_surface != nullptr && state.consumed_point_shadow_product) {
    if (!recorder.IsResourceTracked(*point_shadow_surface)
      && !recorder.AdoptKnownResourceState(*point_shadow_surface)) {
      auto initial = point_shadow_surface->GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kShaderResource;
      }
      recorder.BeginTrackingResourceState(
        *point_shadow_surface, initial, false);
    }
  }

  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);
  const auto reverse_z = IsReverseZ(ctx);
  const auto bind_common_root_parameters
    = [&](const ShaderVisibleIndex index) -> void {
    recorder.SetGraphicsRootConstantBufferView(
      view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
    recorder.SetGraphicsRoot32BitConstant(root_constants_param, 0U, 0U);
    recorder.SetGraphicsRoot32BitConstant(
      root_constants_param, index.get(), 1U);
  };

  for (std::size_t i = 0; i < draws.size(); ++i) {
    const auto& draw = draws[i];
    const auto pass_index = pass_constants_indices[i];

    if (draw.kind == DeferredLightKind::kDirectional
      || draw.kind == DeferredLightKind::kStaticSkyLight) {
      graphics::GpuEventScope light_scope(recorder,
        draw.kind == DeferredLightKind::kDirectional
          ? "Vortex.Stage12.DirectionalLight"
          : "Vortex.Stage12.StaticSkyLight",
        profiling::ProfileGranularity::kDiagnostic,
        profiling::ProfileCategory::kPass);
      if (draw.kind == DeferredLightKind::kDirectional) {
        for (const auto& surface : directional_shadow_surfaces) {
          if (surface) {
            recorder.RequireResourceState(
              *surface, graphics::ResourceStates::kShaderResource);
          }
        }
      }
      recorder.RequireResourceState(scene_textures.GetSceneColor(),
        graphics::ResourceStates::kRenderTarget);
      recorder.FlushBarriers();
      recorder.BindFrameBuffer(*directional_framebuffer_);
      SetViewportAndScissor(recorder, ctx, scene_textures);
      recorder.SetPipelineState(BuildDeferredDirectionalPipelineDesc(
        scene_textures, ctx.shader_debug_mode));
      bind_common_root_parameters(pass_index);
      recorder.Draw(3U, 1U, 0U, 0U);
      state.accumulated_into_scene_color = true;
      continue;
    }

    graphics::GpuEventScope local_scope(recorder,
      draw.kind == DeferredLightKind::kPoint ? "Vortex.Stage12.PointLight"
                                             : "Vortex.Stage12.SpotLight",
      profiling::ProfileGranularity::kDiagnostic,
      profiling::ProfileCategory::kPass);
    CHECK_NOTNULL_F(local_framebuffer_.get(),
      "DeferredLightPass: local framebuffer must exist before local-light "
      "draws");
    recorder.RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
    recorder.RequireResourceState(
      scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
    if (draw.kind == DeferredLightKind::kSpot && spot_shadow_surface != nullptr
      && state.consumed_spot_shadow_product) {
      recorder.RequireResourceState(
        *spot_shadow_surface, graphics::ResourceStates::kShaderResource);
    }
    if (draw.kind == DeferredLightKind::kPoint
      && point_shadow_surface != nullptr
      && state.consumed_point_shadow_product) {
      recorder.RequireResourceState(
        *point_shadow_surface, graphics::ResourceStates::kShaderResource);
    }
    recorder.FlushBarriers();
    recorder.BindFrameBuffer(*local_framebuffer_);
    SetViewportAndScissor(recorder, ctx, scene_textures);
    recorder.SetPipelineState(BuildDeferredLocalPipelineDesc(
      scene_textures, draw.kind, reverse_z, draw.draw_mode));
    bind_common_root_parameters(pass_index);
    recorder.Draw(draw.geometry_vertex_count, 1U, 0U, 0U);
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

  recorder.RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder.RequireResourceState(scene_textures.GetGBufferNormal(),
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(scene_textures.GetGBufferMaterial(),
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(scene_textures.GetGBufferBaseColor(),
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(scene_textures.GetGBufferCustomData(),
    graphics::ResourceStates::kShaderResource);
  for (const auto& surface : directional_shadow_surfaces) {
    if (surface) {
      recorder.RequireResourceState(
        *surface, graphics::ResourceStates::kShaderResource);
    }
  }
  if (spot_shadow_surface != nullptr && state.consumed_spot_shadow_product) {
    recorder.RequireResourceState(
      *spot_shadow_surface, graphics::ResourceStates::kShaderResource);
  }
  if (point_shadow_surface != nullptr && state.consumed_point_shadow_product) {
    recorder.RequireResourceState(
      *point_shadow_surface, graphics::ResourceStates::kShaderResource);
  }

  return state;
}

} // namespace oxygen::vortex::lighting
