//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Benchmarks/ExposureAllocationScenario.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::ResourceStates;

auto ExposureAllocationScenario::RenderFrame(
  FrameRecipe recipe, const std::string& phase) -> void
{
  const auto [view_count, layout, ev] = recipe;
  ASSERT_LE(view_count, targets_.size());
  ASSERT_LT(layout, targets_.size());
  backend_->accounting_phase = phase;
  ++backend_->accounting_iteration;
  const auto slot = frame::Slot {
    fixture_.sequence % frame::kFramesInFlight.get(),
  };
  fixture_.Backend().BeginFrame(
    frame::SequenceNumber {
      ++fixture_.sequence,
    },
    slot);
  fixture_.frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
  fixture_.frame.SetFrameSequenceNumber(
    frame::SequenceNumber {
      fixture_.sequence,
    },
    engine::internal::EngineTagFactory::Get());
  fixture_.renderer_->OnFrameStart(observer_ptr {
    &fixture_.frame,
  });
  for (unsigned index = 0U; index < view_count; ++index) {
    const auto output_index = index ^ layout;
    auto sized_view = fixture_.view;
    sized_view.viewport.width = static_cast<float>(width_ >> output_index);
    sized_view.viewport.height = static_cast<float>(height_ >> output_index);
    auto input = CompositionView::ForScene(
      ViewId {
        500U + index,
      },
      sized_view, fixture_.camera);
    input.view_state_handle = CompositionView::ViewStateHandle {
      500U + index,
    };
    auto exposure = fixture_.settings;
    exposure.manual_ev = ev;
    input.render_settings.exposure = exposure;
    ASSERT_NE(
      fixture_.renderer_->PublishRuntimeCompositionView(fixture_.frame,
        { .composition_view = input,
          .render_target = observer_ptr { targets_.at(output_index).get(), },
          .composite_source
          = observer_ptr { targets_.at(output_index).get(), }, }),
      kInvalidViewId);
  }
  if (view_count != 0U) {
    auto loop = co::testing::TestEventLoop {};
    // Run completes synchronously before the closure or captures are destroyed.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(loop, [&] -> co::Co<void> {
      co_await fixture_.renderer_->OnPreRender(observer_ptr {
        &fixture_.frame,
      });
      co_await fixture_.renderer_->OnRender(observer_ptr {
        &fixture_.frame,
      });
    });
  }
  fixture_.renderer_->OnFrameEnd(observer_ptr {
    &fixture_.frame,
  });
  fixture_.Backend().EndFrame(
    frame::SequenceNumber {
      fixture_.sequence,
    },
    slot);
  fixture_.WaitForQueueIdle();
}

auto ExposureAllocationScenario::RemoveViews() -> void
{
  for (unsigned index = 0U; index < 2U; ++index) {
    fixture_.renderer_->RemovePublishedRuntimeView(fixture_.frame,
      ViewId {
        500U + index,
      });
  }
  current_.clear();
  fixture_.probe->color.reset();
  fixture_.probe->exposure.reset();
}

