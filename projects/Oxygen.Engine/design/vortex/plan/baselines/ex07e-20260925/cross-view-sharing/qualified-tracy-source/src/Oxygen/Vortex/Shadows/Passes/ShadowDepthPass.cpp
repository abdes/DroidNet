//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Frustum.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterCulling.h>
#include <Oxygen/Vortex/Shadows/Internal/SharedShadowMap.h>
#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>

namespace oxygen::vortex::shadows {

struct ShadowDepthPass::SurfaceViews {
  std::weak_ptr<graphics::Texture> surface;
  std::vector<graphics::NativeView> dsvs;
};

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr float kUeCsmShadowSlopeScaleDepthBias = 3.0F;
  constexpr float kUeDefaultUserShadowSlopeBias = 0.5F;
  constexpr float kUeShadowMaxSlopeScaleDepthBias = 1.0F;

  struct alignas(packing::kShaderDataFieldAlignment) ShadowPassConstants {
    glm::mat4 light_view_projection { 1.0F };
    glm::vec4 shadow_bias_parameters { 0.0F };
    glm::vec4 light_direction_to_source { 0.0F, -1.0F, 0.0F, 0.0F };
    glm::vec4 light_position_and_inv_range { 0.0F };
    std::uint32_t draw_metadata_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t current_worlds_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t instance_data_slot { kInvalidShaderVisibleIndex.get() };
    std::uint32_t normal_matrices_slot { kInvalidShaderVisibleIndex.get() };
  };

  static_assert(sizeof(ShadowPassConstants) == 128U);
  static_assert(offsetof(ShadowPassConstants, light_view_projection) == 0U);
  static_assert(offsetof(ShadowPassConstants, shadow_bias_parameters) == 64U);
  static_assert(
    offsetof(ShadowPassConstants, light_direction_to_source) == 80U);
  static_assert(
    offsetof(ShadowPassConstants, light_position_and_inv_range) == 96U);
  static_assert(offsetof(ShadowPassConstants, draw_metadata_slot) == 112U);
  static_assert(offsetof(ShadowPassConstants, current_worlds_slot) == 116U);
  static_assert(offsetof(ShadowPassConstants, instance_data_slot) == 120U);
  static_assert(offsetof(ShadowPassConstants, normal_matrices_slot) == 124U);
  constexpr std::uint32_t kShadowPassConstantsStride
    = sizeof(ShadowPassConstants);

  auto AddBooleanDefine(const bool enabled, std::string_view name,
    std::vector<graphics::ShaderDefine>& defines) -> void
  {
    if (enabled) {
      defines.push_back(
        graphics::ShaderDefine { .name = std::string(name), .value = "1" });
    }
  }

