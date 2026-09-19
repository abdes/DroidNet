//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cmath>
#include <cstring>

#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureAllocationScenario.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

auto ExposureAllocationScenario::RenderFrame(unsigned view_count,
  unsigned layout, float ev, const std::string& phase) -> void
{
  backend_->accounting_phase = phase;
  ++backend_->accounting_iteration;
  const auto slot
    = frame::Slot { fixture_.sequence % frame::kFramesInFlight.get() };
  fixture_.Backend().BeginFrame(
    frame::SequenceNumber { ++fixture_.sequence }, slot);
  fixture_.frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
  fixture_.frame.SetFrameSequenceNumber(
    frame::SequenceNumber { fixture_.sequence },
    engine::internal::EngineTagFactory::Get());
  fixture_.renderer_->OnFrameStart(observer_ptr { &fixture_.frame });
  for (unsigned index = 0U; index < view_count; ++index) {
    const auto output_index = index ^ layout;
    auto sized_view = fixture_.view;
    sized_view.viewport.width = float(width_ >> output_index);
    sized_view.viewport.height = float(height_ >> output_index);
    auto input = CompositionView::ForScene(
      ViewId { 500U + index }, sized_view, fixture_.camera);
    input.view_state_handle = CompositionView::ViewStateHandle { 500U + index };
    auto exposure = fixture_.settings;
    exposure.manual_ev = ev;
    input.render_settings.exposure = exposure;
    ASSERT_NE(
      fixture_.renderer_->PublishRuntimeCompositionView(fixture_.frame,
        { .composition_view = input,
          .render_target = observer_ptr { targets_[output_index].get() },
          .composite_source = observer_ptr { targets_[output_index].get() } }),
      kInvalidViewId);
  }
  if (view_count != 0U) {
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await fixture_.renderer_->OnPreRender(
        observer_ptr { &fixture_.frame });
      co_await fixture_.renderer_->OnRender(observer_ptr { &fixture_.frame });
    });
  }
  fixture_.renderer_->OnFrameEnd(observer_ptr { &fixture_.frame });
  fixture_.Backend().EndFrame(
    frame::SequenceNumber { fixture_.sequence }, slot);
  fixture_.WaitForQueueIdle();
}

auto ExposureAllocationScenario::RemoveViews() -> void
{
  for (unsigned index = 0U; index < 2U; ++index) {
    fixture_.renderer_->RemovePublishedRuntimeView(
      fixture_.frame, ViewId { 500U + index });
  }
  current_.clear();
  fixture_.probe->color.reset();
  fixture_.probe->exposure.reset();
}

auto ExposureAllocationScenario::RunLifecycle() -> void
{
  static_cast<void>(Snapshot("cold_fixture"));
  ASSERT_NO_FATAL_FAILURE(RenderFrame(1U, 0U, 0.0F, "bootstrap_one"));
  static_cast<void>(Snapshot("bootstrap_one"));
  for (unsigned warm = 1U; warm < 8U; ++warm) {
    ASSERT_NO_FATAL_FAILURE(RenderFrame(1U, 0U, 0.0F, "qualify_one"));
  }
  static_cast<void>(Snapshot("steady_one"));
  for (unsigned cycle = 0U; cycle < 3U; ++cycle) {
    SCOPED_TRACE(cycle);
    const auto prefix = "cycle" + std::to_string(cycle) + "_";
    auto observation = CycleObservation {};
    ASSERT_NO_FATAL_FAILURE(QualifyAndRetain(prefix, observation));
    ASSERT_NO_FATAL_FAILURE(ResizeAndCapture(prefix, observation));
    ASSERT_NO_FATAL_FAILURE(QueueConsumers(prefix, observation));
    ASSERT_NO_FATAL_FAILURE(RemoveAndReadd(prefix, observation));
    ASSERT_NO_FATAL_FAILURE(ReleaseAndSubmit(prefix, observation));
    ASSERT_NO_FATAL_FAILURE(VerifyConsumers(cycle, observation));
    ASSERT_NO_FATAL_FAILURE(RetireCycle(cycle, prefix, observation));
  }
}

