//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Profiling/CpuScopeObserver.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Benchmarks/ManyLightBaseline.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing {

auto ManyLightBaseline::BackendConfigJson() const -> std::string
{
  if (!CapturePath().empty()) {
    return R"({"enable_debug_layer":true,"enable_vsync":false,"frame_capture":{"provider":"renderdoc","init_mode":"search"}})";
  }
  return R"({"enable_debug_layer":false,"enable_vsync":false})";
}
auto ManyLightBaseline::AdditionalCapabilities() const -> CapabilitySet
{
  return RendererCapabilityFamily::kDiagnosticsAndProfiling
    | RendererCapabilityFamily::kShadowing;
}
auto ManyLightBaseline::ConfigureRenderer(RendererConfig& config) const -> void
{
  if (reference_) {
    // Existing correctness-preserving complete-list path, outside timed runs.
    config.lighting_compact_index_limit_bytes = 0U;
  }
}
auto ManyLightBaseline::SetUp() -> void
{
  char* path = nullptr;
  std::size_t size {};
  ASSERT_EQ(_dupenv_s(&path, &size, "OXYGEN_MANY_LIGHT_REQUEST"), 0);
  const auto owned
    = std::unique_ptr<char, decltype(&std::free)>(path, &std::free);
  ASSERT_NE(path, nullptr)
    << "Set OXYGEN_MANY_LIGHT_REQUEST to a validated request JSON";
  auto input = std::ifstream(path);
  ASSERT_TRUE(input.good());
  request_ = nlohmann::json::parse(input);
  ASSERT_EQ(request_.at("schema_version"), 1U);
  reference_ = request_.at("reference").get<bool>();
  measure_ = request_.at("measure").get<bool>();
  ASSERT_FALSE(reference_ && measure_);
  const auto family = request_.at("family").get<std::string>();
  ASSERT_TRUE(family == "forward" || family == "deferred");
  forward_ = family == "forward";
  options_.light_count = request_.at("lights").get<unsigned>();
  options_.width = request_.at("width").get<unsigned>();
  options_.height = request_.at("height").get<unsigned>();
  options_.moving = request_.at("moving").get<bool>();
  options_.secondary_view = request_.at("secondary_view").get<bool>();
  const auto layout
    = request_.value("secondary_view_layout", std::string("offset-half"));
  options_.secondary_layout = layout == "matched"
    ? WorkloadSecondaryLayout::kMatched
    : layout == "offset-full"     ? WorkloadSecondaryLayout::kOffsetFull
    : layout == "partial-overlap" ? WorkloadSecondaryLayout::kPartialOverlap
                                  : WorkloadSecondaryLayout::kOffsetHalf;
  options_.point_shadow_requests = request_.at("point_shadows").get<unsigned>();
  options_.spot_shadow_requests = request_.at("spot_shadows").get<unsigned>();
  options_.source_radius_m = request_.at("source_radius_m").get<float>();
  options_.spot_outer_half_angle_radians
    = request_.at("spot_outer_half_angle_radians").get<float>();
  const auto distribution = request_.at("distribution").get<std::string>();
  ASSERT_TRUE(distribution == "sparse" || distribution == "dense"
    || distribution == "irrelevant");
  options_.distribution = distribution == "dense" ? WorkloadDistribution::kDense
    : distribution == "irrelevant" ? WorkloadDistribution::kMostlyIrrelevant
                                   : WorkloadDistribution::kSparse;
  const auto projection = request_.at("projection").get<std::string>();
  ASSERT_TRUE(projection == "perspective" || projection == "orthographic");
  options_.projection = projection == "orthographic"
    ? WorkloadProjection::kOrthographic
    : WorkloadProjection::kPerspective;
  warmup_frames_ = request_.at("warmup_frames").get<unsigned>();
  sample_frames_ = request_.at("sample_frames").get<unsigned>();
  ASSERT_GE(warmup_frames_, 12U);
  ASSERT_LE(warmup_frames_, 960U);
  ASSERT_GE(sample_frames_, 120U);
  ASSERT_LE(sample_frames_, 1920U);
  ASSERT_EQ(sample_frames_ % 120U, 0U);
  if (options_.moving) {
    ASSERT_EQ(sample_frames_ % 240U, 0U);
  }
  directory_ = request_.at("output").get<std::string>();
  ASSERT_FALSE(std::filesystem::exists(directory_ / "manifest.json"));
  std::filesystem::create_directories(directory_);
  workload_ = BuildLightingWorkload(options_);
  ExposureLightingGpuTest::SetUp();
  FailureBackend().SetRecorderNameCollectionEnabled(false);
  FailureBackend().track_resources = true;
}
auto ManyLightBaseline::TearDown() -> void
{
  if (probe) {
    probe->inspect = {};
  }
  captured_.clear();
  targets_.clear();
  scene_owner_ = {};
  ExposureLightingGpuTest::TearDown();
}
auto ManyLightBaseline::SetupCase() -> void
{
  scene_owner_ = CreateLightingWorkloadScene(workload_);
  scene = scene_owner_.scene;
  camera = scene_owner_.cameras.front();
  mesh_node = scene_owner_.floor;
  frame.SetScene(observer_ptr { scene.get() });
  auto timing = frame.GetModuleTimingData();
  timing.game_delta_time
    = time::CanonicalDuration { std::chrono::nanoseconds { 16'666'667 } };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  for (const auto& recipe : workload_.views) {
    auto output = CreateRegisteredTexture({ .width = recipe.width,
      .height = recipe.height,
      .format = Format::kRGBA32Float,
      .debug_name = "ManyLight.Output." + std::to_string(recipe.id.get()),
      .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon });
    targets_.push_back(Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(output)));
  }
  captured_.resize(workload_.views.size());
  if (options_.moving) {
    motion_cycle_.reserve(240U);
    for (unsigned phase = 0; phase < 240U; ++phase) {
      auto moving = options_;
      moving.motion_frame = phase;
      motion_cycle_.push_back(BuildLightingWorkload(moving));
    }
  }
  probe->prepare = [](RenderContext& context) {
    context.current_view.depth_prepass_mode
      = DepthPrePassMode::kOpaqueAndMasked;
  };
  probe->inspect = [this](const RenderContext& context,
                     const SceneTextureExtractRef& color,
                     const unsigned draws) {
    if (!capture_ && !recording_) {
      return;
    }
    CHECK_F(color.valid && color.texture != nullptr && draws > 0U);
    const auto* scene_renderer
      = RendererPublicationProbe::GetSceneRenderer(*renderer_);
    const auto* shadow_service
      = RendererPublicationProbe::GetShadowService(*scene_renderer);
    if (recording_ && shadow_service) {
      const auto& sharing = shadow_service->GetLastRenderState();
      frame_shadow_writers_ += sharing.rendered_point_shadow_count
        + sharing.rendered_spot_shadow_count;
      frame_shadow_map_uses_ += sharing.attached_map_uses;
      frame_shadow_backing_uses_ += sharing.attached_backing_uses;
    }
    if (!capture_) {
      return;
    }
    const auto index = context.current_view.view_state_handle.get() - 100U;
    auto& output = captured_.at(index);
    output.hdr = color.texture->shared_from_this();
    output.exposure = context.current_view.frame_exposure;
    output.draws = draws;
    const auto* lighting
      = RendererPublicationProbe::GetLightingService(*renderer_);
    const auto* bindings
      = lighting->InspectForwardLightBindings(context.current_view.view_id);
    CHECK_NOTNULL_F(bindings);
    output.bindings = *bindings;
    output.grid = lighting->InspectGridResources(context.current_view.view_id);
    const auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
    const auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
    if (const auto* data
      = shadows->InspectShadowData(context.current_view.view_id)) {
      output.point_shadows
        = static_cast<unsigned>(data->cube_local_records.size());
      output.spot_shadows
        = static_cast<unsigned>(data->projected_local_records.size());
      output.quality_omissions
        = static_cast<unsigned>(data->local_quality_omissions.size());
    }
  };
  auto& diagnostics = renderer_->GetDiagnosticsService();
  diagnostics.SetHdrPrecisionControl(HdrPrecisionControl::kProduction);
  diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
  diagnostics.SetGpuTimelineMaxScopesPerFrame(options_.light_count * 2U + 256U);
  diagnostics.SetGpuTimelineRetainLatestFrame(false);
  diagnostics.SetGpuTimelineEnabled(measure_);
}
auto ManyLightBaseline::Milliseconds(const Clock::duration duration) -> double
{
  return std::chrono::duration<double, std::milli>(duration).count();
}
auto ManyLightBaseline::RenderFrame(
  const unsigned motion_frame, const bool begin_recording) -> Sample
{
  frame_shadow_writers_ = frame_shadow_map_uses_ = frame_shadow_backing_uses_
    = 0;
  const auto started = Clock::now();
  const auto slot = frame::Slot { sequence % frame::kFramesInFlight.get() };
  const auto current = frame::SequenceNumber { ++sequence };
  auto observer = std::optional<profiling::ScopedCpuScopeObserver> {};
  if (recording_) {
    cpu_->BeginFrame(current);
    observer.emplace(*cpu_);
  }
  Backend().BeginFrame(current, slot);
  const auto began = Clock::now();
  frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
  frame.SetFrameSequenceNumber(
    current, engine::internal::EngineTagFactory::Get());
  if (options_.moving) {
    scene_owner_.ApplyMotion(motion_cycle_.at(motion_frame % 240U));
  }
  const auto updated = Clock::now();
  renderer_->OnFrameStart(observer_ptr { &frame });
  if (begin_recording) {
    CHECK_F(renderer_->GetDiagnosticsService().RequestGpuTimelineRecording(
      directory_ / "gpu.json", sample_frames_));
  }
  for (std::size_t index = 0; index < workload_.views.size(); ++index) {
    const auto& recipe = workload_.views.at(index);
    auto sized_view = View {};
    sized_view.viewport.width = static_cast<float>(recipe.width);
    sized_view.viewport.height = static_cast<float>(recipe.height);
    auto input = CompositionView::ForScene(
      ViewId { 100U + static_cast<unsigned>(index) }, sized_view,
      scene_owner_.cameras.at(index));
    input.view_state_handle = CompositionView::ViewStateHandle { 100U
      + static_cast<unsigned>(index) };
    input.render_settings.exposure = settings;
    CHECK_F(renderer_->PublishRuntimeCompositionView(frame,
              { .composition_view = input,
                .render_target = observer_ptr { targets_.at(index).get() },
                .composite_source = observer_ptr { targets_.at(index).get() } },
              forward_ ? ShadingMode::kForward : ShadingMode::kDeferred)
      != kInvalidViewId);
  }
  auto loop = co::testing::TestEventLoop {};
  // Run completes synchronously before the coroutine closure leaves scope.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
  co::Run(loop, [&] -> co::Co<void> {
    co_await renderer_->OnPreRender(observer_ptr { &frame });
    co_await renderer_->OnRender(observer_ptr { &frame });
    co_await renderer_->OnCompositing(observer_ptr { &frame });
  });
  renderer_->OnFrameEnd(observer_ptr { &frame });
  Backend().EndFrame(current, slot);
  const auto ended = Clock::now();
  return { sequence, Milliseconds(ended - started),
    Milliseconds(began - started), Milliseconds(updated - began),
    Milliseconds(ended - updated), frame_shadow_writers_,
    frame_shadow_map_uses_, frame_shadow_backing_uses_ };
}
auto ManyLightBaseline::RunBaseline() -> void
{
#ifndef NDEBUG
  if (measure_) {
    FAIL() << "Timing requires optimized Release; Debug permits reference "
              "validation only.";
  }
#endif
  ASSERT_NO_FATAL_FAILURE(SetupCase());
  const auto verbosity = loguru::g_global_verbosity;
  loguru::g_global_verbosity = loguru::Verbosity_WARNING;
  const auto restore = ScopeGuard([&] noexcept {
    FailureBackend().count_resource_creations = false;
    loguru::g_global_verbosity = verbosity;
  });
  const auto capture_warmup_frames
    = request_.value("capture_warmup_frames", 0U);
  ASSERT_LE(capture_warmup_frames, warmup_frames_);
  const auto warmup_capture = capture_warmup_frames != 0U
    ? BeginOptionalCapture()
    : observer_ptr<graphics::FrameCaptureController> {};
  for (unsigned index = 0; index < warmup_frames_; ++index) {
    (void)RenderFrame(index);
    if (warmup_capture && index + 1U == capture_warmup_frames) {
      WaitForQueueIdle();
      ASSERT_TRUE(warmup_capture->EndCapture());
    }
  }
  ASSERT_NO_FATAL_FAILURE(Capture(0U));
  if (HasFailure()) {
    return;
  }
  captured_.assign(captured_.size(), {});
  // Warm all frame slots again after untimed readbacks, before taking the
  // baseline.
  for (unsigned index = 0; index < 12U; ++index) {
    (void)RenderFrame(index);
  }
  snapshots_.push_back(SnapshotResources());
  if (measure_) {
    samples_.reserve(sample_frames_);
    cpu_ = std::make_unique<CpuTimingCapture>(CpuTimingOptions {
      .record_capacity = CpuTimingRecordCapacity { sample_frames_
        * (256U
          + 32U
            * (options_.point_shadow_requests + options_.spot_shadow_requests)
            * static_cast<unsigned>(workload_.views.size())) },
      .detailed = true });
    recording_ = true;
    FailureBackend().count_resource_creations = true;
    auto previous = Clock::now();
    for (unsigned index = 0; index < sample_frames_; ++index) {
      auto sample = RenderFrame(index, index == 0U);
      const auto ended = Clock::now();
      sample.wall_ms = Milliseconds(ended - previous);
      previous = ended;
      samples_.push_back(sample);
    }
    recording_ = false;
    FailureBackend().count_resource_creations = false;
    snapshots_.push_back(SnapshotResources());
    WaitForQueueIdle();
    (void)RenderFrame(0U);
    WaitForQueueIdle();
  }
  if (options_.moving) {
    for (const auto phase : { 60U, 120U, 180U }) {
      ASSERT_NO_FATAL_FAILURE(Capture(phase));
    }
  }
  ASSERT_NO_FATAL_FAILURE(WriteResults());
}
NOLINT_TEST_F(ManyLightBaseline, DISABLED_RenderAndMeasure) { RunBaseline(); }
} // namespace oxygen::vortex::testing
