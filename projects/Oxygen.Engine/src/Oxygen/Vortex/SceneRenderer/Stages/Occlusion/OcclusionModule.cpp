//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Vortex/Internal/StructuredGpuBuffer.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex {

namespace {

  using occlusion::internal::DrawCullInputs;
  using occlusion::internal::DrawCullPass;
  using occlusion::internal::HistorySlotAllocator;
  using vortex::internal::StructuredGpuBuffer;

  constexpr auto kDebugName = "Vortex.Stage3.Occlusion";
  constexpr auto kSlotStride
    = static_cast<std::uint32_t>(sizeof(std::uint32_t));
  constexpr auto kCounterCount = static_cast<std::uint32_t>(
    sizeof(OcclusionCounters) / sizeof(std::uint32_t));

  //! What a view's history is valid for: the unjittered projection and the
  //! rasterized rect. A change resets the history.
  struct HistoryKey {
    glm::mat4 projection { 1.0F };
    //! Viewport origin and size.
    glm::vec4 viewport { 0.0F };
    //! Scissor left, top, right, bottom.
    glm::ivec4 scissors { 0 };

    auto operator==(const HistoryKey&) const -> bool = default;
  };

  auto MakeHistoryKey(const glm::mat4& projection, const ViewPort& viewport,
    const Scissors& scissors) -> HistoryKey
  {
    return HistoryKey {
      .projection = projection,
      .viewport = glm::vec4 { viewport.top_left_x, viewport.top_left_y,
        viewport.width, viewport.height },
      .scissors = glm::ivec4 { scissors.left, scissors.top, scissors.right,
        scissors.bottom },
    };
  }

  auto TrackFromKnownOrCommon(
    graphics::CommandRecorder& recorder, const graphics::Buffer& buffer) -> void
  {
    if (recorder.IsResourceTracked(buffer)
      || recorder.AdoptKnownResourceState(buffer)) {
      return;
    }
    recorder.BeginTrackingResourceState(
      buffer, graphics::ResourceStates::kCommon, true);
  }

} // namespace

struct OcclusionModule::Impl {
  //! One camera view's culling state.
  struct ViewState {
    HistorySlotAllocator slots;
    //! One `uint` per slot; replaced, with its contents copied, on growth.
    std::unique_ptr<StructuredGpuBuffer> history;
    StructuredGpuBuffer counters {
      std::string { kDebugName } + ".Counters",
      kSlotStride,
    };
    std::shared_ptr<graphics::GpuBufferReadback> counters_readback;
    //! The frame and setting of the counters in flight.
    frame::SequenceNumber counters_frame { 0U };
    bool counters_occlusion_enabled { false };
    std::optional<OcclusionStats> stats;
    std::optional<HistoryKey> history_key;
    //! The last phase 2 wrote the history for `history_key`.
    bool history_written { false };

    //! This frame's phase 1, kept for phase 2.
    DrawCullInputs inputs {};
    DrawVisibilityProducts phase1 {};
    bool counting { false };
  };

  explicit Impl(Renderer& renderer_in)
    : renderer(&renderer_in)
    , cull(renderer_in, kDebugName)
  {
  }

  observer_ptr<Renderer> renderer;
  DrawCullPass cull;
  std::optional<upload::TransientStructuredBuffer> slot_words;
  std::unordered_map<ViewId, ViewState> views;
  std::vector<std::uint32_t> slot_word_storage;

  //! Uploads this frame's slot word per draw; invalid when the upload fails.
  auto UploadSlotWords(const RenderContext& ctx,
    const std::span<const HistorySlotAllocator::Assignment> assignments,
    const std::uint32_t draw_count) -> ShaderVisibleIndex
  {
    if (!slot_words.has_value()) {
      auto gfx = renderer->GetGraphics();
      slot_words.emplace(observer_ptr { gfx.get() },
        renderer->GetStagingProvider(), kSlotStride,
        observer_ptr { &renderer->GetInlineTransfersCoordinator() },
        std::string { kDebugName } + ".HistorySlots");
    }
    slot_word_storage.assign(draw_count, occlusion::internal::kNoHistorySlot);
    for (const auto& [word, assignment] :
      std::views::zip(slot_word_storage, assignments)) {
      word = assignment.ToSlotWord();
    }
    slot_words->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    const auto allocation = slot_words->Allocate(draw_count);
    if (!allocation.has_value()
      || !allocation->TryWriteRange(
        std::span<const std::uint32_t>(slot_word_storage))) {
      LOG_F(ERROR, "{}: failed to upload the history slots", kDebugName);
      return kInvalidShaderVisibleIndex;
    }
    return allocation->srv;
  }

