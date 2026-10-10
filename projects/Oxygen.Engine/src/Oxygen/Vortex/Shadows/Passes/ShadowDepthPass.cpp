//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/SubmissionCallback.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidTexture.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/CullingViewHistory.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/Shadows/Internal/SharedShadowMap.h>
#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex::shadows {

struct ShadowDepthPass::SurfaceViews {
  std::weak_ptr<graphics::Texture> surface;
  std::vector<graphics::NativeView> dsvs;
};

//! Two-phase culling state of the shadow slices.
/*!
 Each slice is a culling view keyed by its surface, target slice, light and
 allocation generation. Pyramids are transient: one per extent, reused by the
 slices in recording order.
*/
struct ShadowDepthPass::Culling {
  struct Key {
    const graphics::Texture* surface { nullptr };
    std::uint32_t target_slice { 0U };
    scene::NodeHandle light;
    std::uint32_t generation { 0U };

    auto operator==(const Key&) const -> bool = default;
  };

  struct KeyHash {
    auto operator()(const Key& key) const noexcept -> std::size_t
    {
      auto seed = std::hash<const void*> {}(key.surface);
      HashCombine(seed, key.target_slice);
      HashCombine(seed, std::hash<scene::NodeHandle> {}(key.light));
      HashCombine(seed, key.generation);
      return seed;
    }
  };

  //! Built in place in the map and never moved.
  struct HistoryEntry {
    explicit HistoryEntry(const frame::SequenceNumber used)
      : last_used(used)
    {
    }
    ~HistoryEntry() = default;

    OXYGEN_MAKE_NON_COPYABLE(HistoryEntry)
    OXYGEN_MAKE_NON_MOVABLE(HistoryEntry)

    occlusion::internal::CullingViewHistory history {
      "Vortex.Stage8.ShadowDepths.Occlusion"
    };
    frame::SequenceNumber last_used { 0U };
  };

  //! Frames a slice's history outlives its last use, so cached local maps
  //! that render again soon keep it.
  static constexpr std::uint64_t kHistoryLifetime = 240U;

  explicit Culling(Renderer& renderer)
    : cull(renderer, "Vortex.Stage8.ShadowDepths.Occlusion")
    , lists(renderer, "Vortex.Stage8.ShadowDepths.Lists")
    , pyramids(renderer, "Vortex.Stage8.ShadowDepths.Pyramid")
    , slot_words(observer_ptr { renderer.GetGraphics().get() },
        renderer.GetLightingStagingProvider(),
        static_cast<std::uint32_t>(sizeof(std::uint32_t)),
        observer_ptr { &renderer.GetInlineTransfersCoordinator() },
        "Vortex.Stage8.ShadowDepths.HistorySlots")
  {
  }

  occlusion::internal::DrawCullPass cull;
  occlusion::internal::IndirectListBuilder lists;
  HzbPyramidBuilder pyramids;
  upload::TransientStructuredBuffer slot_words;
  std::unordered_map<Key, HistoryEntry, KeyHash> histories;
  std::map<std::pair<std::uint32_t, std::uint32_t>,
    std::unique_ptr<HzbPyramidTexture>>
    pyramid_textures;
  frame::SequenceNumber sequence { 0U };

  auto OnFrameStart(const frame::SequenceNumber frame_sequence,
    const frame::Slot frame_slot) -> void
  {
    sequence = frame_sequence;
    slot_words.OnFrameStart(frame_sequence, frame_slot);
    std::erase_if(histories, [&](const auto& entry) -> bool {
      return entry.second.last_used.get() + kHistoryLifetime
        < frame_sequence.get();
    });
  }

  auto History(
    const graphics::Texture& surface, const ShadowDepthPass::DepthSlice& slice)
    -> occlusion::internal::CullingViewHistory&
  {
    const auto key = Key {
      .surface = &surface,
      .target_slice = slice.target_slice,
      .light = slice.light_source,
      .generation = slice.slot_generation,
    };
    auto& entry = histories.try_emplace(key, sequence).first->second;
    entry.last_used = sequence;
    return entry.history;
  }

  auto Pyramid(const std::uint32_t width, const std::uint32_t height)
    -> HzbPyramidTexture&
  {
    auto& pyramid = pyramid_textures[{ width, height }];
    if (pyramid == nullptr) {
      pyramid = std::make_unique<HzbPyramidTexture>(
        "Vortex.Stage8.ShadowDepths.Pyramid");
    }
    return *pyramid;
  }
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