auto ExposureAllocationScenario::RunLifecycle() -> void
{
  std::ignore = Snapshot("cold_fixture");
  ASSERT_NO_FATAL_FAILURE(RenderFrame(
    {
      .view_count = 1U,
      .layout = 0U,
      .ev = 0.0F,
    },
    "bootstrap_one"));
  std::ignore = Snapshot("bootstrap_one");
  for (unsigned warm = 1U; warm < 8U; ++warm) {
    ASSERT_NO_FATAL_FAILURE(RenderFrame(
      {
        .view_count = 1U,
        .layout = 0U,
        .ev = 0.0F,
      },
      "qualify_one"));
  }
  std::ignore = Snapshot("steady_one");
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
    ASSERT_NO_FATAL_FAILURE(RenderFrame(
      {
        .view_count = 2U,
        .layout = 0U,
        .ev = 0.0F,
      },
      prefix + "qualify_two"));
  }
  for (unsigned index = 0U; index < 2U; ++index) {
    const auto handle = CompositionView::ViewStateHandle {
      500U + index,
    };
    ASSERT_TRUE(current_.contains(handle));
    retained_.at(index) = current_.at(handle);
    ASSERT_GT(retained_.at(index).draws, 0U);
    ASSERT_TRUE(retained_.at(index).color.valid);
    ASSERT_NE(retained_.at(index).color.retained_texture, nullptr);
    for (const auto& depth : retained_.at(index).depths) {
      ASSERT_TRUE(depth.valid);
      ASSERT_NE(depth.texture, nullptr);
      ASSERT_NE(depth.retained_texture, nullptr);
    }
    ASSERT_EQ(retained_.at(index).depths.at(0).texture,
      retained_.at(index).depths.at(1).texture);
    EXPECT_FALSE(retained_.at(index).depths.at(0).retained_texture.owner_before(
      retained_.at(index).depths.at(1).retained_texture));
    EXPECT_FALSE(retained_.at(index).depths.at(1).retained_texture.owner_before(
      retained_.at(index).depths.at(0).retained_texture));
    if (precision_ != "qualified") {
      EXPECT_EQ(retained_.at(index).color.texture->GetDescriptor().format,
        Format::kRGBA32Float);
    } else if (!temporal_) {
      EXPECT_EQ(retained_.at(index).color.texture->GetDescriptor().format,
        Format::kRGBA16Float);
    }
    ASSERT_TRUE(fixture_.renderer_
        ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
        .has_value());
  }
  backend_->accounting_phase = prefix + "depth_reference";
  for (unsigned index = 0U; index < 2U; ++index) {
    const auto& depth = *retained_.at(index).depths.at(0).texture;
    ASSERT_NO_FATAL_FAILURE(EnsureDepthReadback(index, depth));
    {
      auto recorder
        = fixture_.AcquireRecorder("Lifecycle immutable depth reference");
      CopyDepth(*recorder, depth,
        {
          .view_index = index,
          .alias = 0U,
        });
      recorder->RequireResourceStateFinal(
        depth, ResourceStates::kShaderResource);
    }
  }
  fixture_.WaitForQueueIdle();
  for (unsigned index = 0U; index < 2U; ++index) {
    observation.reference_depth.at(index) = DepthSample({
      .view_index = index,
      .alias = 0U,
    });
    const auto value
      = std::bit_cast<float>(observation.reference_depth.at(index));
    ASSERT_TRUE(std::isfinite(value));
    ASSERT_GT(value, 0.0F);
    ASSERT_LT(value, 1.0F);
  }
}

auto ExposureAllocationScenario::ResizeAndCapture(
  const std::string& prefix, CycleObservation& observation) -> void
{
  const auto steady = Snapshot(prefix + "steady_two_retained");
  ASSERT_NO_FATAL_FAILURE(RenderFrame(
    {
      .view_count = 2U,
      .layout = 0U,
      .ev = 1.0F,
    },
    prefix + "recovery"));
  std::ignore = Snapshot(prefix + "recovery_two_retained");
  for (unsigned warm = 0U; warm < 8U; ++warm) {
    ASSERT_NO_FATAL_FAILURE(RenderFrame(
      {
        .view_count = 2U,
        .layout = 1U,
        .ev = 1.0F,
      },
      prefix + "resized"));
  }
  const auto resized = Snapshot(prefix + "resized_two_retained");
  EXPECT_EQ(resized.at("pool_families"), steady.at("pool_families"))
    << "Retained color must not add attachment families for already warmed "
       "descriptors";
  WithoutDiagnostics([&] -> void {
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto& source = retained_.at(index).color;
      observation.saved_states.at(index) = fixture_.Read<ExposureStateData>(
        *source.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
      observation.saved_domains.at(index) = fixture_.Read<FrameExposureData>(
        *source.exposure->buffer, ResourceStates::kShaderResource);
      observation.saved_lifetimes.at(index)
        = source.exposure->current_state->owner_lifetime;
      observation.wrapper_lifetimes.at(index) = source.retained_texture;
      observation.resource_lifetimes.at(index)
        = source.texture->shared_from_this();
      observation.depth_wrapper_lifetimes.at(index)
        = retained_.at(index).depths.at(0).retained_texture;
      observation.depth_resource_lifetimes.at(index)
        = retained_.at(index).depths.at(0).texture->shared_from_this();
    }
  });
}