  //! Grows the view's history to the slot capacity, copying its contents.
  auto EnsureHistory(graphics::CommandRecorder& recorder, ViewState& view) const
    -> bool
  {
    const auto capacity = view.slots.Capacity();
    if (view.history != nullptr && view.history->GetCapacity() >= capacity) {
      return true;
    }
    auto gfx = renderer->GetGraphics();
    auto grown = std::make_unique<StructuredGpuBuffer>(
      std::string { kDebugName } + ".History", kSlotStride);
    if (!grown->Ensure(gfx, std::bit_ceil((std::max)(capacity, 64U)))) {
      return false;
    }
    if (view.history != nullptr) {
      const auto& source = *view.history->GetBuffer();
      auto& target = *grown->GetBuffer();
      TrackFromKnownOrCommon(recorder, source);
      TrackFromKnownOrCommon(recorder, target);
      recorder.RequireResourceState(
        source, graphics::ResourceStates::kCopySource);
      recorder.RequireResourceState(
        target, graphics::ResourceStates::kCopyDest);
      recorder.FlushBarriers();
      recorder.CopyBuffer(target, 0U, source, 0U,
        static_cast<std::size_t>(view.history->GetCapacity()) * kSlotStride);
    }
    view.history = std::move(grown);
    return true;
  }

  //! Takes the counters of an earlier frame when their readback is done.
  static auto CollectCounters(ViewState& view) -> void
  {
    if (view.counters_readback == nullptr) {
      return;
    }
    const auto ready = view.counters_readback->IsReady();
    if (!ready.has_value()) {
      view.counters_readback->Reset();
      return;
    }
    if (!*ready) {
      return;
    }
    if (const auto mapped = view.counters_readback->TryMap(); mapped.has_value()
      && mapped->Bytes().size() >= sizeof(OcclusionCounters)) {
      auto counters = OcclusionCounters {};
      std::memcpy(&counters, mapped->Bytes().data(), sizeof(counters));
      view.stats = OcclusionStats {
        .counters = counters,
        .frame_sequence = view.counters_frame,
        .occlusion_enabled = view.counters_occlusion_enabled,
      };
    }
    view.counters_readback->Reset();
  }

  //! Whether this frame can count: the previous readback has been taken.
  auto PrepareCounters(ViewState& view) const -> bool
  {
    CollectCounters(view);
    if (view.counters_readback != nullptr
      && view.counters_readback->GetState() != graphics::ReadbackState::kIdle) {
      return false;
    }
    auto gfx = renderer->GetGraphics();
    if (view.counters_readback == nullptr) {
      auto manager = gfx->GetReadbackManager();
      if (manager == nullptr) {
        return false;
      }
      view.counters_readback = manager->CreateBufferReadback(
        std::string { kDebugName } + ".Counters");
    }
    return view.counters_readback != nullptr
      && view.counters.Ensure(gfx, kCounterCount);
  }

  //! Reads back the counters of the phases recorded this frame.
  static auto EnqueueCounters(graphics::CommandRecorder& recorder,
    ViewState& view, const RenderContext& ctx) -> void
  {
    if (!view.counting) {
      return;
    }
    view.counting = false;
    const auto ticket = view.counters_readback->EnqueueCopy(recorder,
      *view.counters.GetBuffer(),
      graphics::BufferRange { 0U, sizeof(OcclusionCounters) });
    if (!ticket.has_value()) {
      LOG_F(WARNING, "{}: counter readback failed", kDebugName);
      return;
    }
    view.counters_frame = ctx.frame_sequence;
    view.counters_occlusion_enabled = view.inputs.occlusion_enabled;
  }
};

OcclusionModule::OcclusionModule(Renderer& renderer, OcclusionConfig config)
  : config_(config)
  , impl_(std::make_unique<Impl>(renderer))
{
}

OcclusionModule::~OcclusionModule() = default;

void OcclusionModule::SetConfig(OcclusionConfig config) noexcept
{
  config_ = config;
}

auto OcclusionModule::GetConfig() const noexcept -> const OcclusionConfig&
{
  return config_;
}