auto ExposureAllocationScenario::QualifyAndRetain(
  const std::string& prefix, CycleObservation& observation) -> void
{
  for (unsigned warm = 0U; warm < 8U; ++warm) {
    ASSERT_NO_FATAL_FAILURE(RenderFrame(2U, 0U, 0.0F, prefix + "qualify_two"));
  }
  for (unsigned index = 0U; index < 2U; ++index) {
    const auto handle = CompositionView::ViewStateHandle { 500U + index };
    ASSERT_TRUE(current_.contains(handle));
    retained_[index] = current_.at(handle);
    ASSERT_GT(retained_[index].draws, 0U);
    ASSERT_TRUE(retained_[index].color.valid);
    ASSERT_NE(retained_[index].color.retained_texture, nullptr);
    for (const auto& depth : retained_[index].depths) {
      ASSERT_TRUE(depth.valid);
      ASSERT_NE(depth.texture, nullptr);
      ASSERT_NE(depth.retained_texture, nullptr);
    }
    ASSERT_EQ(
      retained_[index].depths[0].texture, retained_[index].depths[1].texture);
    EXPECT_FALSE(retained_[index].depths[0].retained_texture.owner_before(
      retained_[index].depths[1].retained_texture));
    EXPECT_FALSE(retained_[index].depths[1].retained_texture.owner_before(
      retained_[index].depths[0].retained_texture));
    if (fp32_reference_) {
      EXPECT_EQ(retained_[index].color.texture->GetDescriptor().format,
        Format::kRGBA32Float);
    } else if (!temporal_) {
      EXPECT_EQ(retained_[index].color.texture->GetDescriptor().format,
        Format::kRGBA16Float);
    }
    ASSERT_TRUE(fixture_.renderer_
        ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
        .has_value());
  }
  backend_->accounting_phase = prefix + "depth_reference";
  for (unsigned index = 0U; index < 2U; ++index) {
    const auto& depth = *retained_[index].depths[0].texture;
    ASSERT_NO_FATAL_FAILURE(EnsureDepthReadback(index, depth));
    {
      auto recorder
        = fixture_.AcquireRecorder("Lifecycle immutable depth reference");
      CopyDepth(*recorder, depth, index, 0U);
      recorder->RequireResourceStateFinal(
        depth, ResourceStates::kShaderResource);
    }
  }
  fixture_.WaitForQueueIdle();
  for (unsigned index = 0U; index < 2U; ++index) {
    observation.reference_depth[index] = DepthSample(index, 0U);
    const auto value = std::bit_cast<float>(observation.reference_depth[index]);
    ASSERT_TRUE(std::isfinite(value));
    ASSERT_GT(value, 0.0F);
    ASSERT_LT(value, 1.0F);
  }
}

auto ExposureAllocationScenario::ResizeAndCapture(
  const std::string& prefix, CycleObservation& observation) -> void
{
  static_cast<void>(Snapshot(prefix + "steady_two_retained"));
  ASSERT_NO_FATAL_FAILURE(RenderFrame(2U, 0U, 1.0F, prefix + "recovery"));
  static_cast<void>(Snapshot(prefix + "recovery_two_retained"));
  for (unsigned warm = 0U; warm < 8U; ++warm) {
    ASSERT_NO_FATAL_FAILURE(RenderFrame(2U, 1U, 1.0F, prefix + "resized"));
  }
  static_cast<void>(Snapshot(prefix + "resized_two_retained"));
  WithoutDiagnostics([&] {
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto& source = retained_[index].color;
      observation.saved_states[index] = fixture_.Read<ExposureStateData>(
        *source.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
      observation.saved_domains[index] = fixture_.Read<FrameExposureData>(
        *source.exposure->buffer, ResourceStates::kShaderResource);
      observation.saved_lifetimes[index]
        = source.exposure->current_state->owner_lifetime;
      observation.wrapper_lifetimes[index] = source.retained_texture;
      observation.resource_lifetimes[index]
        = source.texture->shared_from_this();
      observation.depth_wrapper_lifetimes[index]
        = retained_[index].depths[0].retained_texture;
      observation.depth_resource_lifetimes[index]
        = retained_[index].depths[0].texture->shared_from_this();
    }
  });
}