  static_assert(sizeof(ShadowPassConstants) == 128U); // NOLINT(*-magic-numbers)
  static_assert(offsetof(ShadowPassConstants, light_view_projection) == 0U);
  static_assert(offsetof(ShadowPassConstants, shadow_bias_parameters)
    == 64U); // NOLINT(*-magic-numbers)
  static_assert(offsetof(ShadowPassConstants, light_direction_to_source)
    == 80U); // NOLINT(*-magic-numbers)
  static_assert(offsetof(ShadowPassConstants, light_position_and_inv_range)
    == 96U); // NOLINT(*-magic-numbers)
  static_assert(offsetof(ShadowPassConstants, draw_metadata_slot)
    == 112U); // NOLINT(*-magic-numbers)
  static_assert(offsetof(ShadowPassConstants, current_worlds_slot)
    == 116U); // NOLINT(*-magic-numbers)
  static_assert(offsetof(ShadowPassConstants, instance_data_slot)
    == 120U); // NOLINT(*-magic-numbers)
  static_assert(offsetof(ShadowPassConstants, normal_matrices_slot)
    == 124U); // NOLINT(*-magic-numbers)
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
        .color_target_formats = {},
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

  //! Every shadow-caster draw, in draw order; the GPU culls them per slice.
  auto MakeCasterCandidates(const PreparedSceneFrame& prepared_scene)
    -> std::vector<occlusion::internal::IndirectDrawCandidate>
  {
    const auto metadata = prepared_scene.GetDrawMetadata();
    auto candidates
      = std::vector<occlusion::internal::IndirectDrawCandidate> {};
    candidates.reserve(metadata.size());
    for (const auto& [position, draw] : std::views::enumerate(metadata)) {
      if (!draw.flags.IsSet(PassMaskBit::kShadowCaster)) {
        continue;
      }
      const auto draw_index = static_cast<std::uint32_t>(position);
      candidates.push_back(occlusion::internal::IndirectDrawCandidate {
        .draw_index = draw_index,
        .vertex_count
        = draw.is_indexed != 0U ? draw.index_count : draw.vertex_count,
        .instance_count = (std::max)(draw.instance_count, 1U),
        .raster_state
        = vortex::internal::ResolveMeshRasterState(metadata, draw_index),
      });
    }
    return candidates;
  }

  //! How the slice's depth is stored. Cube faces rasterize projected depth;
  //! other maps with a light range store linear axial depth
  //! (`DirectionalShadowDepth.hlsl`); cascades store orthographic depth.
  auto SliceDepth(
    const graphics::Texture& surface, const ShadowDepthPass::DepthSlice& slice)
    -> occlusion::internal::DrawCullDepth
  {
    const auto inverse_range = slice.light_position_and_inv_range.w;
    if (surface.GetDescriptor().texture_type != TextureType::kTextureCubeArray
      && inverse_range > 0.0F) {
      return occlusion::internal::DrawCullDepth::AxialLinear(inverse_range);
    }
    return occlusion::internal::DrawCullDepth::ForProjection(
      slice.light_view_projection);
  }

  auto EnsureDepthStencilViewForCascade(Graphics& gfx,
    std::vector<graphics::NativeView>& dsvs, graphics::Texture& shadow_surface,
    const std::uint32_t cascade_index) -> graphics::NativeView
  {
    if (dsvs.size() <= cascade_index) {
      dsvs.resize(cascade_index + 1U);
    }
    if (dsvs.at(cascade_index)->IsValid()) {
      return dsvs.at(cascade_index);
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
      dsvs.at(cascade_index) = existing;
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
    dsvs.at(cascade_index) = dsv;
    return dsv;
  }

} // namespace

ShadowDepthPass::ShadowDepthPass(Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetLightingStagingProvider(), kShadowPassConstantsStride,
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "ShadowService.ShadowPassConstants")
  , culling_(std::make_unique<Culling>(renderer))
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
  culling_->OnFrameStart(sequence, slot);
  std::erase_if(surface_views_,
    [](const auto& item) -> auto { return item.second->surface.expired(); });
}