  auto AdoptOrBeginPersistentState(graphics::CommandRecorder& recorder,
    const graphics::Texture& texture) -> void
  {
    if (!recorder.AdoptKnownResourceState(texture)) {
      auto initial = texture.GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kDepthWrite;
      }
      recorder.BeginTrackingResourceState(texture, initial);
    }
  }

  auto BuildShadowPipelineDesc(const graphics::Texture& shadow_surface,
    const vortex::internal::MeshRasterState raster_state)
    -> graphics::GraphicsPipelineDesc
  {
    static const auto root_bindings
      = vortex::internal::BuildVortexRootBindings();
    auto defines = std::vector<graphics::ShaderDefine> {};
    AddBooleanDefine(raster_state.alpha_test, "ALPHA_TEST", defines);
    AddBooleanDefine(shadow_surface.GetDescriptor().texture_type
        == TextureType::kTextureCubeArray,
      "CUBE_SHADOW", defines);

    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Services/Shadows/DirectionalShadowDepth.hlsl",
        .entry_point = "VortexShadowDepthVS",
        .defines = defines,
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Services/Shadows/DirectionalShadowDepth.hlsl",
        .entry_point = "VortexShadowDepthMaskedPS",
        .defines = defines,
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(raster_state.Rasterizer())
      .SetDepthStencilState(graphics::DepthStencilStateDesc {
        .depth_test_enable = true,
        .depth_write_enable = true,
        .depth_func = graphics::CompareOp::kGreaterOrEqual,
        .stencil_enable = false,
      })
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .depth_stencil_format = shadow_surface.GetDescriptor().format,
        .sample_count = shadow_surface.GetDescriptor().sample_count,
        .sample_quality = shadow_surface.GetDescriptor().sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(raster_state.alpha_test ? "Vortex.ShadowDepth.Masked"
                                            : "Vortex.ShadowDepth.Opaque")
      .Build();
  }

  auto ResolveRasterState(const PreparedSceneFrame& prepared_scene,
    const DrawCommand& draw_command) -> vortex::internal::MeshRasterState
  {
    return vortex::internal::ResolveMeshRasterState(
      prepared_scene.GetDrawMetadata(), draw_command.draw_index);
  }

  auto EnsureDepthStencilViewForCascade(Graphics& gfx,
    std::vector<graphics::NativeView>& dsvs, graphics::Texture& shadow_surface,
    const std::uint32_t cascade_index) -> graphics::NativeView
  {
    if (dsvs.size() <= cascade_index) {
      dsvs.resize(cascade_index + 1U);
    }
    if (dsvs[cascade_index]->IsValid()) {
      return dsvs[cascade_index];
    }

    auto& registry = gfx.GetResourceRegistry();
    CHECK_F(registry.Contains(shadow_surface),
      "ShadowDepthPass: shadow surface '{}' must be registered before DSV "
      "lookup",
      shadow_surface.GetName());

    const auto dsv_desc = graphics::TextureViewDescription {
  .view_type = graphics::ResourceViewType::kTexture_DSV,
  .visibility = graphics::DescriptorVisibility::kCpuOnly,
  .format = shadow_surface.GetDescriptor().format,
  .dimension = TextureType::kTexture2DArray,
  .sub_resources = graphics::TextureSubResourceSet {
    .base_mip_level = 0U,
    .num_mip_levels = 1U,
    .base_array_slice = cascade_index,
    .num_array_slices = 1U,
  },
  .is_read_only_dsv = false,
};

    if (const auto existing = registry.Find(shadow_surface, dsv_desc);
      existing->IsValid()) {
      dsvs[cascade_index] = existing;
      return existing;
    }

    auto& allocator = gfx.GetDescriptorAllocator();
    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_DSV,
        graphics::DescriptorVisibility::kCpuOnly);
    CHECK_F(handle.IsValid(),
      "ShadowDepthPass: failed to allocate a DSV for shadow cascade {}",
      cascade_index);
    const auto dsv
      = registry.RegisterView(shadow_surface, std::move(handle), dsv_desc);
    CHECK_F(dsv->IsValid(),
      "ShadowDepthPass: failed to register a DSV for shadow cascade {}",
      cascade_index);
    dsvs[cascade_index] = dsv;
    return dsv;
  }

} // namespace

ShadowDepthPass::ShadowDepthPass(Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetLightingStagingProvider(), kShadowPassConstantsStride,
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "ShadowService.ShadowPassConstants")
{
}

ShadowDepthPass::~ShadowDepthPass() = default;

auto ShadowDepthPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  last_render_state_ = {};
  pass_constants_buffer_.OnFrameStart(sequence, slot);
  std::erase_if(surface_views_,
    [](const auto& item) { return item.second->surface.expired(); });
}