void OcclusionModule::BuildPhase1(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& scene_textures)
{
  ctx.current_view.draw_visibility = {};
  const auto* prepared_frame = ctx.current_view.prepared_frame.get();
  const auto* resolved_view = ctx.current_view.resolved_view.get();
  if (prepared_frame == nullptr || resolved_view == nullptr
    || impl_->renderer->GetGraphics() == nullptr) {
    return;
  }

  auto& view = impl_->views[ctx.current_view.view_id];
  const auto extent = scene_textures.GetExtent();
  const auto clamped = vortex::internal::ResolveClampedViewportState(
    resolved_view->Viewport(), resolved_view->Scissor(), extent.x, extent.y);
  view.inputs = DrawCullInputs {
    .frame_sequence = ctx.frame_sequence,
    .frame_slot = ctx.frame_slot,
    .prepared_frame = ctx.current_view.prepared_frame,
    .view_matrix = resolved_view->ViewMatrix(),
    .projection_matrix = resolved_view->ProjectionMatrix(),
    .viewport = clamped.viewport,
    .scissors = clamped.scissors,
    // The box test assumes reversed-Z depth.
    .occlusion_enabled = config_.enabled && resolved_view->ReverseZ(),
    .depth_bias = config_.depth_bias,
  };

  const auto key = MakeHistoryKey(resolved_view->StableProjectionMatrix(),
    clamped.viewport, clamped.scissors);
  const auto history_valid = view.history_written && view.history_key == key
    && !ctx.current_view.history_discontinuity;
  view.history_key = key;
  view.history_written = false;

  if (view.inputs.occlusion_enabled) {
    const auto draw_count
      = static_cast<std::uint32_t>(prepared_frame->GetDrawMetadata().size());
    const auto assignments = view.slots.Update(prepared_frame->draw_sources);
    const auto slots_srv = impl_->UploadSlotWords(ctx, assignments, draw_count);
    if (slots_srv.IsValid() && impl_->EnsureHistory(recorder, view)) {
      view.inputs.history = occlusion::internal::DrawCullHistory {
        .slots_srv = slots_srv,
        .history = observer_ptr<const graphics::Buffer> {
          view.history->GetBuffer(),
        },
        .history_uav = view.history->GetUav(),
        .valid = history_valid,
        .stats = nullptr,
        .stats_uav = kInvalidShaderVisibleIndex,
      };
    } else {
      // Without history the view culls by frustum only this frame.
      view.inputs.occlusion_enabled = false;
    }
  }

  view.counting = impl_->PrepareCounters(view);
  if (view.counting) {
    view.inputs.history.stats = observer_ptr<const graphics::Buffer> {
      view.counters.GetBuffer(),
    };
    view.inputs.history.stats_uav = view.counters.GetUav();
  }

  view.phase1 = impl_->cull.RunPhase1(recorder, view.inputs);
  ctx.current_view.draw_visibility = view.phase1;
  if (!view.phase1.IsValid()) {
    view.counting = false;
    return;
  }
  if (!view.inputs.occlusion_enabled) {
    Impl::EnqueueCounters(recorder, view, ctx);
  }
}

auto OcclusionModule::NeedsPhase2(const RenderContext& ctx) const -> bool
{
  const auto found = impl_->views.find(ctx.current_view.view_id);
  return found != impl_->views.end()
    && found->second.inputs.frame_sequence == ctx.frame_sequence
    && found->second.inputs.occlusion_enabled && found->second.phase1.IsValid();
}

void OcclusionModule::BuildPhase2(RenderContext& ctx,
  graphics::CommandRecorder& recorder,
  const std::optional<ScreenHzbModule::OcclusionPyramid>& pyramid)
{
  if (!NeedsPhase2(ctx)) {
    return;
  }
  auto& view = impl_->views.at(ctx.current_view.view_id);
  auto binding = std::optional<occlusion::internal::OcclusionPyramidBinding> {};
  if (pyramid.has_value()) {
    binding = occlusion::internal::OcclusionPyramidBinding {
      .texture = observer_ptr { pyramid->texture.get() },
      .srv = pyramid->srv,
      .origin_x = pyramid->source.origin_x,
      .origin_y = pyramid->source.origin_y,
      .width = pyramid->source.width,
      .height = pyramid->source.height,
    };
  }
  const auto products
    = impl_->cull.RunPhase2(recorder, view.inputs, view.phase1, binding);
  ctx.current_view.draw_visibility = products;
  view.history_written = products.phase2;
  Impl::EnqueueCounters(recorder, view, ctx);
}

void OcclusionModule::RemoveViewState(const ViewId view_id)
{
  impl_->views.erase(view_id);
}

auto OcclusionModule::GetStats(const ViewId view_id) const
  -> std::optional<OcclusionStats>
{
  const auto found = impl_->views.find(view_id);
  if (found == impl_->views.end()) {
    return std::nullopt;
  }
  return found->second.stats;
}

} // namespace oxygen::vortex
