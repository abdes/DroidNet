//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <fstream>

#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBaselineScenario.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  constexpr std::array event_frames {
    60U,
    120U,
    180U,
    240U,
    300U,
    360U,
    420U,
    480U,
    540U,
    600U,
    660U,
    720U,
    780U,
    840U,
    900U,
    906U,
  };

  auto IsCheckpoint(const unsigned frame) -> bool
  {
    return frame == 903U
      || std::ranges::any_of(
        event_frames, [frame](const unsigned event) -> bool {
          return frame == event || frame == event + 30U;
        });
  }

  auto Integer(const std::array<std::uint32_t, 2>& words) -> std::uint64_t
  {
    return words.at(0)
      | (std::uint64_t {
           words.at(1),
         }
        << 32U);
  }
} // namespace

auto ExposureBaselineScenario::FinalizeRecording(const unsigned path_frame)
  -> void
{
  fixture_.WaitForQueueIdle();
  static_cast<void>(RenderFrame(false, path_frame));
}

auto ExposureBaselineScenario::ApplyEvent(const unsigned event_frame) -> void
{
  const auto main = CompositionView::ViewStateHandle {
    500U,
  };
  const char* operation = nullptr;
  switch (event_frame) {
  case 60U:
  case 900U: {
    const auto token = fixture_.renderer_->QueueExposureTransition(
      main, ExposureTransitionPolicy::kSeedFromEv100, 14.5F);
    CHECK_F(token.has_value());
    operation = event_frame == 60U ? "seed" : "seed-delay-status";
    break;
  }
  case 120U:
    CHECK_F(fixture_.renderer_
        ->NotifyViewDiscontinuity(main, ViewDiscontinuity::kCameraCut)
        .has_value());
    operation = "camera-cut";
    break;
  case 180U:
    view_settings.at(0).mode = engine::ExposureMode::kManual;
    view_settings.at(0).manual_ev = 14.5F;
    operation = "manual";
    break;
  case 240U:
    view_settings.at(0) = fixture_.settings;
    operation = "restore-auto";
    break;
  case 300U:
  case 360U: {
    auto light = sun.GetLightAs<scene::DirectionalLight>();
    CHECK_F(light.has_value());
    light->get().SetIntensityLux(event_frame == 300U ? 440000.0F : 110000.0F);
    operation = event_frame == 300U ? "sun-step" : "restore-sun";
    // UpdatePath normally synchronizes before OnFrameStart; this operation
    // changes authored lighting afterward, so publish its dirty state now.
    fixture_.scene->Update();
    fixture_.scene->SyncObservers();
    break;
  }
  case 420U:
    secondary_source = ViewId {
      500U,
    };
    operation = "share-main";
    break;
  case 480U:
    fixture_.renderer_->RemovePublishedRuntimeView(fixture_.frame,
      ViewId {
        500U,
      });
    enabled_views.at(0) = false;
    // Removal detaches borrowers. Republishing a missing source is invalid.
    secondary_source = kInvalidViewId;
    operation = "remove-source";
    break;
  case 540U:
    enabled_views.at(0) = true;
    secondary_source = ViewId {
      500U,
    };
    operation = "recreate-source";
    break;
  case 600U:
    secondary_source = kInvalidViewId;
    operation = "independent-secondary";
    break;
  case 660U:
  case 720U:
    std::swap(target_indices.at(0), target_indices.at(1));
    for (unsigned index = 0U; index < view_count; ++index) {
      auto lens = cameras.at(index).GetCameraAs<scene::PerspectiveCamera>();
      CHECK_F(lens.has_value());
      auto viewport = fixture_.view.viewport;
      viewport.width = static_cast<float>(width >> target_indices.at(index));
      viewport.height = static_cast<float>(height >> target_indices.at(index));
      lens->get().SetViewport(viewport);
    }
    operation = event_frame == 660U ? "swap-layout" : "restore-layout";
    break;
  case 780U:
    fixture_.renderer_->RemovePublishedRuntimeView(fixture_.frame,
      ViewId {
        501U,
      });
    enabled_views.at(1) = false;
    operation = "remove-secondary";
    break;
  case 840U:
    enabled_views.at(1) = true;
    operation = "recreate-secondary";
    break;
  case 906U:
    operation = "restore-status-delivery";
    break;
  default:
    break;
  }
  if (operation != nullptr) {
    const auto status = fixture_.renderer_->InspectExposureTransition(main);
    event_operations.push_back({
      {
        "event_frame",
        event_frame,
      },
      {
        "operation",
        operation,
      },
      {
        "frame_sequence",
        fixture_.sequence,
      },
      {
        "requested_generation",
        status ? status->request.generation : 0U,
      },
      {
        "lifetime",
        status ? status->request.lifetime : 0U,
      },
    });
  }
}