auto ExposureAllocationScenario::QueueConsumers(
  const std::string& prefix, CycleObservation& observation) -> void
{
  backend_->accounting_phase = prefix + "queued_consumers";
  backend_->deferred_tonemap_recordings.clear();
  {
    backend_->defer_tonemap_recorders = true;
    auto restore = ScopeGuard(
      [&]() noexcept { backend_->defer_tonemap_recorders = false; });
    auto consume_context = RenderContext {};
    consume_context.frame_sequence
      = frame::SequenceNumber { fixture_.sequence };
    consume_context.frame_slot = fixture_.frame.GetFrameSlot();
    auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
      *fixture_.renderer_);
    ASSERT_NE(owner, nullptr);
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto& source = retained_[index].color;
      const auto& exposure = source.exposure;
      consume_context.current_view.view_id = ViewId { 900U + index };
      ASSERT_TRUE(consumer_
          ->Record(consume_context, owner->GetSceneTextures(),
            { .scene_signal = source.texture,
              .exposure_buffer = exposure->current_state->buffer.get(),
              .frame_exposure_buffer = exposure->buffer.get(),
              .scene_signal_srv = Srv(*source.texture),
              .exposure_buffer_srv = exposure->current_state->srv_index,
              .frame_exposure_srv = exposure->srv_index,
              .post_target
              = observer_ptr<const Framebuffer> { consumer_targets_[index]
                  .get() },
              .tone_mapper = engine::ToneMapper::kNone,
              .gamma = 1.0F,
              .scene_fallback = source.fallback,
              .scene_fallback_srv = source.fallback
                ? Srv(*source.fallback)
                : kInvalidShaderVisibleIndex,
              .conversion_report
              = source.fallback ? exposure->conversion_buffer.get() : nullptr,
              .conversion_report_srv = source.fallback
                ? exposure->conversion_srv
                : kInvalidShaderVisibleIndex })
          .executed);
    }
  }
  ASSERT_EQ(backend_->deferred_tonemap_recordings.size(), 2U);
  {
    auto recorder
      = fixture_.AcquireDeferredRecorder("Lifecycle retained depth aliases");
    for (unsigned index = 0U; index < 2U; ++index) {
      for (unsigned alias = 0U; alias < 2U; ++alias) {
        CopyDepth(
          *recorder, *retained_[index].depths[alias].texture, index, alias);
      }
      recorder->RequireResourceStateFinal(
        *retained_[index].depths[0].texture, ResourceStates::kShaderResource);
    }
    observation.depth_recording = recorder->GetCommandListForInspection();
  }
  ASSERT_NE(observation.depth_recording, nullptr);
  ASSERT_FALSE(observation.depth_recording->IsSubmitted());
  for (const auto& recording : backend_->deferred_tonemap_recordings) {
    ASSERT_FALSE(recording->IsSubmitted());
  }
}

