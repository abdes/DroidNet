//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

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
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Internal/StructuredGpuBuffer.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/CullingViewHistory.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex {

namespace {

  using occlusion::internal::CullingViewHistory;
  using occlusion::internal::DrawCullInputs;
  using occlusion::internal::DrawCullPass;
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

} // namespace

struct OcclusionModule::Impl {
  //! One camera view's culling state.
  struct ViewState {
    CullingViewHistory history { std::string { kDebugName } };
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

  //! Starts this frame's slot upload buffer.
  auto BeginSlotWords(const RenderContext& ctx)
    -> upload::TransientStructuredBuffer&
  {
    if (!slot_words.has_value()) {
      auto gfx = renderer->GetGraphics();
      slot_words.emplace(observer_ptr { gfx.get() },
        renderer->GetStagingProvider(), kSlotStride,
        observer_ptr { &renderer->GetInlineTransfersCoordinator() },
        std::string { kDebugName } + ".HistorySlots");
    }
    slot_words->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    return *slot_words;
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

  // A stateless view keeps nothing across frames: without history it culls by
  // frustum only and counts nothing.
  const auto view_id = ctx.current_view.view_id;
  const bool stateless = ctx.current_view.view_state_handle
    == CompositionView::kInvalidViewStateHandle;
  auto& view = impl_->views[view_id];
  const auto extent = scene_textures.GetExtent();
  const auto clamped = vortex::internal::ResolveClampedViewportState(
    resolved_view->Viewport(), resolved_view->Scissor(), extent.x, extent.y);
  const auto view_projection
    = resolved_view->ProjectionMatrix() * resolved_view->ViewMatrix();
  view.inputs = DrawCullInputs {
    .frame_sequence = ctx.frame_sequence,
    .frame_slot = ctx.frame_slot,
    .prepared_frame = ctx.current_view.prepared_frame,
    .view_projection = view_projection,
    .depth = occlusion::internal::DrawCullDepth::ForProjection(view_projection),
    .viewport = clamped.viewport,
    .scissors = clamped.scissors,
    // The box test assumes reversed-Z depth.
    .occlusion_enabled
    = config_.enabled && resolved_view->ReverseZ() && !stateless,
    .depth_bias = config_.depth_bias,
  };

  const auto key = MakeHistoryKey(resolved_view->StableProjectionMatrix(),
    clamped.viewport, clamped.scissors);
  const auto reset
    = view.history_key != key || ctx.current_view.history_discontinuity;
  view.history_key = key;

  if (view.inputs.occlusion_enabled) {
    const auto draw_count
      = static_cast<std::uint32_t>(prepared_frame->GetDrawMetadata().size());
    const auto history = view.history.Prepare(recorder,
      impl_->renderer->GetGraphics(), impl_->BeginSlotWords(ctx),
      prepared_frame->draw_sources, draw_count, reset);
    if (history.has_value()) {
      view.inputs.history = *history;
    } else {
      // Without history the view culls by frustum only this frame.
      view.inputs.occlusion_enabled = false;
    }
  } else {
    view.history.SetWritten(false);
  }

  view.counting = !stateless && impl_->PrepareCounters(view);
  if (view.counting) {
    view.inputs.history.stats = observer_ptr<const graphics::Buffer> {
      view.counters.GetBuffer(),
    };
    view.inputs.history.stats_uav = view.counters.GetUav();
  }

  view.phase1 = impl_->cull.RunPhase1(recorder, view.inputs);
  ctx.current_view.draw_visibility = view.phase1;
  if (stateless) {
    impl_->views.erase(view_id);
    return;
  }
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
  view.history.SetWritten(products.phase2);
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