auto ExposureBaselineScenario::ObserveEvent(const unsigned event_frame) -> void
{
  using Probe = vortex::testing::RendererPublicationProbe;
  auto* owner = Probe::GetSceneRenderer(*fixture_.renderer_);
  auto* service = Probe::GetPostProcessService(*owner);
  auto observation = EventObservation {
    .event_frame = event_frame,
  };
  for (unsigned index = 0U; index < view_count; ++index) {
    observation.active.at(index) = seen_views.at(index);
    if (!seen_views.at(index)) {
      continue;
    }
    const auto handle = CompositionView::ViewStateHandle {
      500U + index,
    };
    const auto state = Probe::ExposureStateForView(*service, handle);
    CHECK_NOTNULL_F(state.get());
    observation.lifetimes.at(index) = state->owner_lifetime;
    if (const auto status
      = fixture_.renderer_->InspectExposureTransition(handle)) {
      observation.requested.at(index) = status->request.generation;
      observation.applied.at(index) = status->applied_generation;
      observation.phases.at(index) = static_cast<unsigned>(status->phase);
    }
  }
  if (event_frame == 120U) {
    // Camera-cut policy creates its request when controls are captured.
    event_operations.back()["requested_generation"]
      = observation.requested.at(0);
    event_operations.back()["lifetime"] = observation.lifetimes.at(0);
  }
  if (event_frame >= 900U && event_frame <= 905U) {
    held_statuses = Probe::TakeExposureStatusDelivery(*service,
      CompositionView::ViewStateHandle {
        500U,
      });
    CHECK_F(held_statuses.pending.size() <= frame::kFramesInFlight.get());
    const auto status = fixture_.renderer_->InspectExposureTransition(
      CompositionView::ViewStateHandle {
        500U,
      });
    CHECK_F(status && status->phase == ExposureTransitionPhase::kQueued);
  }
  observation.held_status_jobs
    = static_cast<unsigned>(held_statuses.pending.size());
  observations.push_back(observation);
  if (capture_event) {
    checkpoints.push_back(std::move(current_checkpoint));
    current_checkpoint = {};
  }
}

auto ExposureBaselineScenario::MeasureEventWindows() -> void
{
  FinalizeRecording(sample_count);
  observations.reserve(1200U);
  checkpoints.reserve(33U);
  for (auto* window : {
         &matched,
         &events,
       }) {
    window->samples.reserve(1200U);
    recording_path = window->gpu;
    recording_frames = 1200U;
    event_cycle = window == &events;
    auto started = Clock::now();
    auto previous_end = started;
    for (unsigned index = 0U; index < 1200U; ++index) {
      capture_event = event_cycle && IsCheckpoint(index);
      current_checkpoint.event_frame = index;
      auto sample = RenderFrame(index == 0U, index);
      const auto ended = Clock::now();
      sample.wall_ms = Milliseconds(ended - previous_end);
      previous_end = ended;
      window->samples.push_back(sample);
    }
    window->seconds
      = std::chrono::duration<double>(Clock::now() - started).count();
    event_cycle = false;
    capture_event = false;
    FinalizeRecording(1200U);
  }
  CHECK_F(held_statuses.pending.empty() && !held_statuses.deferred);
  recording_path = gpu_path;
  recording_frames = sample_count;
}