auto ExposureAllocationScenario::RemoveAndReadd(
  const std::string& prefix, CycleObservation& observation) -> void
{
  RemoveViews();
  static_cast<void>(Snapshot(prefix + "removed_retained_pending"));
  ASSERT_NO_FATAL_FAILURE(RenderFrame(2U, 1U, 2.0F, prefix + "readded"));
  ASSERT_FALSE(observation.depth_recording->IsSubmitted());
  for (const auto& recording : backend_->deferred_tonemap_recordings) {
    ASSERT_FALSE(recording->IsSubmitted());
  }
  for (unsigned index = 0U; index < 2U; ++index) {
    const auto handle = CompositionView::ViewStateHandle { 500U + index };
    ASSERT_TRUE(current_.contains(handle));
    EXPECT_NE(current_.at(handle).color.exposure->current_state->owner_lifetime,
      observation.saved_lifetimes[index]);
  }
  WithoutDiagnostics([&] {
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto& source = retained_[index].color;
      const auto state = fixture_.Read<ExposureStateData>(
        *source.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
      const auto domain = fixture_.Read<FrameExposureData>(
        *source.exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(
        std::memcmp(&state, &observation.saved_states[index], sizeof(state)),
        0);
      EXPECT_EQ(
        std::memcmp(&domain, &observation.saved_domains[index], sizeof(domain)),
        0);
    }
  });
  static_cast<void>(Snapshot(prefix + "readded_retained_pending"));
}

auto ExposureAllocationScenario::ReleaseAndSubmit(
  const std::string& prefix, CycleObservation& observation) -> void
{
  const auto completion = fixture_.SignalQueue();
  {
    auto recorder = fixture_.AcquireDeferredRecorder(
      "Lifecycle retained consumer completion");
    recorder->RecordQueueSignal(completion.get());
  }
  EXPECT_LT(fixture_.GetQueue()->GetCompletedValue(), completion.get());
  retained_ = {};
  for (unsigned index = 0U; index < 2U; ++index) {
    EXPECT_TRUE(observation.wrapper_lifetimes[index].expired());
    EXPECT_FALSE(observation.resource_lifetimes[index].expired());
    EXPECT_TRUE(observation.depth_wrapper_lifetimes[index].expired());
    EXPECT_FALSE(observation.depth_resource_lifetimes[index].expired());
  }
  auto pending_snapshot = backend_->MeasureTrackedPlacement();
  pending_snapshot["phase"] = prefix + "released_queued_before_submission";
  pending_snapshot["sequence"] = fixture_.sequence;
  pending_snapshot["slot"] = fixture_.frame.GetFrameSlot().get();
  pending_snapshot["retained_extracts"] = 0U;
  pending_snapshot["retained_depth_aliases"] = 0U;
  phases_.push_back(std::move(pending_snapshot));
  EXPECT_LT(fixture_.GetQueue()->GetCompletedValue(), completion.get());
  fixture_.Backend().SubmitDeferredCommandLists();
  ASSERT_TRUE(observation.depth_recording->IsSubmitted());
  for (const auto& recording : backend_->deferred_tonemap_recordings) {
    ASSERT_TRUE(recording->IsSubmitted());
  }
  fixture_.WaitForQueue(completion);
  fixture_.WaitForQueueIdle();
}

auto ExposureAllocationScenario::VerifyConsumers(
  const unsigned cycle, CycleObservation& observation) -> void
{
  for (unsigned index = 0U; index < 2U; ++index) {
    for (unsigned alias = 0U; alias < 2U; ++alias) {
      const auto actual = DepthSample(index, alias);
      EXPECT_EQ(actual, observation.reference_depth[index]);
      depth_results_.push_back(
        { { "cycle", cycle }, { "view", index }, { "alias", alias },
          { "reference_bits", observation.reference_depth[index] },
          { "actual_bits", actual },
          { "width", depth_readbacks_[index].footprint.Footprint.Width },
          { "height", depth_readbacks_[index].footprint.Footprint.Height } });
    }
  }
  observation.depth_recording.reset();
  WithoutDiagnostics([&] {
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto pixel = fixture_.ReadFloatTexture(*consumer_outputs_[index]);
      ASSERT_EQ(pixel.size(), 1U);
      // A 1x1 output samples the source center. Ordered dithering uses that
      // source pixel, so the half-size view can have a different Bayer rank.
      // Construct the 4x4 rank independently from its interleaved bit pairs.
      const auto sample_x = (width_ >> index) / 2U;
      const auto sample_y = (height_ >> index) / 2U;
      const auto bayer_rank = 8U * ((sample_x ^ sample_y) & 1U)
        + 4U * (sample_y & 1U) + 2U * (((sample_x ^ sample_y) >> 1U) & 1U)
        + ((sample_y >> 1U) & 1U);
      const double expected = .25 + (double(bayer_rank) / 16.0 - .5) / 255.0;
      EXPECT_EQ(observation.saved_states[index].displayed_scale, 1.0F);
      ASSERT_GT(observation.saved_domains[index].pre_exposure, 0.0F);
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(pixel.front()[channel], expected, 2e-5);
      }
      EXPECT_EQ(pixel.front()[3], 1.0F);
      consumer_results_.push_back({ { "cycle", cycle }, { "view", index },
        { "expected", expected }, { "actual", pixel.front() },
        { "source_pixel", { sample_x, sample_y } },
        { "bayer_rank", bayer_rank },
        { "P", observation.saved_domains[index].pre_exposure },
        { "gain", observation.saved_states[index].displayed_scale },
        { "retired_lifetime", observation.saved_lifetimes[index] },
        { "requested_generation",
          observation.saved_states[index].requested_generation },
        { "applied_generation",
          observation.saved_states[index].applied_generation } });
    }
  });
}