auto ShadowDepthPass::Record(const PreparedViewShadowInput& view_input,
  const std::shared_ptr<graphics::Texture>& shadow_surface,
  const ShadowFrameData& frame_data, const glm::vec3& light_direction)
  -> RenderState
{
  auto depth_slices = std::vector<DepthSlice> {};
  depth_slices.reserve(frame_data.cascades.size());
  for (std::uint32_t cascade_index = 0U;
    cascade_index < frame_data.cascades.size(); ++cascade_index) {
    const auto& cascade = frame_data.cascades.at(cascade_index);
    depth_slices.push_back(DepthSlice {
      .light_view_projection = cascade.light_view_projection,
      .shadow_bias_parameters = glm::vec4(cascade.depth_bias,
        cascade.depth_bias * kUeCsmShadowSlopeScaleDepthBias
          * kUeDefaultUserShadowSlopeBias,
        kUeShadowMaxSlopeScaleDepthBias, 0.0F),
      .light_direction_to_source = glm::vec4(light_direction, 0.0F),
      .target_slice = cascade.array_layer.get(),
    });
  }

  return RecordSlices(view_input, shadow_surface, std::span(depth_slices));
}

auto ShadowDepthPass::RecordSlices(const PreparedViewShadowInput& view_input,
  const std::shared_ptr<graphics::Texture>& shadow_surface,
  const std::span<const DepthSlice> depth_slices,
  const std::shared_ptr<internal::ShadowMapVersion>& local_map) -> RenderState
{
  last_render_state_ = {};
  if (shadow_surface == nullptr || depth_slices.empty()) {
    return last_render_state_;
  }
  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return last_render_state_;
  }

  std::shared_ptr<SurfaceViews> views;
  if (local_map) {
    if (local_map->slot->backing->texture != shadow_surface) {
      throw std::logic_error(
        "Shadow writer target does not match its managed allocation");
    }
  } else {
    auto& cached = surface_views_[{
      shadow_surface.get(), depth_slices.front().target_slice }];
    if (!cached || cached->surface.lock() != shadow_surface) {
      cached = std::make_shared<SurfaceViews>();
      cached->surface = shadow_surface;
    }
    views = cached;
  }
  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key,
    "ShadowService ShadowDepth", graphics::SubmissionPolicy::kExplicit);
  if (!recorder) {
    return last_render_state_;
  }
  renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(*recorder);
  if (local_map) {
    internal::AttachShadowUse(local_map, internal::ShadowUseMode::kWrite,
      *recorder, gfx->GetResourceRegistry());
  }

  auto slice_draws = std::vector<std::vector<DrawCommand>>(depth_slices.size());
  auto has_draws = false;
  if (view_input.prepared_scene != nullptr) {
    auto culling = internal::ShadowCasterCulling {};
    for (std::size_t index = 0U; index < depth_slices.size(); ++index) {
      const auto& slice = depth_slices[index];
      culling.BuildDrawCommands(*view_input.prepared_scene,
        slice.light_view_projection, slice.light_position_and_inv_range);
      const auto draws = culling.GetDrawCommands();
      slice_draws[index].assign(draws.begin(), draws.end());
      has_draws = has_draws || !draws.empty();
      last_render_state_.shadow_caster_draw_count = culling.GetCandidateCount();
    }
  }
  if (has_draws && view_input.view_constants == nullptr) {
    return last_render_state_;
  }

  auto pass_constants_srvs = std::vector<ShaderVisibleIndex> {};
  if (has_draws) {
    pass_constants_srvs.resize(depth_slices.size(), kInvalidShaderVisibleIndex);
    for (std::uint32_t slice_index = 0U; slice_index < depth_slices.size();
      ++slice_index) {
      const auto& depth_slice = depth_slices[slice_index];
      if (slice_draws[slice_index].empty()) {
        continue;
      }
      const auto constants = ShadowPassConstants {
        .light_view_projection = depth_slice.light_view_projection,
        .shadow_bias_parameters = depth_slice.shadow_bias_parameters,
        .light_direction_to_source = depth_slice.light_direction_to_source,
        .light_position_and_inv_range
        = depth_slice.light_position_and_inv_range,
        .draw_metadata_slot
        = view_input.prepared_scene->bindless_draw_metadata_slot.get(),
        .current_worlds_slot
        = view_input.prepared_scene->bindless_worlds_slot.get(),
        .instance_data_slot
        = view_input.prepared_scene->bindless_instance_data_slot.get(),
        .normal_matrices_slot
        = view_input.prepared_scene->bindless_normals_slot.get(),
      };
      auto allocation = pass_constants_buffer_.Allocate(1U);
      if (!allocation.has_value() || !allocation->IsValid(current_sequence_)
        || !allocation->TryWriteObject(constants)) {
        LOG_F(ERROR,
          "ShadowDepthPass: failed to allocate pass constants for shadow slice "
          "{}",
          slice_index);
        return last_render_state_;
      }
      pass_constants_srvs[slice_index] = allocation->srv;
    }
  }

  {
    graphics::GpuEventScope stage_scope(*recorder, "Vortex.Stage8.ShadowDepths",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    if (!local_map) {
      AdoptOrBeginPersistentState(*recorder, *shadow_surface);
    }
    recorder->RequireResourceState(
      *shadow_surface, graphics::ResourceStates::kDepthWrite);

    const auto root_constants_param
      = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
    const auto view_constants_param
      = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);

    auto current_raster_state
      = std::optional<vortex::internal::MeshRasterState> {};
    for (std::uint32_t slice_index = 0U; slice_index < depth_slices.size();
      ++slice_index) {
      const auto target_slice = depth_slices[slice_index].target_slice;
      const auto dsv = local_map
        ? local_map->slot->backing->dsvs.at(target_slice)
        : EnsureDepthStencilViewForCascade(
            *gfx, views->dsvs, *shadow_surface, target_slice);
      const auto& shadow_desc = shadow_surface->GetDescriptor();
      recorder->FlushBarriers();
      recorder->SetRenderTargets({}, dsv);
      recorder->ClearDepthStencilView(
        *shadow_surface, dsv, graphics::ClearFlags::kDepth, 0.0F, 0U);
      recorder->SetViewport({
        .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = static_cast<float>(shadow_desc.width),
        .height = static_cast<float>(shadow_desc.height),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      });
      recorder->SetScissors({
        .left = 0,
        .top = 0,
        .right = static_cast<std::int32_t>(shadow_desc.width),
        .bottom = static_cast<std::int32_t>(shadow_desc.height),
      });

      auto pass_constants_bound = false;
      for (const auto& draw_command : slice_draws[slice_index]) {
        const auto raster_state
          = ResolveRasterState(*view_input.prepared_scene, draw_command);
        if (!current_raster_state.has_value()
          || current_raster_state.value() != raster_state) {
          recorder->SetPipelineState(
            BuildShadowPipelineDesc(*shadow_surface, raster_state));
          recorder->SetGraphicsRootConstantBufferView(view_constants_param,
            view_input.view_constants->GetGPUVirtualAddress());
          current_raster_state = raster_state;
          pass_constants_bound = false;
        }
        if (!pass_constants_bound) {
          recorder->SetGraphicsRoot32BitConstant(
            root_constants_param, pass_constants_srvs[slice_index].get(), 1U);
          pass_constants_bound = true;
        }

        recorder->SetGraphicsRoot32BitConstant(
          root_constants_param, draw_command.draw_index, 0U);
        recorder->Draw(draw_command.index_count, draw_command.instance_count,
          0U, draw_command.start_instance);
        ++last_render_state_.rendered_draw_count;
      }

      ++last_render_state_.rendered_cascade_count;
    }

    recorder->RequireResourceStateFinal(
      *shadow_surface, graphics::ResourceStates::kShaderResource);
  }
  last_render_state_.recording_succeeded = local_map
    ? recorder.SubmitWithReceipt().outcome
      == graphics::SubmissionOutcome::kSubmitted
    : recorder.Submit();
  return last_render_state_;
}

} // namespace oxygen::vortex::shadows