auto ExposureBaselineScenario::SaveWindow(const Window& window) -> void
{
  auto input = std::ifstream(window.gpu);
  ASSERT_TRUE(input.is_open());
  const auto gpu = nlohmann::json::parse(input);
  ASSERT_EQ(gpu.at("complete"), true);
  ASSERT_EQ(gpu.at("timing_valid"), true);
  ASSERT_EQ(gpu.at("frames").size(), window.samples.size());
  ASSERT_EQ(gpu.at("first_frame_seq"), window.samples.front().frame_sequence);
  auto records = nlohmann::json::array();
  for (const auto& sample : window.samples) {
    records.push_back({
      {
        "frame_sequence",
        sample.frame_sequence,
      },
      {
        "wall_ms",
        sample.wall_ms,
      },
      {
        "frame_start_ms",
        sample.frame_start_ms,
      },
      {
        "submission_ms",
        sample.submission_ms,
      },
      {
        "formats",
        sample.formats,
      },
      {
        "draws",
        sample.draws,
      },
      {
        "path_phases",
        sample.path_phases,
      },
      {
        "history_reprojected",
        sample.history_reprojected,
      },
      {
        "history_reset",
        sample.history_reset,
      },
    });
  }
  const auto path = directory / (stem + "-" + window.name + ".cpu.json");
  ASSERT_FALSE(std::filesystem::exists(path));
  auto output = std::ofstream(path, std::ios::binary);
  ASSERT_TRUE(output.is_open());
  output << records.dump(2) << '\n';
  output.close();
  ASSERT_TRUE(output.good());
  acceptance_windows[window.name] = {
    {
      "gpu",
      window.gpu.filename().string(),
    },
    {
      "cpu",
      path.filename().string(),
    },
    {
      "frames",
      window.samples.size(),
    },
    {
      "seconds",
      window.seconds,
    },
  };
}