auto ExposureAllocationScenario::RetireCycle(const unsigned cycle,
  const std::string& prefix, CycleObservation& observation) -> void
{
  backend_->deferred_tonemap_recordings.clear();
  RemoveViews();
  static_cast<void>(Snapshot(prefix + "released_before_fence"));
  for (unsigned drain = 0U; drain < 2U * frame::kFramesInFlight.get();
    ++drain) {
    ASSERT_NO_FATAL_FAILURE(RenderFrame(0U, 0U, 0.0F, prefix + "retirement"));
  }
  for (const auto& lifetime : observation.resource_lifetimes) {
    EXPECT_TRUE(lifetime.expired());
  }
  for (const auto& lifetime : observation.depth_resource_lifetimes) {
    EXPECT_TRUE(lifetime.expired());
  }
  const auto retired = Snapshot(prefix + "retired_cached");
  EXPECT_EQ(retired.at("leased_families").get<std::size_t>(), 0U);
  const auto signature = Population(retired);
  if (cycle != 0U) {
    EXPECT_EQ(signature, retired_population_)
      << "Repeated visits to the same descriptors must return to the same "
         "cached population";
  }
  retired_population_ = signature;
}

auto ExposureAllocationScenario::WriteReport() -> void
{
  backend_->account_texture_allocations = false;
  backend_->track_resources = false;
  fixture_.probe->inspect = {};
  const auto adapter = backend_->GetCurrentDevice()->GetAdapterLuid();
  const auto report = nlohmann::json { { "schema_version", 1U },
    { "precision", precision_ }, { "width", width_ }, { "height", height_ },
    { "temporal_fog", temporal_ }, { "cycles", 3U }, { "frame_slots", 3U },
    { "scope",
      "Creation-time unique live texture/buffer placement; fixed fixture "
      "outputs included and named, diagnostic readbacks excluded. "
      "Queue-drained lifecycle schedule with explicitly deferred retained "
      "consumers; not heap commitment, residency, arbitrary concurrency or "
      "timed performance." },
    { "layout",
      "Two fixed large/half outputs; layout1 swaps their view assignments" },
    { "phases", phases_ }, { "frames", frames_ },
    { "allocation_peaks", backend_->allocation_peaks },
    { "allocation_peak_history", backend_->allocation_peak_history },
    { "delayed_consumers", consumer_results_ },
    { "delayed_depth_consumers", depth_results_ },
    { "adapter_luid_low", adapter.LowPart },
    { "adapter_luid_high", adapter.HighPart } };
  fixture_.RecordProperty("allocation_lifecycle_report", report.dump());
  fixture_.RecordProperty("main_width", width_);
  fixture_.RecordProperty("main_height", height_);
  fixture_.RecordProperty("concurrent_views", 2);
  fixture_.RecordProperty("temporal_fog", temporal_ ? 1 : 0);
  fixture_.RecordProperty("precision", precision_);
  fixture_.RecordProperty(
    "peak_traced_texture_bytes", std::to_string(backend_->peak_texture_bytes));
  fixture_.RecordProperty(
    "peak_hdr_bytes", std::to_string(backend_->peak_hdr_bytes));
  fixture_.RecordProperty(
    "peak_buffer_bytes", std::to_string(backend_->peak_buffer_bytes));
  fixture_.RecordProperty(
    "peak_placement_bytes", std::to_string(backend_->peak_placement_bytes));
  fixture_.RecordProperty("peak_engine_placement_bytes",
    std::to_string(backend_->peak_engine_placement_bytes));
  fixture_.RecordProperty("peak_hdr_iteration", backend_->peak_hdr_iteration);
  fixture_.RecordProperty("adapter_luid_low", std::to_string(adapter.LowPart));
  fixture_.RecordProperty(
    "adapter_luid_high", std::to_string(adapter.HighPart));
}

} // namespace oxygen::vortex::testing::exposure