auto ExposureAllocationScenario::QueueConsumers(
  const std::string& prefix, CycleObservation& observation) -> void
{
  backend_->accounting_phase = prefix + "queued_consumers";
  if (!consumer_.has_value()) {
    FAIL() << "Expected the retained-consumer pass to be initialized";
  }
  pending_consumers_.clear();
  pending_tonemap_lists_.clear();
  {
    auto consume_context = RenderContext {};
    consume_context.frame_sequence = frame::SequenceNumber {
      fixture_.sequence,
    };
    consume_context.frame_slot = fixture_.frame.GetFrameSlot();
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto& source = retained_.at(index).color;
      ASSERT_NE(source.exposure, nullptr);
      const auto& exposure = source.exposure;
      consume_context.current_view.view_id = ViewId {
        900U + index,
      };
      if (!consumer_.has_value()) {
        FAIL() << "The retained consumer pass has not been initialized";
      }
      auto& consumer = *consumer_;
      const bool has_fallback = source.fallback != nullptr;
      auto inputs = postprocess::TonemapPass::Inputs {};
      inputs.scene_signal = source.texture;
      inputs.exposure_buffer = exposure->current_state->buffer.get();
      inputs.frame_exposure_buffer = exposure->buffer.get();
      inputs.scene_signal_srv = Srv(*source.texture);
      inputs.exposure_buffer_srv = exposure->current_state->srv_index;
      inputs.frame_exposure_srv = exposure->srv_index;
      inputs.post_target = observer_ptr<const Framebuffer> {
        consumer_targets_.at(index).get(),
      };
      inputs.tone_mapper = engine::ToneMapper::kNone;
      inputs.gamma = 1.0F;
      inputs.scene_fallback = source.fallback;
      inputs.scene_fallback_srv
        = has_fallback ? Srv(*source.fallback) : kInvalidShaderVisibleIndex;
      inputs.conversion_report
        = has_fallback ? exposure->conversion_buffer.get() : nullptr;
      inputs.conversion_report_srv
        = has_fallback ? exposure->conversion_srv : kInvalidShaderVisibleIndex;
      auto recording = fixture_.AcquireRecorder("Vortex PostProcess",
        graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
      ASSERT_TRUE(recording);
      ASSERT_TRUE(
        consumer.Record(consume_context, *recording, inputs).recorded);
      pending_tonemap_lists_.push_back(
        recording->GetCommandListForInspection());
      pending_consumers_.push_back(std::move(recording));
    }
  }
  ASSERT_EQ(pending_tonemap_lists_.size(), 2U);
  {
    auto recorder = fixture_.AcquireRecorder("Lifecycle retained depth aliases",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    for (unsigned index = 0U; index < 2U; ++index) {
      for (unsigned alias = 0U; alias < 2U; ++alias) {
        CopyDepth(*recorder, *retained_.at(index).depths.at(alias).texture,
          {
            .view_index = index,
            .alias = alias,
          });
      }
      recorder->RequireResourceStateFinal(
        *retained_.at(index).depths.at(0).texture,
        ResourceStates::kShaderResource);
    }
    observation.depth_recording = recorder->GetCommandListForInspection();
    pending_consumers_.push_back(std::move(recorder));
  }
  ASSERT_NE(observation.depth_recording, nullptr);
  ASSERT_FALSE(observation.depth_recording->IsSubmitted());
  for (const auto& recording : pending_tonemap_lists_) {
    ASSERT_FALSE(recording->IsSubmitted());
  }
}

auto ExposureAllocationScenario::RemoveAndReadd(
  const std::string& prefix, CycleObservation& observation) -> void
{
  RemoveViews();
  std::ignore = Snapshot(prefix + "removed_retained_pending");
  ASSERT_NO_FATAL_FAILURE(RenderFrame(
    {
      .view_count = 2U,
      .layout = 1U,
      .ev = 2.0F,
    },
    prefix + "readded"));
  ASSERT_FALSE(observation.depth_recording->IsSubmitted());
  for (const auto& recording : pending_tonemap_lists_) {
    ASSERT_FALSE(recording->IsSubmitted());
  }
  for (unsigned index = 0U; index < 2U; ++index) {
    const auto handle = CompositionView::ViewStateHandle {
      500U + index,
    };
    ASSERT_TRUE(current_.contains(handle));
    EXPECT_NE(current_.at(handle).color.exposure->current_state->owner_lifetime,
      observation.saved_lifetimes.at(index));
  }
  WithoutDiagnostics([&] -> void {
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto& source = retained_.at(index).color;
      const auto state = fixture_.Read<ExposureStateData>(
        *source.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
      const auto domain = fixture_.Read<FrameExposureData>(
        *source.exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ((std::bit_cast<std::array<std::byte, sizeof(state)>>(state)),
        (std::bit_cast<std::array<std::byte, sizeof(state)>>(
          observation.saved_states.at(index))));
      EXPECT_EQ((std::bit_cast<std::array<std::byte, sizeof(domain)>>(domain)),
        (std::bit_cast<std::array<std::byte, sizeof(domain)>>(
          observation.saved_domains.at(index))));
    }
  });
  std::ignore = Snapshot(prefix + "readded_retained_pending");
}

auto ExposureAllocationScenario::ReleaseAndSubmit(
  const std::string& prefix, CycleObservation& observation) -> void
{
  const auto completion = fixture_.SignalQueue();
  {
    auto recorder
      = fixture_.AcquireRecorder("Lifecycle retained consumer completion",
        graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    recorder->RecordQueueSignal(completion.get());
    pending_consumers_.push_back(std::move(recorder));
  }
  EXPECT_LT(fixture_.GetQueue()->GetCompletedValue(), completion.get());
  retained_ = {};
  for (unsigned index = 0U; index < 2U; ++index) {
    EXPECT_TRUE(observation.wrapper_lifetimes.at(index).expired());
    EXPECT_FALSE(observation.resource_lifetimes.at(index).expired());
    EXPECT_TRUE(observation.depth_wrapper_lifetimes.at(index).expired());
    EXPECT_FALSE(observation.depth_resource_lifetimes.at(index).expired());
  }
  auto pending_snapshot = backend_->MeasureTrackedPlacement();
  pending_snapshot.update({
    {
      "phase",
      prefix + "released_queued_before_submission",
    },
    {
      "sequence",
      fixture_.sequence,
    },
    {
      "slot",
      fixture_.frame.GetFrameSlot().get(),
    },
    {
      "retained_extracts",
      0U,
    },
    {
      "retained_depth_aliases",
      0U,
    },
  });
  phases_.push_back(std::move(pending_snapshot));
  EXPECT_LT(fixture_.GetQueue()->GetCompletedValue(), completion.get());
  for (auto& recording : pending_consumers_) {
    ASSERT_TRUE(recording.Submit());
  }
  pending_consumers_.clear();
  ASSERT_TRUE(observation.depth_recording->IsSubmitted());
  for (const auto& recording : pending_tonemap_lists_) {
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
      const auto actual = DepthSample({
        .view_index = index,
        .alias = alias,
      });
      EXPECT_EQ(actual, observation.reference_depth.at(index));
      depth_results_.push_back({
        {
          "cycle",
          cycle,
        },
        {
          "view",
          index,
        },
        {
          "alias",
          alias,
        },
        {
          "reference_bits",
          observation.reference_depth.at(index),
        },
        {
          "actual_bits",
          actual,
        },
        {
          "width",
          depth_readbacks_.at(index).footprint.Footprint.Width,
        },
        {
          "height",
          depth_readbacks_.at(index).footprint.Footprint.Height,
        },
      });
    }
  }
  observation.depth_recording.reset();
  WithoutDiagnostics([&] -> void {
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto pixel
        = fixture_.ReadFloatTexture(*consumer_outputs_.at(index));
      ASSERT_EQ(pixel.size(), 1U);
      // A 1x1 output samples the source center. Ordered dithering uses that
      // source pixel, so the half-size view can have a different Bayer rank.
      // Construct the 4x4 rank independently from its interleaved bit pairs.
      const auto sample_x = (width_ >> index) / 2U;
      const auto sample_y = (height_ >> index) / 2U;
      const auto bayer_rank = (8U * ((sample_x ^ sample_y) & 1U))
        + (4U * (sample_y & 1U)) + (2U * (((sample_x ^ sample_y) >> 1U) & 1U))
        + ((sample_y >> 1U) & 1U);
      const double expected
        = .25 + (((static_cast<double>(bayer_rank) / 16.0) - .5) / 255.0);
      EXPECT_EQ(observation.saved_states.at(index).displayed_scale, 1.0F);
      ASSERT_GT(observation.saved_domains.at(index).pre_exposure, 0.0F);
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(pixel.front().at(channel), expected, 2e-5);
      }
      EXPECT_EQ(pixel.front().at(3), 1.0F);
      consumer_results_.push_back({
        {
          "cycle",
          cycle,
        },
        {
          "view",
          index,
        },
        {
          "expected",
          expected,
        },
        {
          "actual",
          pixel.front(),
        },
        {
          "source_pixel",
          {
            sample_x,
            sample_y,
          },
        },
        {
          "bayer_rank",
          bayer_rank,
        },
        {
          "P",
          observation.saved_domains.at(index).pre_exposure,
        },
        {
          "gain",
          observation.saved_states.at(index).displayed_scale,
        },
        {
          "retired_lifetime",
          observation.saved_lifetimes.at(index),
        },
        {
          "requested_generation",
          observation.saved_states.at(index).requested_generation,
        },
        {
          "applied_generation",
          observation.saved_states.at(index).applied_generation,
        },
      });
    }
  });
}