auto ShadowDepthPass::Record(const PreparedViewShadowInput& view_input,
  const std::shared_ptr<graphics::Texture>& shadow_surface,
  const ShadowFrameData& frame_data, const glm::vec3& light_direction,
  const scene::NodeHandle light_source) -> RenderState
{
  auto depth_slices = std::vector<DepthSlice> {};
  depth_slices.reserve(frame_data.cascades.size());
  for (const auto& cascade : frame_data.cascades) {
    depth_slices.push_back(DepthSlice {
      .light_view_projection = cascade.light_view_projection,
      .shadow_bias_parameters = glm::vec4(cascade.depth_bias,
        cascade.depth_bias * kUeCsmShadowSlopeScaleDepthBias
          * kUeDefaultUserShadowSlopeBias,
        kUeShadowMaxSlopeScaleDepthBias, 0.0F),
      .light_direction_to_source = glm::vec4(light_direction, 0.0F),
      .target_slice = cascade.array_layer.get(),
      .light_source = light_source,
      .slot_generation = 0U,
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

  const auto candidates = view_input.prepared_scene != nullptr
    ? MakeCasterCandidates(*view_input.prepared_scene)
    : std::vector<occlusion::internal::IndirectDrawCandidate> {};
  last_render_state_.shadow_caster_draw_count
    = static_cast<std::uint32_t>(candidates.size());
  const auto has_draws = !candidates.empty();
  if (has_draws && view_input.view_constants == nullptr) {
    return last_render_state_;
  }

  auto pass_constants_srvs = std::vector<ShaderVisibleIndex>(
    depth_slices.size(), kInvalidShaderVisibleIndex);
  if (has_draws) {
    for (const auto& [slice_index, slice] :
      std::views::enumerate(depth_slices)) {
      auto& srv = pass_constants_srvs.at(static_cast<std::size_t>(slice_index));
      srv = PublishPassConstants(*view_input.prepared_scene, slice);
      if (!srv.IsValid()) {
        LOG_F(ERROR,
          "ShadowDepthPass: failed to allocate pass constants for shadow slice "
          "{}",
          slice_index);
        return last_render_state_;
      }
    }
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

  {
    graphics::GpuEventScope stage_scope(*recorder, "Vortex.Stage8.ShadowDepths",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    if (!local_map) {
      AdoptOrBeginPersistentState(*recorder, *shadow_surface);
    }

    const auto occlusion = has_draws && renderer_.GetOcclusionEnabled();
    for (const auto& [slice, pass_constants] :
      std::views::zip(depth_slices, pass_constants_srvs)) {
      const auto dsv = local_map
        ? local_map->slot->backing->dsvs.at(slice.target_slice)
        : EnsureDepthStencilViewForCascade(
            *gfx, views->dsvs, *shadow_surface, slice.target_slice);
      const auto target = SliceTarget {
        .surface
        = observer_ptr<const graphics::Texture> { shadow_surface.get() },
        .dsv = dsv,
        .pass_constants = pass_constants,
      };

      recorder->RequireResourceState(
        *shadow_surface, graphics::ResourceStates::kDepthWrite);
      recorder->FlushBarriers();
      recorder->SetRenderTargets({}, dsv);
      recorder->ClearDepthStencilView(
        *shadow_surface, dsv, graphics::ClearFlags::kDepth, 0.0F, 0U);
      if (has_draws) {
        last_render_state_.submitted_draw_count
          += static_cast<std::uint32_t>(candidates.size());
        RecordSliceDraws(*recorder, view_input, slice, target,
          std::span(candidates), occlusion);
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

auto ShadowDepthPass::PublishPassConstants(
  const PreparedSceneFrame& prepared_scene, const DepthSlice& slice)
  -> ShaderVisibleIndex
{
  const auto constants = ShadowPassConstants {
    .light_view_projection = slice.light_view_projection,
    .shadow_bias_parameters = slice.shadow_bias_parameters,
    .light_direction_to_source = slice.light_direction_to_source,
    .light_position_and_inv_range = slice.light_position_and_inv_range,
    .draw_metadata_slot = prepared_scene.bindless_draw_metadata_slot.get(),
    .current_worlds_slot = prepared_scene.bindless_worlds_slot.get(),
    .instance_data_slot = prepared_scene.bindless_instance_data_slot.get(),
    .normal_matrices_slot = prepared_scene.bindless_normals_slot.get(),
  };
  auto allocation = pass_constants_buffer_.Allocate(1U);
  if (!allocation.has_value() || !allocation->IsValid(current_sequence_)
    || !allocation->TryWriteObject(constants)) {
    return kInvalidShaderVisibleIndex;
  }
  return allocation->srv;
}

auto ShadowDepthPass::RecordSliceDraws(graphics::CommandRecorder& recorder,
  const PreparedViewShadowInput& view_input, const DepthSlice& slice,
  const SliceTarget& target,
  const std::span<const occlusion::internal::IndirectDrawCandidate> candidates,
  const bool occlusion) -> void
{
  const auto& prepared_scene = *view_input.prepared_scene;
  const auto& surface = *target.surface;
  const auto& desc = surface.GetDescriptor();
  const auto viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(desc.width),
    .height = static_cast<float>(desc.height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  const auto scissors = Scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<std::int32_t>(desc.width),
    .bottom = static_cast<std::int32_t>(desc.height),
  };

  auto inputs = occlusion::internal::DrawCullInputs {
    .frame_sequence = current_sequence_,
    .frame_slot = current_slot_,
    .prepared_frame = view_input.prepared_scene,
    .view_projection = slice.light_view_projection,
    .depth = SliceDepth(surface, slice),
    .viewport = viewport,
    .scissors = scissors,
    .occlusion_enabled = occlusion,
    .depth_bias = kDefaultOcclusionDepthBias,
  };
  auto* history = occlusion ? &culling_->History(surface, slice) : nullptr;
  if (history != nullptr) {
    const auto binding = history->Prepare(recorder, renderer_.GetGraphics(),
      culling_->slot_words, prepared_scene.draw_sources,
      static_cast<std::uint32_t>(prepared_scene.GetDrawMetadata().size()),
      false);
    if (binding.has_value()) {
      inputs.history = *binding;
    } else {
      inputs.occlusion_enabled = false;
      history = nullptr;
    }
  }

  const auto draw = [&](const DrawVisibilityProducts& visibility,
                      const DrawVisibilityBit phase) -> void {
    const auto list = culling_->lists.Build(recorder, current_sequence_,
      current_slot_, candidates, visibility, DrawVisibilityPredicate { phase });
    recorder.RequireResourceState(
      surface, graphics::ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
    recorder.SetRenderTargets({}, target.dsv);
    recorder.SetViewport(viewport);
    recorder.SetScissors(scissors);
    occlusion::internal::IndirectListBuilder::Draw(recorder, list,
      [&](const vortex::internal::MeshRasterState& raster_state) -> void {
        recorder.SetPipelineState(
          BuildShadowPipelineDesc(surface, raster_state));
        recorder.SetGraphicsRootConstantBufferView(
          static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
          view_input.view_constants->GetGPUVirtualAddress());
        recorder.SetGraphicsRoot32BitConstant(
          static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
          target.pass_constants.get(), 1U);
      });
  };

  const auto phase1 = culling_->cull.RunPhase1(recorder, inputs);
  draw(phase1, DrawVisibilityBit::kPhase1Drawn);
  if (history == nullptr || !phase1.IsValid()) {
    return;
  }

  auto& pyramid = culling_->Pyramid(desc.width, desc.height);
  auto binding = std::optional<occlusion::internal::OcclusionPyramidBinding> {};
  if (pyramid.Ensure(renderer_.GetGraphics(), desc.width, desc.height)
    && culling_->pyramids.Build(
      HzbPyramidBuilder::BuildFrame {
        .sequence = current_sequence_,
        .slot = current_slot_,
        .view_id = view_input.view_id,
      },
      recorder,
      HzbPyramidBuilder::Source {
        .depth = observer_ptr<const graphics::Texture> { &surface },
        .array_slice = slice.target_slice,
        .origin_x = 0U,
        .origin_y = 0U,
        .width = desc.width,
        .height = desc.height,
      },
      HzbPyramidBuilder::Targets {
        .closest = nullptr,
        .furthest = observer_ptr { pyramid.GetTexture().get() },
      })) {
    binding = occlusion::internal::OcclusionPyramidBinding {
      .texture = observer_ptr<const graphics::Texture> {
        pyramid.GetTexture().get(),
      },
      .srv = pyramid.GetSrv(),
      .origin_x = 0U,
      .origin_y = 0U,
      .width = desc.width,
      .height = desc.height,
    };
  }
  const auto phase2
    = culling_->cull.RunPhase2(recorder, inputs, phase1, binding);
  history->SetWritten(phase2.phase2);
  draw(phase2, DrawVisibilityBit::kPhase2Drawn);
}

} // namespace oxygen::vortex::shadows