auto ExposureBaselineScenario::SaveAcceptanceWindows() -> void
{
  ASSERT_NO_FATAL_FAILURE(SaveWindow(startup));
  if (events.samples.empty()) {
    return;
  }
  ASSERT_NO_FATAL_FAILURE(SaveWindow(matched));
  ASSERT_NO_FATAL_FAILURE(SaveWindow(events));
  auto gpu_checkpoints = nlohmann::json::array();
  for (const auto& checkpoint : checkpoints) {
    for (unsigned index = 0U; index < view_count; ++index) {
      const auto& exposure = checkpoint.exposure.at(index);
      if (!exposure) {
        continue;
      }
      const auto domain = fixture_.Read<FrameExposureData>(
        *exposure->buffer, graphics::ResourceStates::kShaderResource);
      const auto state
        = fixture_.Read<ExposureStateData>(*exposure->current_state->buffer,
          graphics::ResourceStates::kShaderResource);
      EXPECT_FLOAT_EQ(domain.pre_exposure, 1.0F);
      EXPECT_FLOAT_EQ(domain.one_over_pre_exposure, 1.0F);
      EXPECT_EQ(checkpoint.formats.at(index),
        static_cast<unsigned>(Format::kRGBA32Float));
      EXPECT_EQ(exposure->suitability_buffer, nullptr);
      EXPECT_EQ(exposure->conversion_buffer, nullptr);
      EXPECT_TRUE(std::isfinite(state.displayed_scale));
      EXPECT_GT(state.displayed_scale, 0.0F);
      if (index == 0U
        && (checkpoint.event_frame == 60U || checkpoint.event_frame == 180U
          || checkpoint.event_frame == 900U)) {
        EXPECT_NEAR(state.displayed_scale, std::exp2(-14.5F), 1e-9F);
      }
      if (index == 0U
        && (checkpoint.event_frame == 60U || checkpoint.event_frame == 120U
          || checkpoint.event_frame == 900U)) {
        const auto operation = std::ranges::find_if(
          event_operations, [&](const auto& event) -> bool {
            return event.at("event_frame") == checkpoint.event_frame;
          });
        ASSERT_NE(operation, event_operations.end());
        EXPECT_EQ(Integer(state.applied_generation),
          operation->at("requested_generation").get<std::uint64_t>());
      }
      if (index == 1U
        && (checkpoint.event_frame == 420U || checkpoint.event_frame == 450U
          || checkpoint.event_frame == 540U
          || checkpoint.event_frame == 570U)) {
        ASSERT_NE(exposure->selected_history, nullptr);
        const auto source = fixture_.Read<ExposureStateData>(
          *exposure->selected_history->buffer,
          graphics::ResourceStates::kShaderResource);
        EXPECT_FLOAT_EQ(state.displayed_scale, source.displayed_scale);
        EXPECT_EQ(exposure->current_state->borrowed_from.get(), 500U);
      }
      gpu_checkpoints.push_back({
        {
          "event_frame",
          checkpoint.event_frame,
        },
        {
          "view",
          index,
        },
        {
          "lifetime",
          exposure->current_state->owner_lifetime,
        },
        {
          "borrowed_from",
          exposure->current_state->borrowed_from.get(),
        },
        {
          "borrowed_lifetime",
          exposure->current_state->borrowed_lifetime,
        },
        {
          "pre_exposure",
          domain.pre_exposure,
        },
        {
          "displayed_scale",
          state.displayed_scale,
        },
        {
          "applied_generation",
          Integer(state.applied_generation),
        },
        {
          "history_reset",
          checkpoint.history_reset.at(index),
        },
      });
    }
  }
  EXPECT_NE(observations.at(479U).lifetimes.at(0),
    observations.at(540U).lifetimes.at(0));
  EXPECT_NE(observations.at(779U).lifetimes.at(1),
    observations.at(840U).lifetimes.at(1));
  for (const auto operation : {
         60U,
         120U,
         900U,
       }) {
    const auto& issued = observations.at(operation);
    ASSERT_GT(issued.requested.at(0), 0U);
    const auto completion = std::ranges::find_if(
      observations, [&](const EventObservation& observation) -> bool {
        return observation.event_frame >= operation
          && observation.lifetimes.at(0) == issued.lifetimes.at(0)
          && observation.applied.at(0) == issued.requested.at(0)
          && observation.phases.at(0)
          == static_cast<unsigned>(ExposureTransitionPhase::kApplied);
      });
    ASSERT_NE(completion, observations.end());
    for (auto& event : event_operations) {
      if (event.at("event_frame") == operation) {
        event["completed_at_event_frame"] = completion->event_frame;
      }
    }
  }
  auto statuses = nlohmann::json::array();
  for (const auto& observation : observations) {
    statuses.push_back({
      {
        "event_frame",
        observation.event_frame,
      },
      {
        "active",
        observation.active,
      },
      {
        "lifetimes",
        observation.lifetimes,
      },
      {
        "requested_generations",
        observation.requested,
      },
      {
        "applied_generations",
        observation.applied,
      },
      {
        "phases",
        observation.phases,
      },
      {
        "held_status_jobs",
        observation.held_status_jobs,
      },
    });
  }
  const auto path = directory / (stem + "-event-status.json");
  ASSERT_FALSE(std::filesystem::exists(path));
  auto output = std::ofstream(path, std::ios::binary);
  output << nlohmann::json(
    {
      {
        "operations",
        event_operations,
      },
      {
        "cpu_status",
        statuses,
      },
      {
        "gpu_checkpoints",
        gpu_checkpoints,
      },
      {
        "scope",
        "Untimed GPU reads after collection; retained frame/state leases are "
        "included in resource ownership. No image readbacks or drains inside "
        "event windows.",
      },
    })
              .dump(2)
         << '\n';
  output.close();
  ASSERT_TRUE(output.good());
  acceptance_windows["event_status"] = path.filename().string();
  checkpoints.clear();
}

} // namespace oxygen::vortex::testing::exposure