auto ExposureAllocationScenario::RetireCycle(const unsigned cycle,
  const std::string& prefix, CycleObservation& observation) -> void
{
  pending_consumers_.clear();
  pending_tonemap_lists_.clear();
  RemoveViews();
  std::ignore = Snapshot(prefix + "released_before_fence");
  for (unsigned drain = 0U; drain < 2U * frame::kFramesInFlight.get();
    ++drain) {
    ASSERT_NO_FATAL_FAILURE(RenderFrame(
      {
        .view_count = 0U,
        .layout = 0U,
        .ev = 0.0F,
      },
      prefix + "retirement"));
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
  const auto report = nlohmann::json {
    {
      "schema_version",
      1U,
    },
    {
      "precision",
      precision_,
    },
    {
      "width",
      width_,
    },
    {
      "height",
      height_,
    },
    {
      "temporal_fog",
      temporal_,
    },
    {
      "cycles",
      3U,
    },
    {
      "frame_slots",
      3U,
    },
    {
      "scope",
      "Creation-time unique live texture/buffer placement; fixed fixture "
      "outputs included and named, diagnostic readbacks excluded. "
      "Queue-drained lifecycle schedule with explicitly deferred retained "
      "consumers; not heap commitment, residency, arbitrary concurrency or "
      "timed performance.",
    },
    {
      "layout",
      "Two fixed large/half outputs; layout1 swaps their view assignments",
    },
    {
      "phases",
      phases_,
    },
    {
      "frames",
      frames_,
    },
    {
      "allocation_peaks",
      backend_->allocation_peaks,
    },
    {
      "allocation_peak_history",
      backend_->allocation_peak_history,
    },
    {
      "delayed_consumers",
      consumer_results_,
    },
    {
      "delayed_depth_consumers",
      depth_results_,
    },
    {
      "adapter_luid_low",
      adapter.LowPart,
    },
    {
      "adapter_luid_high",
      adapter.HighPart,
    },
  };
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "allocation_lifecycle_report", report.dump());
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "main_width", width_);
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "main_height", height_);
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "concurrent_views", 2);
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "temporal_fog", temporal_ ? 1 : 0);
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "precision", precision_);
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "peak_traced_texture_bytes", std::to_string(backend_->peak_texture_bytes));
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "peak_hdr_bytes", std::to_string(backend_->peak_hdr_bytes));
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "peak_buffer_bytes", std::to_string(backend_->peak_buffer_bytes));
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "peak_placement_bytes", std::to_string(backend_->peak_placement_bytes));
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "peak_engine_placement_bytes",
    std::to_string(backend_->peak_engine_placement_bytes));
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "peak_hdr_iteration", backend_->peak_hdr_iteration);
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "adapter_luid_low", std::to_string(adapter.LowPart));
  oxygen::vortex::testing::exposure::ExposureLightingGpuTest::RecordProperty(
    "adapter_luid_high", std::to_string(adapter.HighPart));
}

} // namespace oxygen::vortex::testing::exposure
