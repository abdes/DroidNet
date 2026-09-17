//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
}

namespace oxygen::vortex::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::vortex::internal

namespace {
using namespace oxygen;
using namespace oxygen::graphics;
using namespace oxygen::vortex;
using Pixel = std::array<float, 4>;

class ExposureFailureGraphics final : public graphics::d3d12::Graphics {
public:
  using graphics::d3d12::Graphics::Graphics;
  mutable std::weak_ptr<graphics::Texture> processed_sky;
  auto CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture> override
  {
    auto texture = graphics::d3d12::Graphics::CreateTexture(desc);
    if (desc.debug_name == "Vortex.StaticSkyLight.ProcessedCubemap")
      processed_sky = texture;
    return texture;
  }
  std::vector<std::string> recorder_names;
  bool fail_next_exposure_recorder { false };
  bool fail_next_frame_recorder { false };
  bool fail_next_fallback_recorder { false };
  auto AcquireCommandRecorder(const graphics::QueueKey& queue,
    std::string_view name, bool immediate = true)
    -> std::unique_ptr<graphics::CommandRecorder,
      std::function<void(graphics::CommandRecorder*)>> override
  {
    recorder_names.emplace_back(name);
    if (fail_next_fallback_recorder && name == "Vortex Exposure Fallback") {
      fail_next_fallback_recorder = false;
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    if (fail_next_frame_recorder && name == "Vortex Exposure Frame") {
      fail_next_frame_recorder = false;
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    if (fail_next_exposure_recorder && name == "Vortex Exposure") {
      fail_next_exposure_recorder = false;
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    return graphics::d3d12::Graphics::AcquireCommandRecorder(
      queue, name, immediate);
  }
};

class ExposureGpuTest : public graphics::d3d12::testing::ReadbackTestFixture {
protected:
  auto CheckOffscreenSharing(bool inside_frame) -> void;
  auto CheckSceneExposureRetry(bool inside_frame) -> void;
  auto CreateBackend(const SerializedBackendConfig& config,
    const SerializedPathFinderConfig& paths)
    -> std::shared_ptr<graphics::d3d12::Graphics> override
  {
    return std::make_shared<ExposureFailureGraphics>(config, paths);
  }
  struct Signal {
    std::shared_ptr<const Texture> texture;
    ShaderVisibleIndex srv;
  };
  struct Snapshot {
    ExposureStateData state;
    std::array<std::uint32_t, 264> histogram;
  };

  auto BackendConfigJson() const -> std::string override
  {
    if (!CapturePath().empty())
      return R"({"enable_debug_layer":true,"frame_capture":{"provider":"renderdoc","init_mode":"search"}})";
    return R"({"enable_debug_layer":true})";
  }
  static auto CapturePath() -> std::string
  {
    char* value = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&value, &size, "OXYGEN_EXPOSURE_CAPTURE") != 0 || !value)
      return {};
    const auto owned = std::unique_ptr<char, decltype(&std::free)>(value, &std::free);
    return owned.get();
  }
  auto BeginOptionalCapture() -> observer_ptr<FrameCaptureController>
  {
    const auto path = CapturePath();
    if (path.empty())
      return {};
    WaitForQueueIdle();
    const auto capture = Backend().GetFrameCaptureController();
    CHECK_F(capture && capture->IsAvailable());
    CHECK_F(capture->SetCaptureFileTemplate(path));
    CHECK_F(capture->StartCapture());
    return capture;
  }
  auto PathFinderConfigJson() const -> std::string override
  {
    return R"({"workspace_root_path":")" OXYGEN_EXPOSURE_WORKSPACE R"("})";
  }
  auto SetUp() -> void override
  {
    ReadbackTestFixture::SetUp();
    auto config = RendererConfig {};
    config.upload_queue_key = QueueKeyFor().get();
    renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config);
    pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
    ctx_.current_view.view_id = ViewId { 1U };
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 1U };
    ctx_.frame_slot = frame::Slot { 0U };
  }
  auto TearDown() -> void override
  {
    FlushBackend();
    registered_targets_.clear();
    last_state_.reset();
    pass_.reset();
    if (renderer_) {
      renderer_->OnShutdown();
      renderer_.reset();
    }
    ReadbackTestFixture::TearDown();
  }
  auto MakeSignal(std::uint32_t width, std::uint32_t height,
    std::span<const Pixel> pixels) -> Signal
  {
    CHECK_F(pixels.size() == 1U || pixels.size() == width * height);
    auto texture = CreateRegisteredTexture({
      .width = width,
      .height = height,
      .format = Format::kRGBA32Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "ExposureFloatFixture",
      .is_shader_resource = true,
      .initial_state = ResourceStates::kCommon,
    });
    const auto pitch = ((width * 16U + 255U) / 256U) * 256U;
    auto upload
      = CreateUploadBuffer(SizeBytes { std::uint64_t(pitch) * height });
    std::vector<std::byte> bytes(std::size_t(pitch) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        const auto& pixel = pixels[pixels.size() == 1U ? 0U : y * width + x];
        std::memcpy(
          bytes.data() + std::size_t(y) * pitch + x * 16U, pixel.data(), 16U);
      }
    }
    upload->Update(bytes.data(), bytes.size(), 0U);
    {
      auto recorder = AcquireRecorder("Exposure fixture upload");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        {
          .buffer_offset = 0U,
          .buffer_row_pitch = pitch,
          .buffer_slice_pitch = std::uint64_t(pitch) * height,
          .dst_slice = { .width = width, .height = height, .depth = 1U },
        },
        *texture);
      recorder->RequireResourceStateFinal(
        *texture, ResourceStates::kShaderResource);
    }
    WaitForQueueIdle();
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(
      ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    auto srv = allocator.GetShaderVisibleIndex(handle);
    const auto view = Backend().GetResourceRegistry().RegisterView(*texture,
      std::move(handle),
      TextureViewDescription {
        .format = Format::kRGBA32Float,
        .dimension = TextureType::kTexture2D,
      });
    CHECK_F(view->IsValid());
    return { std::move(texture), srv };
  }
  auto Uniform(float value, std::uint32_t width = 1U, std::uint32_t height = 1U)
    -> Signal
  {
    const auto pixels
      = std::array<Pixel, 1> { Pixel { value, value, value, 1.0F } };
    return MakeSignal(width, height, pixels);
  }
  template <typename Payload>
  auto Read(const Buffer& source, ResourceStates final_state) -> Payload
  {
    auto readback
      = GetReadbackManager()->CreateBufferReadback("Exposure fixture readback");
    {
      auto recorder = AcquireRecorder("Exposure fixture copy");
      recorder->BeginTrackingResourceState(source, final_state, false);
      const auto ticket
        = readback->EnqueueCopy(*recorder, source, { 0U, sizeof(Payload) });
      CHECK_F(ticket.has_value());
      recorder->RequireResourceStateFinal(source, final_state);
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Payload result {};
    std::memcpy(&result, mapped->Bytes().data(), sizeof(result));
    return result;
  }
  auto Run(const Signal& signal, scene::ExposureSettings settings = {},
    float dt = 0.0F, const Signal* mask = nullptr, float inverse_p = 1.0F,
    bool metering_available = true,
    std::optional<ExposureTransitionToken> transition = {},
    std::optional<float> camera_ev = {}, bool temporary_unit = false)
    -> Snapshot
  {
    settings.key = 12.5F;
    const auto resolved = scene::ResolveExposureSettings(settings, camera_ev);
    CHECK_F(resolved.has_value());
    auto config = PostProcessConfig {};
    config.resolved_exposure = *resolved;
    config.temporary_unit_exposure = temporary_unit;
    config.exposure_settings_revision = ++sequence_;
    config.metering_mode = settings.metering_mode;
    config.auto_exposure_min_log_luminance = settings.min_log_luminance;
    config.auto_exposure_log_luminance_range = settings.log_luminance_range;
    config.auto_exposure_min_ev = settings.min_ev;
    config.auto_exposure_max_ev = settings.max_ev;
    config.auto_exposure_low_percentile = settings.low_percentile;
    config.auto_exposure_high_percentile = settings.high_percentile;
    config.auto_exposure_speed_up = settings.speed_up;
    config.auto_exposure_speed_down = settings.speed_down;
    config.auto_exposure_spot_meter_radius = settings.spot_meter_radius;
    ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
    ctx_.delta_time = dt;
    const auto result = pass_->Execute(ctx_, config,
      {
        .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .metering_mask = mask ? mask->texture.get() : nullptr,
        .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex,
        .one_over_pre_exposure = inverse_p,
        .metering_available = metering_available,
        .transition = transition,
      });
    CHECK_F(result.executed);
    last_state_ = result.state;
    auto snapshot
      = Snapshot { .state = Read<ExposureStateData>(*result.exposure_buffer,
                     ResourceStates::kShaderResource) };
    if (result.histogram_buffer) {
      snapshot.histogram = Read<std::array<std::uint32_t, 264>>(
        *result.histogram_buffer, ResourceStates::kCommon);
    }
    return snapshot;
  }
  auto ServicePixel(PostProcessService& service, const Signal& signal,
    scene::ExposureSettings settings = {}, bool diagnostic = false,
    float dt = 0.0F, std::function<void()> before_execute = {},
    engine::ToneMapper tone_mapper = engine::ToneMapper::kNone,
    bool start_new_frame = true) -> float
  {
    settings.key = 12.5F;
    if (start_new_frame)
      ++sequence_;
    ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
    ctx_.delta_time = dt;
    ctx_.render_mode = diagnostic ? RenderMode::kWireframe : RenderMode::kSolid;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings,
      {}, diagnostic, ctx_.GetScene());
    auto config = PostProcessConfig {};
    config.resolved_exposure = accepted.resolved;
    config.exposure_settings_revision = accepted.revision;
    config.auto_exposure_min_ev = settings.min_ev;
    config.auto_exposure_max_ev = settings.max_ev;
    config.auto_exposure_speed_up = settings.speed_up;
    config.auto_exposure_speed_down = settings.speed_down;
    config.enable_auto_exposure
      = settings.enabled && settings.mode == engine::ExposureMode::kAuto;
    config.enable_bloom = false;
    config.bloom_intensity = 0.0F;
    config.tone_mapper = tone_mapper;
    config.gamma = 1.0F;
    service.SetConfig(config);
    if (before_execute)
      before_execute();
    auto output_desc = TextureDesc {};
    output_desc.debug_name = "ExposureServiceOutput";
    output_desc.width = output_desc.height = 4U;
    output_desc.format = Format::kRGBA32Float;
    output_desc.is_render_target = output_desc.is_shader_resource = true;
    output_desc.initial_state = ResourceStates::kCommon;
    auto output = CreateRegisteredTexture(output_desc);
    auto framebuffer = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
    auto textures
      = SceneTextures(Backend(), SceneTexturesConfig { .extent = { 4U, 4U } });
    service.Execute(ctx_.current_view.view_id, ctx_, textures,
      {
        .scene_signal = signal.texture.get(),
        .post_target = observer_ptr<const Framebuffer> { framebuffer.get() },
        .scene_signal_srv = signal.srv,
      });
    if (!service.GetLastExecutionState().tonemap_executed)
      return std::numeric_limits<float>::quiet_NaN();
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Exposure service pixel");
    {
      auto recorder = AcquireRecorder("Exposure service pixel readback");
      CHECK_F(recorder->AdoptKnownResourceState(*output));
      const auto ticket = readback->EnqueueCopy(*recorder, *output,
        {
          .src_slice
          = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U },
        });
      CHECK_F(ticket.has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Pixel pixel {};
    std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
    return pixel[0];
  }

  auto Qualify(const Signal& signal, bool meter,
    scene::ExposureSettings settings = {}, const Signal* mask = nullptr,
    bool coverage = false) -> HdrSuitabilityData
  {
    settings.mode = engine::ExposureMode::kManual;
    const auto config = SharedConfig(settings);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    CHECK_NOTNULL_F(frame.get());
    CHECK_F(RecordShared(signal, config).executed);
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 1U,
      .metering = meter,
      .coverage = coverage } };
    CHECK_F(pass_->EvaluateFp16Products(ctx_, frame, config, products,
      { .metering_mask = mask ? mask->texture.get() : nullptr,
        .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex }));
    return Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
  }

  auto ResetHistory() -> void
  {
    pass_->RemoveViewState(ctx_.current_view.view_state_handle);
  }
  auto PublishExposureOwner(engine::FrameContext& frame, const ViewId intent_id,
    CompositionView::ViewStateHandle handle, scene::ExposureSettings settings,
    ViewId source = kInvalidViewId, bool diagnostic = false,
    std::optional<ShaderDebugMode> debug_override = {}) -> ViewId
  {
    auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
      .height = 4U,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    auto target = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(texture));
    registered_targets_.push_back(target);
    auto view = CompositionView {};
    view.id = intent_id;
    view.view_state_handle = handle;
    view.render_settings.exposure = std::move(settings);
    view.exposure_source_view_id = source;
    view.force_wireframe = diagnostic;
    view.render_settings.shader_debug_mode = debug_override;
    return renderer_->PublishRuntimeCompositionView(frame,
      { .composition_view = view,
        .render_target = observer_ptr { target.get() } });
  }
  auto SharedConfig(scene::ExposureSettings settings = {},
    std::optional<float> camera_ev = {}) -> PostProcessConfig
  {
    settings.key = 12.5F;
    auto config = PostProcessConfig {};
    config.resolved_exposure
      = *scene::ResolveExposureSettings(settings, camera_ev);
    config.exposure_settings_revision = 1U;
    config.auto_exposure_min_ev = settings.min_ev;
    config.auto_exposure_max_ev = settings.max_ev;
    return config;
  }
  auto RecordShared(const Signal& signal, const PostProcessConfig& config,
    const postprocess::ExposurePass::Source* source = nullptr,
    std::optional<ExposureTransitionToken> token = {},
    std::uint64_t lifetime = 0U) -> postprocess::ExposurePass::Result
  {
    return pass_->Execute(ctx_, config,
      { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .transition = token,
        .source = source,
        .lifetime = lifetime });
  }
  auto ReadState(const postprocess::ExposurePass::Result& result)
    -> ExposureStateData
  {
    CHECK_NOTNULL_F(result.exposure_buffer);
    return Read<ExposureStateData>(
      *result.exposure_buffer, ResourceStates::kShaderResource);
  }
  auto OwnedExposureService() -> PostProcessService&
  {
    if (!vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_)) {
      auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
        .height = 4U,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = ResourceStates::kCommon });
      auto target = Backend().CreateFramebuffer(
        FramebufferDesc {}.AddColorAttachment(texture));
      registered_targets_.push_back(target);
      auto params = ResolvedView::Params {};
      params.view_config.viewport = { .width = 4.0F, .height = 4.0F };
      auto session
        = renderer_->ForSinglePassHarness()
            .SetFrameSession({ .frame_slot = frame::Slot { 0U },
              .frame_sequence = frame::SequenceNumber { 1U },
              .delta_time_seconds = 0.0F })
            .SetResolvedView(
              { .view_id = ViewId { 1000U }, .value = ResolvedView { params } })
            .SetOutputTarget({ .framebuffer = observer_ptr { target.get() } })
            .Finalize();
      CHECK_F(session.has_value());
    }
    return *vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_));
  }
  auto StartSharedServiceView(PostProcessService& service,
    engine::FrameContext& frame, scene::ExposureSettings consumer_settings = {})
    -> ViewId
  {
    auto source_settings = scene::ExposureSettings {};
    source_settings.key = 12.5F;
    source_settings.mode = engine::ExposureMode::kManual;
    source_settings.manual_ev = 4.0F;
    const auto root = PublishExposureOwner(frame, ViewId { 50U },
      CompositionView::ViewStateHandle { 50U }, source_settings);
    ctx_.current_view.view_id = root;
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 50U };
    ctx_.current_view.exposure_view_state_handle
      = CompositionView::kInvalidViewStateHandle;
    ServicePixel(service, Uniform(.25F, 4U, 4U), source_settings);
    consumer_settings.key = 12.5F;
    const auto consumer = PublishExposureOwner(frame, ViewId { 60U },
      CompositionView::ViewStateHandle { 60U }, consumer_settings,
      ViewId { 50U });
    ctx_.current_view.view_id = consumer;
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 60U };
    ctx_.current_view.exposure_view_id = root;
    ctx_.current_view.exposure_view_state_handle
      = CompositionView::ViewStateHandle { 50U };
    ServicePixel(service, Uniform(8.0F, 4U, 4U), consumer_settings);
    return consumer;
  }
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<vortex::testing::FakeAssetLoader> owned_asset_loader_;
  std::vector<std::shared_ptr<Framebuffer>> registered_targets_;
  std::unique_ptr<postprocess::ExposurePass> pass_;
  RenderContext ctx_;
  std::uint64_t sequence_ { 0U };
  postprocess::ExposurePass::StateLease last_state_;
};

NOLINT_TEST_F(ExposureGpuTest, ConservedTwoBinMassAndIndependentMeter)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  const auto result = Run(Uniform(0.375F, 17U, 19U), settings);
  // Independent double-precision scatter at L=3/8, window [-12,13].
  const double x = (std::log2(0.375) + 12.0) * 255.0 / 25.0;
  const auto bin = static_cast<unsigned>(std::floor(x));
  const auto upper
    = static_cast<unsigned>(std::floor(4095.0 * (x - bin) + 0.5));
  EXPECT_EQ(result.histogram[bin], 323U * (4095U - upper));
  EXPECT_EQ(result.histogram[bin + 1U], 323U * upper);
  EXPECT_EQ(std::accumulate(
              result.histogram.begin(), result.histogram.begin() + 256, 0U),
    323U * 4095U);
  const double expected_log = -12.0 + (bin + upper / 4095.0) * 25.0 / 255.0;
  EXPECT_NEAR(
    result.state.raw_metered_ev, expected_log - std::log2(0.18), 2e-4);
  EXPECT_NEAR(std::log2(result.state.displayed_scale),
    std::log2(0.18) - expected_log, 2e-4);
  EXPECT_EQ(result.histogram[256], 323U);
  EXPECT_EQ(result.histogram[257], 323U);
  EXPECT_EQ(result.histogram[260], 0U);
}

NOLINT_TEST_F(ExposureGpuTest, MaximumGridMassRemainsBounded)
{
  const auto result = Run(Uniform(0.25F, 1024U, 513U));
  EXPECT_EQ(std::accumulate(
              result.histogram.begin(), result.histogram.begin() + 256, 0U),
    1073479680U);
  EXPECT_EQ(result.histogram[256], 262144U);
  EXPECT_EQ(result.histogram[257], 262144U);
}

NOLINT_TEST_F(ExposureGpuTest, DarkValidAndZeroMaskInvalidRemainDistinct)
{
  const auto dark = Uniform(0.0F);
  const auto zero_mask = Uniform(0.0F);
  const auto result = Run(dark);
  EXPECT_EQ(result.histogram[258], 1U);
  EXPECT_EQ(result.histogram[261], 1U);
  EXPECT_EQ(result.histogram[0], 0U);
  EXPECT_EQ(result.state.flags & 31U, 27U);
  EXPECT_FLOAT_EQ(result.state.displayed_scale, 64.0F);
  const auto invalid = Run(dark, {}, 1.0F, &zero_mask);
  EXPECT_EQ(invalid.histogram[257], 0U);
  EXPECT_EQ(invalid.state.flags & 12U, 0U);
  EXPECT_EQ(invalid.state.fallback_reason, 2U);
  EXPECT_EQ(invalid.state.displayed_scale, result.state.displayed_scale);
}

NOLINT_TEST_F(ExposureGpuTest, NonfiniteSamplesAreRejectedAndNeverBlack)
{
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const auto inf = std::numeric_limits<float>::infinity();
  const auto pixels = std::array { Pixel { nan, 0, 0, 1 },
    Pixel { inf, 0, 0, 1 }, Pixel { .25F, .25F, .25F, 1 } };
  const auto result = Run(MakeSignal(3U, 1U, pixels));
  EXPECT_EQ(result.histogram[256], 1U);
  EXPECT_EQ(result.histogram[257], 1U);
  EXPECT_EQ(result.histogram[258], 0U);
  EXPECT_EQ(result.histogram[260], 2U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, TinyPercentileIntervalRetainsItsContainingBin)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = std::numeric_limits<float>::denorm_min();
  settings.high_percentile = settings.low_percentile * 2.0F;
  const auto result = Run(Uniform(.25F, 512U, 512U), settings);
  EXPECT_EQ(result.state.flags & 12U, 12U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, ZeroTargetRestoresImmediatelyFromLastValidMeter)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  const auto zero_mask = Uniform(0.0F);
  settings.target_luminance = 0.0F;
  const auto black = Run(signal, settings, 0.0F, &zero_mask);
  EXPECT_EQ(black.state.displayed_scale, 0.0F);
  EXPECT_EQ(black.state.latent_scale, initial.state.latent_scale);
  settings.target_luminance = .36F;
  const auto restored = Run(signal, settings, 0.0F, &zero_mask);
  EXPECT_NEAR(restored.state.displayed_scale, 1.44F, 2e-5F);
  EXPECT_EQ(restored.state.flags & 12U, 0U);
}

NOLINT_TEST_F(ExposureGpuTest, ZeroSpeedAndPauseFreezeOrdinaryAdaptation)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -10.0F;
  EXPECT_EQ(
    Run(signal, settings, 0.0F).state.latent_scale, initial.state.latent_scale);
  settings.speed_up = 0.0F;
  EXPECT_EQ(Run(signal, settings, 10.0F).state.latent_scale,
    initial.state.latent_scale);
  settings.compensation_ev = 10.0F;
  settings.speed_down = 0.0F;
  EXPECT_EQ(Run(signal, settings, 10.0F).state.latent_scale,
    initial.state.latent_scale);
}

NOLINT_TEST_F(
  ExposureGpuTest, HybridCrossingMatchesElapsedTimeAcrossFrameSchedules)
{
  const auto signal = Uniform(.25F);
  // Starting target is .72, changed target is .72*2^-8. SpeedUp=3 EV/s,
  // D=1.5: at t=3, remaining error is 1.5*exp(-5/3) stops.
  const double expected = std::log2(.72) - 8.0 + 1.5 * std::exp(-5.0 / 3.0);
  for (const unsigned frequency : { 30U, 60U, 120U }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    Run(signal, settings);
    settings.compensation_ev = -8.0F;
    Snapshot result {};
    for (unsigned frame = 0; frame < 3U * frequency; ++frame) {
      result = Run(signal, settings, 1.0F / frequency);
    }
    EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4)
      << frequency;
  }
}
NOLINT_TEST_F(
  ExposureGpuTest, HugeFiniteSpeedAndDistanceDoNotOverflowTheExponent)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  settings.min_ev = -16.0F;
  settings.max_ev = 16.0F;
  settings.compensation_ev = 8.0F;
  settings.target_luminance = .25F;
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -8.0F;
  settings.speed_up = std::exp2(127.0F);
  settings.transition_distance = std::exp2(127.0F);
  const auto result = Run(signal, settings, 2.0F);
  const double expected = std::log2(double(initial.state.latent_scale)) - 16.0
    + 16.0 * std::exp(-2.0);
  EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4);
}

NOLINT_TEST_F(ExposureGpuTest, TinySpeedTimesHugeDeltaRetainsFiniteLinearTravel)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -8.0F;
  settings.speed_up = std::exp2(-140.0F);
  const auto result = Run(signal, settings, std::exp2(127.0F));
  EXPECT_NEAR(std::log2(result.state.latent_scale),
    std::log2(initial.state.latent_scale) - std::exp2(-13.0F), 1e-5);
}
NOLINT_TEST_F(
  ExposureGpuTest, FractionalPercentileBoundariesMatchIndependentCdf)
{
  const auto pixels
    = std::array { Pixel { .25F, .25F, .25F, 1 }, Pixel { .25F, .25F, .25F, 1 },
        Pixel { .25F, .25F, .25F, 1 }, Pixel { 8, 8, 8, 1 } };
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = .5F;
  settings.high_percentile = .875F;
  const auto result = Run(MakeSignal(4U, 1U, pixels), settings);
  // Retain one .25 sample and half an 8 sample: (-2 + .5*3)/1.5.
  EXPECT_NEAR(result.state.raw_metered_ev, -1.0 / 3.0 - std::log2(.18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, DarkInfluenceDoesNotAttenuatePositiveBinZeroMass)
{
  auto settings = scene::ExposureSettings {};
  settings.min_log_luminance = -2.0F;
  settings.log_luminance_range = 25.0F;
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  const auto pixels = std::array { Pixel { 0, 0, 0, 1 },
    Pixel { .2578125F, .2578125F, .2578125F, 1 } };
  const auto signal = MakeSignal(2U, 1U, pixels);
  const auto zero = Run(signal, settings);
  EXPECT_GT(zero.histogram[0], 0U);
  EXPECT_GT(zero.histogram[1], 0U);
  EXPECT_EQ(zero.histogram[0] + zero.histogram[1], 4095U);
  settings.black_influence = .5F;
  const auto half = Run(signal, settings);
  EXPECT_EQ(half.histogram[0], zero.histogram[0] + 2048U);
  EXPECT_EQ(half.histogram[1], zero.histogram[1]);
}

NOLINT_TEST_F(ExposureGpuTest, BilinearMaskUsesOnlyClampedLinearRed)
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const auto pixels
    = std::array { Pixel { 0, nan, nan, nan }, Pixel { 1, nan, nan, nan } };
  const auto mask = MakeSignal(2U, 1U, pixels);
  const auto result = Run(Uniform(.25F), {}, 0.0F, &mask);
  EXPECT_EQ(result.histogram[102], 2048U);
  EXPECT_EQ(result.histogram[260], 0U);
  const auto high_mask = Uniform(4.0F);
  EXPECT_EQ(Run(Uniform(.25F), {}, 0.0F, &high_mask).histogram[102], 4095U);
  const auto low_mask = Uniform(-1.0F);
  EXPECT_EQ(Run(Uniform(.25F), {}, 0.0F, &low_mask).histogram[257], 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, CoverageUnpremultipliesAndScalesMassOnlyWithBackground)
{
  auto scene = scene::Scene("CoverageFixture", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& background
    = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
  background.SetEnabled(true);
  ctx_.scene = observer_ptr { &scene };
  const auto pixels
    = std::array<Pixel, 1> { Pixel { .125F, .125F, .125F, .5F } };
  const auto signal = MakeSignal(1U, 1U, pixels);
  const auto result = Run(signal);
  EXPECT_EQ(result.histogram[102], 2048U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  background.SetEnabled(false);
  auto untrimmed = scene::ExposureSettings {};
  untrimmed.low_percentile = 0.0F;
  untrimmed.high_percentile = 1.0F;
  const auto opaque = Run(signal, untrimmed);
  EXPECT_EQ(std::accumulate(
              opaque.histogram.begin(), opaque.histogram.begin() + 256, 0U),
    4095U);
  EXPECT_NEAR(opaque.state.raw_metered_ev, std::log2(.125 / .18), 2e-4);
  ctx_.scene.reset();
}

NOLINT_TEST_F(
  ExposureGpuTest, ProfilesAndZeroRadiusUseNormalizedContentCoordinates)
{
  const auto signal = Uniform(.25F, 3U, 1U);
  auto settings = scene::ExposureSettings {};
  settings.metering_mode = engine::MeteringMode::kCenterWeighted;
  EXPECT_EQ(Run(signal, settings).histogram[102], 6825U);
  settings.metering_mode = engine::MeteringMode::kSpot;
  settings.spot_meter_radius = 1.0e-20F;
  EXPECT_EQ(Run(signal, settings).histogram[102], 4095U);
  settings.spot_meter_radius = 0.0F;
  EXPECT_EQ(Run(signal, settings).histogram[102], 4095U);
  EXPECT_EQ(Run(Uniform(.25F, 2U, 1U), settings).histogram[257], 0U);
}

NOLINT_TEST_F(ExposureGpuTest, ContentRectangleExcludesOutputBars)
{
  const auto pixels
    = std::array { Pixel { 8, 8, 8, 1 }, Pixel { .25F, .25F, .25F, 1 },
        Pixel { .25F, .25F, .25F, 1 }, Pixel { 8, 8, 8, 1 } };
  const auto signal = MakeSignal(4U, 1U, pixels);
  auto params = ResolvedView::Params {};
  params.view_config.viewport
    = { .top_left_x = 1.0F, .top_left_y = 0.0F, .width = 2.0F, .height = 1.0F };
  params.view_config.scissor = { .left = 1, .top = 0, .right = 3, .bottom = 1 };
  const auto view = ResolvedView { params };
  ctx_.current_view.resolved_view = observer_ptr { &view };
  const auto result = Run(signal);
  EXPECT_EQ(result.histogram[102], 8190U);
  EXPECT_EQ(result.histogram[256], 2U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  ctx_.current_view.resolved_view.reset();
}

NOLINT_TEST_F(ExposureGpuTest, SceneReferredMeterIsInvariantToInputPreExposure)
{
  const auto ordinary = Run(Uniform(.25F));
  const auto scaled = Run(Uniform(1024.0F), {}, 0.0F, nullptr, 1.0F / 4096.0F);
  EXPECT_EQ(ordinary.histogram, scaled.histogram);
  EXPECT_EQ(ordinary.state.raw_metered_ev, scaled.state.raw_metered_ev);
}

NOLINT_TEST_F(ExposureGpuTest, LongAndIrregularHybridStepsRemainEquivalent)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  Run(signal, settings);
  settings.compensation_ev = -8.0F;
  Snapshot result {};
  for (float dt : { .125F, .875F, .25F, .25F, .5F, 1.0F }) {
    result = Run(signal, settings, dt);
  }
  EXPECT_NEAR(std::log2(result.state.latent_scale),
    std::log2(.72) - 8.0 + 1.5 * std::exp(-5.0 / 3.0), 5e-4);
  const auto settled = Run(signal, settings, 1e30F);
  EXPECT_NEAR(
    std::log2(settled.state.latent_scale), std::log2(.72) - 8.0, 5e-4);
}
NOLINT_TEST_F(ExposureGpuTest, NarrowPercentilesStraddleLargeIntegerCdfBoundary)
{
  std::vector<Pixel> pixels(512U * 512U, Pixel { 8, 8, 8, 1 });
  std::fill_n(pixels.begin(), pixels.size() / 2, Pixel { .25F, .25F, .25F, 1 });
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = std::nextafter(.5F, 0.0F);
  settings.high_percentile = std::nextafter(.5F, 1.0F);
  const auto result = Run(MakeSignal(512U, 512U, pixels), settings);
  // Adjacent binary32 fractions straddle the half-mass boundary with a 1:2
  // retained-mass ratio, independent of the nearly 2^30 total count.
  EXPECT_NEAR(result.state.raw_metered_ev, 4.0 / 3.0 - std::log2(.18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, EightKOutputStillUsesAtMost512SquaredSamples)
{
  const auto result = Run(Uniform(.25F, 7680U, 4320U));
  EXPECT_EQ(result.histogram[256], 262144U);
  EXPECT_EQ(result.histogram[102], 1073479680U);
}
NOLINT_TEST_F(ExposureGpuTest, OrdinaryAutoCurveInterpolatesAtRawMeterEv)
{
  auto settings = scene::ExposureSettings {};
  settings.compensation_curve = { { 0.0F, -2.0F }, { 2.0F, 2.0F } };
  const auto result = Run(Uniform(.25F), settings);
  const double raw_ev = std::log2(.25 / .18);
  // Curve contributes 2*EV-2; the denominator contributes -EV.
  EXPECT_NEAR(std::log2(result.state.target_scale), raw_ev - 2.0, 2e-4);
  EXPECT_NEAR(std::log2(result.state.displayed_scale), raw_ev - 2.0, 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, OrdinaryAutoCurveClampsBothAuthoredEndpoints)
{
  auto settings = scene::ExposureSettings {};
  settings.compensation_curve = { { 1.0F, 2.0F }, { 2.0F, 4.0F } };
  const auto below = Run(Uniform(.25F), settings);
  EXPECT_NEAR(
    std::log2(below.state.target_scale), 2.0 - std::log2(.25 / .18), 2e-4);
  const auto above = Run(Uniform(8.0F), settings);
  EXPECT_NEAR(
    std::log2(above.state.target_scale), 4.0 - std::log2(8.0 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, CurveInputIgnoresEvClampAndAdaptedHistory)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.min_ev = 0.0F;
  settings.max_ev = .1F;
  settings.compensation_curve = { { 0.0F, -2.0F }, { 2.0F, 2.0F } };
  settings.speed_up = settings.speed_down = 0.0F;
  const auto result = Run(signal, settings, 1.0F);
  const double expected = 2.0 * std::log2(.25 / .18) - 2.0 - .1;
  EXPECT_NEAR(std::log2(result.state.target_scale), expected, 2e-4);
  EXPECT_EQ(result.state.latent_scale, initial.state.latent_scale);
}

NOLINT_TEST_F(ExposureGpuTest, BrighteningUsesSpeedDownAcrossFrameSchedules)
{
  const auto signal = Uniform(.25F);
  const double expected = std::log2(.72) + 8.0 - 1.5 * std::exp(-1.0);
  for (const unsigned frequency : { 30U, 60U, 120U }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    Run(signal, settings);
    settings.compensation_ev = 8.0F;
    settings.speed_up = 7.0F;
    settings.speed_down = 1.0F;
    Snapshot result {};
    for (unsigned frame = 0; frame < 8U * frequency; ++frame) {
      result = Run(signal, settings, 1.0F / frequency);
    }
    EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4)
      << frequency;
  }
}

NOLINT_TEST_F(ExposureGpuTest, MovingEdgeHasBoundedGridSamplingError)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  // A 1024x1 content rectangle has 512 cell-centre samples at odd pixels.
  // Moving a sharp five-stop edge by one pixel changes at most one grid cell:
  // geometric-mean error against all pixels is bounded by 5/1024 EV.
  for (const unsigned edge : { 1U, 2U, 3U, 255U, 256U, 257U, 511U, 512U }) {
    std::vector<Pixel> pixels(1024U, Pixel { .25F, .25F, .25F, 1 });
    std::fill_n(pixels.begin(), edge, Pixel { 8, 8, 8, 1 });
    const auto result = Run(MakeSignal(1024U, 1U, pixels), settings);
    EXPECT_EQ(result.histogram[153], (edge / 2U) * 4095U);
    const double exact_sample_ev
      = -2.0 + 5.0 * (edge / 2U) / 512.0 - std::log2(.18);
    const double full_image_ev = -2.0 + 5.0 * edge / 1024.0 - std::log2(.18);
    EXPECT_NEAR(result.state.raw_metered_ev, exact_sample_ev, 2e-4);
    EXPECT_LE(std::abs(result.state.raw_metered_ev - full_image_ev),
      5.0 / 1024.0 + 2e-4);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, SingleBrightPixelRecordsSamplingAliasingWithoutAreaAveraging)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  for (const unsigned x : { 0U, 1U, 2U, 3U, 510U, 511U, 1022U, 1023U }) {
    std::vector<Pixel> pixels(1024U, Pixel { .25F, .25F, .25F, 1 });
    pixels[x] = Pixel { 8, 8, 8, 1 };
    const auto result = Run(MakeSignal(1024U, 1U, pixels), settings);
    EXPECT_EQ(result.histogram[153], (x % 2U) * 4095U);
    EXPECT_LE(std::abs(result.state.raw_metered_ev
                - (-2.0 + 5.0 / 1024.0 - std::log2(.18))),
      5.0 / 1024.0 + 2e-4);
  }
}
NOLINT_TEST_F(
  ExposureGpuTest, UnavailableMaskPreventsMeterInitializationButNotLockedSolve)
{
  const auto signal = Uniform(.25F);
  const auto pending = Run(signal, {}, 1.0F, nullptr, 1.0F, false);
  EXPECT_EQ(pending.histogram[257], 0U);
  EXPECT_EQ(pending.state.flags & 15U, 0U);
  EXPECT_EQ(pending.state.displayed_scale, 1.0F);
  const auto ready = Run(signal);
  const auto invalid = Run(signal, {}, 1.0F, nullptr, 1.0F, false);
  EXPECT_EQ(invalid.state.displayed_scale, ready.state.displayed_scale);
  EXPECT_EQ(invalid.state.flags & 12U, 0U);
  auto locked = scene::ExposureSettings {};
  locked.min_ev = locked.max_ev = 2.0F;
  EXPECT_EQ(
    Run(signal, locked, 0.0F, nullptr, 1.0F, false).state.displayed_scale,
    .25F);
}

NOLINT_TEST_F(
  ExposureGpuTest, CookedMaskUploadAndResidentLeaseReachProductionHistogram)
{
  auto loader = vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_, observer_ptr { &loader });
  auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  data::pak::render::TexturePayloadHeader header {};
  std::memcpy(&header,
    payload.data() + sizeof(data::pak::core::TextureResourceDesc),
    sizeof(header));
  payload[sizeof(data::pak::core::TextureResourceDesc)
    + header.data_offset_bytes] = 128U;
  auto settings = scene::ExposureSettings {};
  settings.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  const auto tag = internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(tag, frame::Slot { 0U });
  service.OnFrameStart(frame::SequenceNumber { 1U }, frame::Slot { 0U });
  EXPECT_EQ(service
              .ResolveViewExposureSettings(
                ctx_.current_view.view_state_handle, settings)
              .mask_status,
    PostProcessService::ExposureMaskStatus::kPending);
  // Each step flushes submitted upload work and observes its completed ticket.
  // This is resource-readiness synchronization, not exposure-settling warmup.
  for (unsigned i = 0U; i < 8U; ++i) {
    WaitForQueueIdle();
    const auto slot = frame::Slot { (i + 1U) % 3U };
    renderer_->GetUploadCoordinator().OnFrameStart(tag, slot);
    service.OnFrameStart(frame::SequenceNumber { i + 2U }, slot);
    if (service
          .ResolveViewExposureSettings(
            ctx_.current_view.view_state_handle, settings)
          .mask_status
      == PostProcessService::ExposureMaskStatus::kReady) {
      break;
    }
  }
  const auto& accepted = service.ResolveViewExposureSettings(
    ctx_.current_view.view_state_handle, settings);
  ASSERT_EQ(
    accepted.mask_status, PostProcessService::ExposureMaskStatus::kReady);
  ASSERT_NE(accepted.mask, nullptr);
  const auto mask = Signal { accepted.mask->texture, accepted.mask->srv };
  const auto result = Run(Uniform(.25F), settings, 0.0F, &mask);
  EXPECT_EQ(result.histogram[102], 2056U); // round-half-up(4095*128/255).
  EXPECT_EQ(result.histogram[257], 1U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, LockedServiceGainReachesTonemapDespitePendingOrFailedMask)
{
  auto loader = vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_, observer_ptr { &loader });
  const auto signal = Uniform(1.0F, 4U, 4U);
  auto output_desc = TextureDesc {};
  output_desc.width = output_desc.height = 4U;
  output_desc.format = Format::kRGBA32Float;
  output_desc.is_render_target = true;
  output_desc.initial_state = ResourceStates::kCommon;
  const auto output = CreateRegisteredTexture(output_desc);
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto textures
    = SceneTextures(Backend(), SceneTexturesConfig { .extent = { 4U, 4U } });
  unsigned id = 1U;
  for (bool previous : { false, true }) {
    for (bool failure : { false, true }) {
      const auto handle = CompositionView::ViewStateHandle { id++ };
      ctx_.current_view.view_state_handle = handle;
      ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
      service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
      auto requested = scene::ExposureSettings {};
      requested.key = 12.5F;
      if (previous) {
        static_cast<void>(
          service.ResolveViewExposureSettings(handle, requested));
      }
      requested.metering_mask = loader.MintSyntheticTextureKey();
      EXPECT_EQ(
        service.ResolveViewExposureSettings(handle, requested).mask_status,
        PostProcessService::ExposureMaskStatus::kPending);
      if (failure) {
        ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
        service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
        EXPECT_EQ(
          service.ResolveViewExposureSettings(handle, requested).mask_status,
          PostProcessService::ExposureMaskStatus::kFailed);
      }
      requested.min_ev = requested.max_ev = 2.0F;
      const auto& accepted
        = service.ResolveViewExposureSettings(handle, requested);
      ASSERT_EQ(accepted.resolved.authored.min_ev, 2.0F);
      auto config = PostProcessConfig {};
      config.resolved_exposure = accepted.resolved;
      config.auto_exposure_min_ev = config.auto_exposure_max_ev = 2.0F;
      config.enable_bloom = false;
      config.bloom_intensity = 0.0F;
      config.tone_mapper = engine::ToneMapper::kNone;
      config.gamma = 1.0F;
      service.SetConfig(config);
      ctx_.delta_time = 0.0F;
      service.Execute(ctx_.current_view.view_id, ctx_, textures,
        {
          .scene_signal = signal.texture.get(),
          .post_target = observer_ptr<const Framebuffer> { framebuffer.get() },
          .scene_signal_srv = signal.srv,
        });
      EXPECT_TRUE(service.GetLastExecutionState().tonemap_executed);
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Locked service result");
      {
        auto recorder = AcquireRecorder("Read locked service tonemap");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*output));
        const auto ticket = readback->EnqueueCopy(*recorder, *output,
          {
            .src_slice
            = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U },
          });
        ASSERT_TRUE(ticket.has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      Pixel pixel {};
      std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(pixel[channel], .25F, 2e-5F) << previous << failure;
      }
    }
  }
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, NarrowCdfBoundariesRetainIntegerAndFractionalProductBits)
{
  // Power-of-two CDF boundaries exercise both sides of the 32-bit word shift
  // and narrow fractional tails. Below each exact boundary binary32 spacing
  // is half the spacing above it, giving retained mass ratio 1:2.
  for (unsigned low_pixels : { 1U, 512U, 32768U, 131072U }) {
    std::vector<Pixel> pixels(262144U, Pixel { .25F, .25F, .25F, 1 });
    std::fill_n(pixels.begin(), low_pixels,
      Pixel { 1.0F / 128.0F, 1.0F / 128.0F, 1.0F / 128.0F, 1 });
    auto settings = scene::ExposureSettings {};
    const float boundary = static_cast<float>(low_pixels) / 262144.0F;
    settings.low_percentile = std::nextafter(boundary, 0.0F);
    settings.high_percentile = std::nextafter(boundary, 1.0F);
    const auto result = Run(MakeSignal(512U, 512U, pixels), settings);
    EXPECT_EQ(result.histogram[51], low_pixels * 4095U);
    EXPECT_EQ(result.histogram[102], (262144U - low_pixels) * 4095U);
    EXPECT_NEAR(result.state.raw_metered_ev, -11.0 / 3.0 - std::log2(.18), 2e-4)
      << low_pixels;
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, DarkCountersSeparateBlackPositiveBelowWindowAndNegative)
{
  const auto pixels = std::array { Pixel { 0, 0, 0, 1 },
    Pixel { 0x1p-20F, 0x1p-20F, 0x1p-20F, 1 },
    Pixel { -.25F, -.25F, -.25F, 1 } };
  const auto result = Run(MakeSignal(3U, 1U, pixels));
  EXPECT_EQ(result.histogram[256], 3U);
  EXPECT_EQ(result.histogram[257], 3U);
  EXPECT_EQ(result.histogram[258], 1U);
  EXPECT_EQ(result.histogram[259], 1U);
  EXPECT_EQ(result.histogram[260], 0U);
  EXPECT_EQ(result.histogram[261], 3U);
  EXPECT_EQ(result.state.flags & 31U, 27U);
  EXPECT_EQ(result.state.raw_metered_ev, -6.0F);
  EXPECT_EQ(result.state.displayed_scale, 64.0F);
}

NOLINT_TEST_F(ExposureGpuTest,
  AllNonfiniteInputKeepsAutoEvZeroFallbackIndependentOfManualEv)
{
  auto settings = scene::ExposureSettings {};
  settings.manual_ev = 31.0F;
  const auto result
    = Run(Uniform(std::numeric_limits<float>::quiet_NaN()), settings);
  EXPECT_EQ(result.histogram[256], 0U);
  EXPECT_EQ(result.histogram[260], 1U);
  EXPECT_EQ(result.state.flags & 15U, 0U);
  EXPECT_EQ(result.state.displayed_scale, 1.0F);
  EXPECT_EQ(result.state.latent_scale, 1.0F);
}

NOLINT_TEST_F(
  ExposureGpuTest, ZeroTargetContinuesLatentAdaptationAndUpdatesLastValidMeter)
{
  auto settings = scene::ExposureSettings {};
  Run(Uniform(.25F), settings);
  settings.target_luminance = 0.0F;
  const auto dark_output = Run(Uniform(8.0F), settings, 2.0F);
  EXPECT_EQ(dark_output.state.displayed_scale, 0.0F);
  EXPECT_EQ(dark_output.state.target_scale, 0.0F);
  EXPECT_NEAR(dark_output.state.latent_target_scale, .0225F, 2e-6);
  EXPECT_NEAR(std::log2(dark_output.state.latent_scale),
    std::log2(.0225) + 1.5 * std::exp(-5.0 / 3.0), 5e-4);
  EXPECT_NEAR(dark_output.state.raw_metered_ev, std::log2(8.0 / .18), 2e-4);
  settings.target_luminance = .36F;
  const auto restored
    = Run(Uniform(0.0F), settings, 0.0F, nullptr, 1.0F, false);
  EXPECT_NEAR(restored.state.displayed_scale, .045F, 2e-6);
}

NOLINT_TEST_F(ExposureGpuTest, LockedCurveUsesBoundInsteadOfRawMeteredEv)
{
  auto settings = scene::ExposureSettings {};
  settings.min_ev = settings.max_ev = 2.0F;
  settings.compensation_curve = { { 0.0F, -2.0F }, { 4.0F, 6.0F } };
  EXPECT_NEAR(Run(Uniform(.25F), settings).state.displayed_scale, 1.0F, 2e-6);
  EXPECT_NEAR(Run(Uniform(8.0F), settings).state.displayed_scale, 1.0F, 2e-6);
  EXPECT_NEAR(Run(Uniform(0.0F), settings, 0.0F, nullptr, 1.0F, false)
                .state.displayed_scale,
    1.0F, 2e-6);
}

NOLINT_TEST_F(
  ExposureGpuTest, SubnormalCurveCoordinatesInterpolateAtExactZeroMeterEv)
{
  for (const float coordinate :
    { 1.0e-40F, std::numeric_limits<float>::denorm_min() }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    settings.min_log_luminance = std::log2(.18F);
    settings.log_luminance_range = 25.0F;
    settings.low_percentile = 0.0F;
    settings.high_percentile = 0x1p-16F;
    settings.compensation_curve
      = { { -coordinate, -1.0F }, { coordinate, 1.0F } };
    // Brighter than the dark threshold, but entirely quantized to bin zero.
    // The exact bin position gives EV zero and midpoint compensation zero.
    const auto result = Run(Uniform(.1800001F), settings);
    ASSERT_EQ(result.histogram[261], 0U);
    ASSERT_EQ(result.histogram[0], 4095U);
    ASSERT_EQ(result.state.raw_metered_ev, 0.0F);
    EXPECT_NEAR(result.state.target_scale, 1.0F, 2e-5F);
    EXPECT_NEAR(result.state.displayed_scale, 1.0F, 2e-5F);
  }
}

NOLINT_TEST_F(ExposureGpuTest, PublicPausedFrameSessionFreezesGpuAdaptation)
{
  const auto before = Run(Uniform(.25F));
  auto output_desc = TextureDesc {};
  output_desc.width = output_desc.height = 1U;
  output_desc.format = Format::kRGBA8UNorm;
  output_desc.is_render_target = output_desc.is_shader_resource = true;
  output_desc.initial_state = ResourceStates::kCommon;
  const auto output = CreateRegisteredTexture(output_desc);
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto params = ResolvedView::Params {};
  params.view_config.viewport = { .width = 1.0F, .height = 1.0F };
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = frame::Slot { 0U },
    .frame_sequence = frame::SequenceNumber { 2U },
    .delta_time_seconds = 0.0F,
  });
  facade.SetResolvedView(Renderer::ResolvedViewInput {
    .view_id = ViewId { 1U }, .value = ResolvedView { params } });
  facade.SetOutputTarget(Renderer::OutputTargetInput {
    .framebuffer = observer_ptr<Framebuffer> { framebuffer.get() } });
  const auto paused = facade.Finalize();
  ASSERT_TRUE(paused.has_value());
  ASSERT_EQ(paused->GetRenderContext().delta_time, 0.0F);
  const auto after
    = Run(Uniform(8.0F), {}, paused->GetRenderContext().delta_time);
  EXPECT_EQ(after.state.latent_scale, before.state.latent_scale);
  EXPECT_NE(after.state.latent_target_scale, before.state.latent_target_scale);
}

NOLINT_TEST_F(ExposureGpuTest, ManualCameraAndDisabledWriteUnifiedGpuState)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  const auto manual = Run(Signal {}, settings);
  EXPECT_EQ(manual.state.displayed_scale, 0x1p-14F);
  EXPECT_EQ(manual.state.latent_scale, 0x1p-14F);
  EXPECT_EQ(manual.state.flags & 12U, 0U);
  settings.mode = engine::ExposureMode::kManualCamera;
  const auto camera = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, true, {},
    static_cast<float>(std::log2(15125.0)));
  EXPECT_NEAR(camera.state.displayed_scale, 1.0 / 15125.0, 2e-5 / 15125.0);
  EXPECT_EQ((camera.state.flags >> 10U) & 3U, 1U);
  settings.enabled = false;
  const auto disabled = Run(Signal {}, settings);
  EXPECT_EQ(disabled.state.displayed_scale, 1.0F);
  EXPECT_EQ(disabled.state.latent_scale, 1.0F);
  EXPECT_EQ((disabled.state.flags >> 10U) & 3U, 3U);
}

NOLINT_TEST_F(
  ExposureGpuTest, ManualToAutoPreservesGainForTransitionFrameThenAdapts)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto manual = Run(Uniform(.25F), settings);
  settings.mode = engine::ExposureMode::kAuto;
  const auto transition = Run(Uniform(.25F), settings, 1.0F);
  EXPECT_EQ(transition.state.displayed_scale, manual.state.displayed_scale);
  EXPECT_NEAR(transition.state.target_scale, .72F, 2e-5);
  const auto next = Run(Uniform(.25F), settings, 1.0F);
  EXPECT_NEAR(next.state.displayed_scale, .125F, 2e-5);
}

NOLINT_TEST_F(
  ExposureGpuTest, SeedUsesRequestedEvAndDuplicateGenerationDoesNotReapply)
{
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto event = Run(Uniform(.25F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(event.state.displayed_scale, 0x1p-8F);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next = Run(Uniform(.25F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(next.state.displayed_scale, 0x1p-7F, 2e-5);
  EXPECT_EQ(next.state.applied_generation, event.state.applied_generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemeterRemainsPendingWithoutInputAndAppliesAtZeroDelta)
{
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(token.has_value());
  const auto invalid = Run(Signal {}, {}, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(invalid.state.applied_generation[0], 0U);
  EXPECT_EQ(invalid.state.requested_generation[0], token->generation);
  const auto applied
    = Run(Uniform(.25F), {}, 0.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(applied.state.displayed_scale, .72F, 2e-5);
  EXPECT_EQ(applied.state.applied_generation[0], token->generation);
  const auto retry = Run(Uniform(8.0F), {}, 0.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(retry.state.displayed_scale, applied.state.displayed_scale);
}

NOLINT_TEST_F(
  ExposureGpuTest, RejectedManualSeedDoesNotReactivateOnLaterAutoEntry)
{
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto rejected
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(rejected.state.applied_generation[0], 0U);
  EXPECT_NE(rejected.state.flags & (1U << 12U), 0U);
  settings.mode = engine::ExposureMode::kAuto;
  const auto next
    = Run(Uniform(.25F), settings, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(next.state.displayed_scale, 0x1p-4F);
  EXPECT_EQ(next.state.applied_generation[0], 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, StatelessAutoSolvesEachInvocationWithoutAdaptation)
{
  ctx_.current_view.view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  const auto first = Run(Uniform(.25F), {}, 0.0F);
  const auto next = Run(Uniform(8.0F), {}, 0.0F);
  EXPECT_NEAR(first.state.displayed_scale, .72F, 2e-5);
  EXPECT_NEAR(next.state.displayed_scale, .0225F, 2e-5);
}

NOLINT_TEST_F(ExposureGpuTest, PriorStateLeaseRemainsImmutableAcrossLaterSolves)
{
  const auto first = Run(Uniform(.25F));
  const auto retained = last_state_;
  const auto second = Run(Uniform(8.0F), {}, 10.0F);
  EXPECT_NE(second.state.displayed_scale, first.state.displayed_scale);
  EXPECT_NE(retained->buffer, last_state_->buffer);
  const auto reread = Read<ExposureStateData>(
    *retained->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(reread.displayed_scale, first.state.displayed_scale);
  EXPECT_EQ(reread.frame_sequence, first.state.frame_sequence);
}

NOLINT_TEST_F(ExposureGpuTest,
  TwoViewsInitializeIndependentlyWithoutWaitingBetweenSubmissions)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  auto config = PostProcessConfig {};
  config.resolved_exposure = *scene::ResolveExposureSettings(settings);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto first = pass_->Execute(ctx_, config, {});
  ASSERT_TRUE(first.executed);
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.current_view.view_id = ViewId { 2U };
  settings.manual_ev = 8.0F;
  config.resolved_exposure = *scene::ResolveExposureSettings(settings);
  const auto second = pass_->Execute(ctx_, config, {});
  ASSERT_TRUE(second.executed);
  EXPECT_NE(first.exposure_buffer, second.exposure_buffer);
  EXPECT_EQ(Read<ExposureStateData>(
              *first.exposure_buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-4F);
  EXPECT_EQ(Read<ExposureStateData>(
              *second.exposure_buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
  const auto duplicate = pass_->Execute(ctx_, config, {});
  EXPECT_FALSE(duplicate.executed);
  EXPECT_EQ(duplicate.state, second.state);
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticUnitStatePreservesAutoHistoryAndPendingSeed)
{
  const auto before = Run(Uniform(.25F));
  const auto persistent = last_state_;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  for (unsigned i = 0U; i < 3U; ++i) {
    const auto diagnostic
      = Run(Uniform(8.0F), {}, 5.0F, nullptr, 1.0F, true, *token, {}, true);
    EXPECT_EQ(diagnostic.state.displayed_scale, 1.0F);
    EXPECT_EQ(diagnostic.state.applied_generation[0], 0U);
    const auto retained = Read<ExposureStateData>(
      *persistent->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(retained.displayed_scale, before.state.displayed_scale);
    EXPECT_EQ(retained.applied_generation, before.state.applied_generation);
  }
  const auto resumed
    = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(resumed.state.displayed_scale, 0x1p-8F);
  const auto next = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(next.state.displayed_scale, 0x1p-7F, 2e-5);
}

NOLINT_TEST_F(ExposureGpuTest, DiagnosticUnitStateDoesNotOverwriteManualHistory)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto before = Run(Signal {}, settings);
  const auto persistent = last_state_;
  const auto diagnostic
    = Run(Signal {}, settings, 5.0F, nullptr, 1.0F, true, {}, {}, true);
  EXPECT_EQ(diagnostic.state.displayed_scale, 1.0F);
  EXPECT_EQ(Read<ExposureStateData>(
              *persistent->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    before.state.displayed_scale);
  EXPECT_EQ(Run(Signal {}, settings).state.displayed_scale, 0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, ServiceDiagnosticFramesDoNotAcknowledgePendingTransition)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5);
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  for (unsigned i = 0U; i < 3U; ++i) {
    EXPECT_NEAR(ServicePixel(service, signal, {}, true, 3.0F), .25F, 2e-5);
    EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_requested);
    EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_executed);
    ASSERT_NE(service.InspectBindings(ctx_.current_view.view_id), nullptr);
    EXPECT_EQ(
      service.InspectBindings(ctx_.current_view.view_id)->enable_auto_exposure,
      0U);
    EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
      ExposureTransitionPhase::kQueued);
  }
  EXPECT_NEAR(
    ServicePixel(service, signal, {}, false, 1.0F), .25F / 256.0F, 2e-5);
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(status->applied_generation, token->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, CompletedOldGenerationCannotConsumeNewerQueuedTransition)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto first
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(first.has_value());
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5);
  const auto second = renderer_->QueueExposureTransition(
    first->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(second.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto intermediate = renderer_->InspectExposureTransition(first->target);
  ASSERT_TRUE(intermediate.has_value());
  EXPECT_EQ(intermediate->request.generation, second->generation);
  EXPECT_EQ(intermediate->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(intermediate->applied_generation, first->generation);
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(first->target)->applied_generation,
    second->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, TransitionQueuedAfterFrameCaptureWaitsForNextFrame)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  std::optional<ExposureTransitionToken> token;
  const auto current = ServicePixel(service, signal, {}, false, 0.0F, [&] {
    const auto issued
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    CHECK_F(issued.has_value());
    token = *issued;
  });
  EXPECT_NEAR(current, .18F, 2e-5);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5);
}

NOLINT_TEST_F(
  ExposureGpuTest, RetiredViewAcknowledgementCannotApplyToReusedHandle)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto first
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(first.has_value());
  ServicePixel(service, signal);
  service.RemoveViewState(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle);
  const auto next = renderer_->QueueExposureTransition(
    first->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(next.has_value());
  EXPECT_NE(next->lifetime, first->lifetime);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(next->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(status->applied_generation, 0U);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*first).has_value());
}

NOLINT_TEST_F(ExposureGpuTest, PreserveHoldsOnlyItsEventFrame)
{
  const auto before = Run(Uniform(.25F));
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  const auto event = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(event.state.displayed_scale, before.state.displayed_scale);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(
    next.state.displayed_scale, before.state.displayed_scale / 8.0F, 2e-5F);
}

NOLINT_TEST_F(ExposureGpuTest, SeedOutsideLockedRangeOwnsOnlyItsEventFrame)
{
  auto settings = scene::ExposureSettings {};
  settings.min_ev = settings.max_ev = 4.0F;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(token.has_value());
  const auto event
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(event.state.displayed_scale, 0x1p-12F);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(next.state.displayed_scale, 0x1p-4F);
}

NOLINT_TEST_F(ExposureGpuTest, ServiceManualConsumesExactGpuGain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(4096.0F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (const auto ev : { 14.0F, 16.0F, 32.0F }) {
    settings.manual_ev = ev;
    const auto expected = std::exp2(12.0F - ev);
    EXPECT_NEAR(
      ServicePixel(service, signal, settings), expected, expected * 2e-5F);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, CompletedUnsupportedSeedRejectsWithoutChangingGain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto before = ServicePixel(service, signal);
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 1000.0F);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(ServicePixel(service, signal), before);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kUnsupportedSeed);
  EXPECT_EQ(status->applied_generation, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, LateModeChangeCannotAlterCapturedTransitionSemantics)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto pixel = ServicePixel(service, signal, settings, false, 0.0F, [&] {
    settings.key = 12.5F;
    settings.mode = engine::ExposureMode::kAuto;
    const auto& late = service.ResolveViewExposureSettings(
      ctx_.current_view.view_state_handle, settings);
    auto config = service.GetConfig();
    config.resolved_exposure = late.resolved;
    config.exposure_settings_revision = late.revision;
    config.enable_auto_exposure = true;
    service.SetConfig(config);
  });
  EXPECT_NEAR(pixel, .25F / 16.0F, 2e-5F);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kNotAuto);
  EXPECT_NEAR(
    ServicePixel(service, signal, settings, false, 1.0F), .25F / 16.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, LateMaskRemovalCannotEnableMeteringForCapturedFrame)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.metering_mask = content::ResourceKey { 123U };
  const auto pixel = ServicePixel(service, signal, settings, false, 0.0F, [&] {
    settings.key = 12.5F;
    settings.metering_mask = {};
    const auto& late = service.ResolveViewExposureSettings(
      ctx_.current_view.view_state_handle, settings);
    auto config = service.GetConfig();
    config.resolved_exposure = late.resolved;
    config.exposure_settings_revision = late.revision;
    service.SetConfig(config);
  });
  // The initial invalid-mask fallback uses the canonical key 10 at EV0.
  EXPECT_NEAR(pixel, .25F * .8F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, signal, settings), .18F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharedConsumersUsePriorOwnerStateInBothRenderOrders)
{
  const auto owner_signal = Uniform(.25F);
  const auto consumer_signal = Uniform(8.0F);
  const auto config = SharedConfig();
  const auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  for (bool consumer_first : { false, true }) {
    pass_->RemoveViewState(CompositionView::ViewStateHandle { 1U });
    pass_->RemoveViewState(CompositionView::ViewStateHandle { 2U });
    for (unsigned frame_index = 0; frame_index < 2U; ++frame_index) {
      ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
      ctx_.delta_time = 0.0F;
      postprocess::ExposurePass::Result owner;
      postprocess::ExposurePass::Result consumer;
      const auto render_owner = [&] {
        ctx_.current_view.view_id = ViewId { 1U };
        ctx_.current_view.view_state_handle = source.handle;
        owner = RecordShared(owner_signal, config);
      };
      const auto render_consumer = [&] {
        ctx_.current_view.view_id = ViewId { 2U };
        ctx_.current_view.view_state_handle
          = CompositionView::ViewStateHandle { 2U };
        consumer = RecordShared(consumer_signal, config, &source);
      };
      if (consumer_first) {
        render_consumer();
        render_owner();
      } else {
        render_owner();
        render_consumer();
      }
      ASSERT_TRUE(owner.executed);
      ASSERT_TRUE(consumer.executed);
      EXPECT_EQ(consumer.histogram_buffer, nullptr);
      EXPECT_NE(owner.state, consumer.state);
      EXPECT_NEAR(ReadState(owner).displayed_scale, .72F, 2e-5F);
      const auto borrowed = ReadState(consumer);
      EXPECT_NEAR(
        borrowed.displayed_scale, frame_index == 0U ? 1.0F : .72F, 2e-5F);
      EXPECT_EQ(borrowed.flags & (4U | 8U | 512U), 0U);
      EXPECT_NE(borrowed.flags & 128U, 0U);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, SharedSourceResetBecomesVisibleOnTheNextFrame)
{
  const auto signal = Uniform(.25F);
  const auto config = SharedConfig();
  auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto first = RecordShared(signal, config);
  ASSERT_TRUE(first.executed);
  const auto seed = renderer_->QueueExposureTransition(
    source.handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  source.transition = *seed;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto reset = RecordShared(signal, config, nullptr, *seed);
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto same_frame = RecordShared(signal, config, &source);
  EXPECT_EQ(ReadState(reset).displayed_scale, 0x1p-8F);
  EXPECT_NEAR(ReadState(same_frame).displayed_scale, .72F, 2e-5F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  // The source is inactive in this frame; retain its last completed
  // publication.
  const auto next = RecordShared(signal, config, &source);
  EXPECT_EQ(ReadState(next).displayed_scale, 0x1p-8F);
}

NOLINT_TEST_F(ExposureGpuTest, SharedBootstrapUsesSourceModeBoundsAndSeed)
{
  const auto signal = Uniform(8.0F);
  auto consumer_settings = scene::ExposureSettings {};
  consumer_settings.enabled = false;
  const auto consumer_config = SharedConfig(consumer_settings);
  auto settings = scene::ExposureSettings {};
  settings.min_ev = 4.0F;
  auto source = postprocess::ExposurePass::Source { .handle
    = CompositionView::ViewStateHandle { 1U },
    .config = SharedConfig(settings) };
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto initial = RecordShared(signal, consumer_config, &source);
  EXPECT_EQ(ReadState(initial).displayed_scale, 0x1p-4F);
  EXPECT_EQ(ReadState(initial).fallback_reason, 3U);
  const auto seed = renderer_->QueueExposureTransition(
    source.handle, ExposureTransitionPolicy::kSeedFromEv100, -2.0F);
  ASSERT_TRUE(seed.has_value());
  source.transition = *seed;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    4.0F);
  source.transition.reset();
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    0x1p-14F);
  settings.mode = engine::ExposureMode::kManualCamera;
  source.config
    = SharedConfig(settings, static_cast<float>(std::log2(15125.0)));
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_NEAR(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    1.0 / 15125.0, 2e-5 / 15125.0);
  settings.enabled = false;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    1.0F);
  settings.enabled = true;
  settings.mode = engine::ExposureMode::kAuto;
  settings.target_luminance = 0.0F;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto zero = ReadState(RecordShared(signal, consumer_config, &source));
  EXPECT_EQ(zero.displayed_scale, 0.0F);
  EXPECT_EQ(zero.latent_scale, 0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharedConsumerTransitionIsRejectedAfterGpuCompletion)
{
  auto service = PostProcessService(*renderer_);
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.current_view.exposure_view_id = ViewId { 1U };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 1U };
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto capture = BeginOptionalCapture();
  const auto pixel
    = ServicePixel(service, signal, {}, false, 0.0F, [&] {
        auto source = scene::ExposureSettings {};
        source.key = 12.5F;
        source.mode = engine::ExposureMode::kManual;
        source.manual_ev = 4.0F;
        static_cast<void>(service.CaptureViewExposureSettings(
          ViewId { 1U }, CompositionView::ViewStateHandle { 1U }, source));
      });
  EXPECT_NEAR(pixel, .25F / 16.0F, 2e-5F);
  EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_executed);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kSharedConsumer);
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest,
  RegisteredInactiveSourceBootstrapsConsumersWithoutConsumingItsSeed)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(texture));
  auto view = CompositionView {};
  view.id = ViewId { 50U };
  view.view_state_handle = CompositionView::ViewStateHandle { 50U };
  view.render_settings.exposure = scene::ExposureSettings {};
  view.render_settings.exposure->key = 12.5F;
  const auto published = renderer_->PublishRuntimeCompositionView(frame,
    { .composition_view = view,
      .render_target = observer_ptr { target.get() } });
  ASSERT_NE(published, kInvalidViewId);
  const auto token = renderer_->QueueExposureTransition(
    view.view_state_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  ctx_.current_view.view_id = ViewId { 900U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 900U };
  ctx_.current_view.exposure_view_id = published;
  ctx_.current_view.exposure_view_state_handle = view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F / 256.0F, 2e-5F);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharingDiscardsDormantMeterHistoryAndExplicitDetachRemeters)
{
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto before = Run(Uniform(.25F));
  EXPECT_NEAR(before.state.displayed_scale, .72F, 2e-5F);
  const auto config = SharedConfig();
  const auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  const auto dark = Uniform(8.0F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto shared = ReadState(RecordShared(dark, config, &source));
  EXPECT_EQ(shared.displayed_scale, 1.0F);
  EXPECT_EQ(shared.flags & (4U | 8U | 512U), 0U);
  const auto remeter = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(remeter.has_value());
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  ctx_.delta_time = 0.0F;
  const auto detached
    = ReadState(RecordShared(dark, config, nullptr, *remeter));
  EXPECT_NEAR(detached.displayed_scale, .0225F, 2e-5F);
  EXPECT_EQ(detached.applied_generation[0], remeter->generation);
  EXPECT_EQ(detached.flags & 128U, 0U);
}

NOLINT_TEST_F(ExposureGpuTest,
  BackloggedStatusAcknowledgesLatestSubmissionWithoutRenderingOwnerAgain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F);
  std::optional<ExposureTransitionToken> latest;
  // Withhold CPU delivery while real GPU status copies fill the bounded queue.
  // Subsequent completed states must coalesce into one retained catch-up
  // record.
  for (unsigned i = 1U; i <= 6U; ++i) {
    const auto token
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, static_cast<float>(i));
    ASSERT_TRUE(token.has_value());
    latest = *token;
    const auto result = Run(signal, {}, 0.0F, nullptr, 1.0F, true, *token);
    EXPECT_EQ(result.state.applied_generation[0], token->generation);
    vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
      service, *token, last_state_, ctx_, sequence_);
  }
  const auto full
    = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
      service, latest->target);
  EXPECT_EQ(full.first, frame::kFramesInFlight.get());
  EXPECT_EQ(full.second, 1U);
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 1U });
  EXPECT_EQ(renderer_->InspectExposureTransition(latest->target)->phase,
    ExposureTransitionPhase::kQueued);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(latest->target)->applied_generation,
    3U);
  WaitForQueueIdle();
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 2U });
  const auto completed = renderer_->InspectExposureTransition(latest->target);
  ASSERT_TRUE(completed.has_value());
  EXPECT_EQ(completed->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(completed->applied_generation, latest->generation);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
              service, latest->target),
    (std::pair<std::size_t, std::size_t> { 0U, 0U }));
}

NOLINT_TEST_F(
  ExposureGpuTest, DeferredOldAcknowledgementCannotConsumeNewUnsubmittedIntent)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F);
  std::optional<ExposureTransitionToken> submitted;
  for (unsigned i = 0U; i < 4U; ++i) {
    const auto token
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, static_cast<float>(i));
    ASSERT_TRUE(token.has_value());
    submitted = *token;
    Run(signal, {}, 0.0F, nullptr, 1.0F, true, *token);
    vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
      service, *token, last_state_, ctx_, sequence_);
  }
  const auto pending = renderer_->QueueExposureTransition(
    submitted->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(pending.has_value());
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 1U });
  WaitForQueueIdle();
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 2U });
  const auto status = renderer_->InspectExposureTransition(pending->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->request, *pending);
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(status->applied_generation, submitted->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveInvalidRequestsCannotReactivateAfterSettingsChange)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto signal = Uniform(.25F, 4U, 4U);
  for (unsigned kind = 0U; kind < 3U; ++kind) {
    const auto handle = CompositionView::ViewStateHandle { 50U + kind };
    const auto intent = ViewId { 50U + kind };
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    settings.mode = kind == 0U ? engine::ExposureMode::kManual
                               : engine::ExposureMode::kAuto;
    settings.enabled = kind != 1U;
    const auto view = PublishExposureOwner(frame, intent, handle, settings);
    ASSERT_NE(view, kInvalidViewId);
    const auto token = renderer_->QueueExposureTransition(handle,
      ExposureTransitionPolicy::kSeedFromEv100, kind == 2U ? 1000.0F : 8.0F);
    ASSERT_TRUE(token.has_value());
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    service.CaptureRegisteredExposureControls(ctx_);
    const auto rejected = renderer_->InspectExposureTransition(handle);
    ASSERT_TRUE(rejected.has_value());
    EXPECT_EQ(rejected->phase, ExposureTransitionPhase::kRejected);
    EXPECT_EQ(rejected->error,
      kind == 2U ? ExposureTransitionError::kUnsupportedSeed
                 : ExposureTransitionError::kNotAuto);
    settings.enabled = true;
    settings.mode = engine::ExposureMode::kAuto;
    if (kind == 2U) {
      // The formerly unsupported seed would now produce gain one; the locked
      // target instead produces 1/16. A rejected generation must remain
      // rejected.
      settings.compensation_ev = 1000.0F;
      settings.min_ev = settings.max_ev = 1004.0F;
    }
    ASSERT_EQ(PublishExposureOwner(frame, intent, handle, settings), view);
    ctx_.current_view.view_id = view;
    ctx_.current_view.view_state_handle = handle;
    EXPECT_NEAR(ServicePixel(service, signal, settings),
      kind == 2U ? .25F / 16.0F : .18F, 2e-5F);
    const auto state
      = vortex::testing::RendererPublicationProbe::ExposureStateForView(
        service, handle);
    ASSERT_NE(state, nullptr);
    const auto gpu = Read<ExposureStateData>(
      *state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(gpu.requested_generation[0], token->generation);
    EXPECT_EQ(gpu.applied_generation[0], 0U);
    EXPECT_NE(gpu.flags & (1U << 12U), 0U);
    EXPECT_EQ(renderer_->RetryExposureTransition(*token),
      ExposureTransitionPhase::kRejected);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveModeValidationCannotRejectAnObservedSubmission)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto handle = CompositionView::ViewStateHandle { 50U };
  const auto view
    = PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  Run(Uniform(.25F), settings, 0.0F, nullptr, 1.0F, true, *token);
  service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
    service, *token, last_state_, ctx_, sequence_);
  settings.mode = engine::ExposureMode::kManual;
  PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  WaitForQueueIdle();
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kApplied);
}

NOLINT_TEST_F(ExposureGpuTest, InactiveDiagnosticOwnerPreservesPendingRequest)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  const auto handle = CompositionView::ViewStateHandle { 50U };
  const auto view = PublishExposureOwner(
    frame, ViewId { 50U }, handle, settings, kInvalidViewId, true);
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  settings.mode = engine::ExposureMode::kAuto;
  PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveSharingConsumerRequestStaysRejectedAfterDetach)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(
    frame, ViewId { 50U }, CompositionView::ViewStateHandle { 50U }, settings);
  const auto handle = CompositionView::ViewStateHandle { 60U };
  const auto view = PublishExposureOwner(
    frame, ViewId { 60U }, handle, settings, ViewId { 50U });
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->error,
    ExposureTransitionError::kSharedConsumer);
  PublishExposureOwner(frame, ViewId { 60U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U), settings), .18F, 2e-5F);
  EXPECT_EQ(renderer_->RetryExposureTransition(*token),
    ExposureTransitionPhase::kSuperseded);
}

NOLINT_TEST_F(
  ExposureGpuTest, AutoDetachRemetersAtZeroDeltaWithOneImplicitGeneration)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(
    frame, ViewId { 60U }, ctx_.current_view.view_state_handle, settings);
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  const auto event
    = renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle);
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->request.policy, ExposureTransitionPolicy::kRemeter);
  EXPECT_GT(event->request.generation, 0U);
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F * .0225F, 2e-5F);
  const auto after
    = renderer_->InspectExposureTransition(event->request.target);
  EXPECT_EQ(after->request, event->request);
  EXPECT_EQ(after->phase, ExposureTransitionPhase::kApplied);
}

NOLINT_TEST_F(ExposureGpuTest, ExplicitDetachPolicyOverridesDefaultRemeter)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  for (const auto policy : { ExposureTransitionPolicy::kPreserve,
         ExposureTransitionPolicy::kSeedFromEv100 }) {
    const auto consumer = StartSharedServiceView(service, frame);
    const auto token = renderer_->QueueExposureTransition(
      ctx_.current_view.view_state_handle, policy,
      policy == ExposureTransitionPolicy::kSeedFromEv100
        ? std::optional { 8.0F }
        : std::nullopt);
    ASSERT_TRUE(token.has_value());
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    PublishExposureOwner(frame, ViewId { 60U }, token->target, settings);
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle = token->target;
    EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)),
      policy == ExposureTransitionPolicy::kPreserve ? .5F : 8.0F / 256.0F,
      2e-5F);
    EXPECT_EQ(
      renderer_->InspectExposureTransition(token->target)->request, *token);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FixedModeDetachAppliesAuthoredGainWithoutAnAutoRejection)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  for (const bool enabled : { true, false }) {
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = 8.0F;
    settings.enabled = enabled;
    const auto consumer = StartSharedServiceView(service, frame, settings);
    PublishExposureOwner(
      frame, ViewId { 60U }, ctx_.current_view.view_state_handle, settings);
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle
      = ctx_.current_view.view_state_handle;
    EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
      enabled ? .25F / 256.0F : .25F, 2e-5F);
    service.OnFrameStart(
      frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
    const auto status = renderer_->InspectExposureTransition(
      ctx_.current_view.view_state_handle);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->phase, ExposureTransitionPhase::kApplied);
    EXPECT_EQ(status->request.policy, ExposureTransitionPolicy::kPreserve);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticDetachDefersTheImplicitEventUntilNormalRendering)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(
    frame, ViewId { 60U }, ctx_.current_view.view_state_handle, settings);
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U), settings, true), .25F, 2e-5F);
  EXPECT_FALSE(
    renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle)
      .has_value());
  EXPECT_NEAR(
    ServicePixel(service, Uniform(8.0F, 4U, 4U), settings), .18F, 2e-5F);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle)
      ->request.policy,
    ExposureTransitionPolicy::kRemeter);
}

NOLINT_TEST_F(
  ExposureGpuTest, ReusedLifetimeCannotConsumeOldOwnOrSharedPriorState)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto signal = Uniform(.25F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto old
    = RecordShared(signal, SharedConfig(settings), nullptr, {}, 100U);
  EXPECT_EQ(ReadState(old).displayed_scale, 0x1p-4F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  pass_->OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  const auto config = SharedConfig();
  const auto new_owner = RecordShared(signal, config, nullptr, {}, 101U);
  EXPECT_NEAR(ReadState(new_owner).displayed_scale, .72F, 2e-5F);
  const auto source = postprocess::ExposurePass::Source { .handle
    = CompositionView::ViewStateHandle { 1U },
    .config = config,
    .lifetime = 101U };
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto consumer = RecordShared(signal, config, &source, {}, 200U);
  EXPECT_EQ(ReadState(consumer).displayed_scale, 1.0F);
  EXPECT_EQ(consumer.state->owner_lifetime, 200U);
}

NOLINT_TEST_F(ExposureGpuTest, CanceledDetachDoesNotIssueAnImplicitGeneration)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto handle = ctx_.current_view.view_state_handle;
  PublishExposureOwner(frame, ViewId { 60U }, handle, settings);
  PublishExposureOwner(frame, ViewId { 60U }, handle, settings, ViewId { 50U });
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .5F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
}

NOLINT_TEST_F(ExposureGpuTest,
  PublicHandleReplacementRetiresHistoryRequestsAndMaskOwnership)
{
  auto frame = engine::FrameContext {};
  owned_asset_loader_ = std::make_unique<vortex::testing::FakeAssetLoader>();
  auto output = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto params = ResolvedView::Params {};
  params.view_config.viewport = { .width = 4.0F, .height = 4.0F };
  auto session
    = renderer_->ForSinglePassHarness()
        .SetFrameSession({ .frame_slot = frame::Slot { 0U },
          .frame_sequence = frame::SequenceNumber { 1U },
          .delta_time_seconds = 0.0F })
        .SetResolvedView(
          { .view_id = ViewId { 1000U }, .value = ResolvedView { params } })
        .SetOutputTarget({ .framebuffer = observer_ptr { framebuffer.get() } })
        .Finalize();
  ASSERT_TRUE(session.has_value());
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  vortex::testing::RendererPublicationProbe::SetExposureAssetLoader(
    *service, observer_ptr { owned_asset_loader_.get() });
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  settings.metering_mask
    = owned_asset_loader_->PreloadCookedTexture(std::span(payload));
  const auto h1 = CompositionView::ViewStateHandle { 50U };
  const auto h2 = CompositionView::ViewStateHandle { 51U };
  const auto first_view
    = PublishExposureOwner(frame, ViewId { 50U }, h1, settings);
  std::weak_ptr<const resources::TextureBinder::ReadyTexture> old_mask;
  for (unsigned i = 0U; i < 8U; ++i) {
    WaitForQueueIdle();
    ctx_.frame_slot = frame::Slot { i % 3U };
    renderer_->GetUploadCoordinator().OnFrameStart(
      internal::RendererTagFactory::Get(), ctx_.frame_slot);
    service->OnFrameStart(
      frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
    const auto& ready = service->ResolveViewExposureSettings(h1, settings);
    if (ready.mask) {
      old_mask = ready.mask;
      break;
    }
  }
  ASSERT_FALSE(old_mask.expired());
  const auto mask_slot = ctx_.frame_slot;
  ctx_.current_view.view_id = first_view;
  ctx_.current_view.view_state_handle = h1;
  const auto applied = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
  ASSERT_TRUE(applied.has_value());
  EXPECT_NEAR(ServicePixel(*service, Uniform(.25F, 4U, 4U), settings),
    .25F / 16.0F, 2e-5F);
  const auto old_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, h1);
  ASSERT_NE(old_state, nullptr);
  const auto pending = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(pending.has_value());
  const auto old_mask_key = settings.metering_mask;
  settings.metering_mask = {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 8.0F;
  ASSERT_EQ(
    PublishExposureOwner(frame, ViewId { 50U }, h2, settings), first_view);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*pending).has_value());
  EXPECT_FALSE(renderer_->RetryExposureTransition(*applied).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              *service, h1),
    nullptr);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
              *service, h1),
    (std::pair<std::size_t, std::size_t> { 0U, 0U }));
  owned_asset_loader_->EmitTextureEviction(
    old_mask_key, content::EvictionReason::kRefCountZero);
  EXPECT_FALSE(old_mask.expired());
  const auto next_view
    = PublishExposureOwner(frame, ViewId { 70U }, h1, settings);
  const auto next = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(next.has_value());
  EXPECT_NE(next->lifetime, applied->lifetime);
  ctx_.current_view.view_id = next_view;
  ctx_.current_view.view_state_handle = h1;
  ctx_.frame_slot = frame::Slot { (mask_slot.get() + 1U) % 3U };
  EXPECT_NEAR(ServicePixel(*service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-5F);
  EXPECT_EQ(
    Read<ExposureStateData>(*old_state->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
  EXPECT_FALSE(old_mask.expired());
  WaitForQueueIdle();
  service->OnFrameStart(frame::SequenceNumber { ++sequence_ }, mask_slot);
  EXPECT_TRUE(old_mask.expired());
  const auto next_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, h1);
  ASSERT_NE(next_state, nullptr);
  const auto last = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(last.has_value());
  PublishExposureOwner(
    frame, ViewId { 70U }, CompositionView::kInvalidViewStateHandle, settings);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*last).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              *service, h1),
    nullptr);
  EXPECT_EQ(Read<ExposureStateData>(
              *next_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemovedSourcePreservesOneFrameThenAdaptsIndependently)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  const auto signal = Uniform(8.0F, 4U, 4U);
  EXPECT_NEAR(ServicePixel(service, signal, {}, false, 1.0F), .5F, 2e-5F);
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, ctx_.current_view.view_state_handle);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource)
      .fallback_reason,
    4U);
  const double target = std::log2(.0225);
  const double expected = target + (-4.0 - target) * std::exp(-2.0);
  EXPECT_NEAR(std::log2(ServicePixel(service, signal, {}, false, 1.0F) / 8.0),
    expected, 5e-4);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemovedZeroSourcePreservesBlackOnlyForContinuityFrame)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  auto zero = scene::ExposureSettings {};
  zero.key = 12.5F;
  zero.target_luminance = 0.0F;
  zero.min_ev = zero.max_ev = 0.0F;
  const auto source = PublishExposureOwner(
    frame, ViewId { 50U }, CompositionView::ViewStateHandle { 50U }, zero);
  ctx_.current_view.view_id = source;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), zero), 0.0F, 2e-5F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 60U };
  ctx_.current_view.exposure_view_id = source;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), 0.0F, 2e-5F);
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(8.0F, 4U, 4U), {}, false, 1.0F), 0.0F, 2e-5F);
  static_cast<void>(ServicePixel(
    service, Uniform(std::numeric_limits<float>::quiet_NaN(), 4U, 4U)));
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, ctx_.current_view.view_state_handle);
  const auto recovered
    = Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(recovered.displayed_scale, 1.0F);
  EXPECT_EQ(recovered.latent_scale, 1.0F);
  EXPECT_EQ(recovered.flags & 12U, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemovedNeverRenderedSourceUsesCapturedFallbackAcrossChain)
{
  auto frame = engine::FrameContext {};
  auto source_settings = scene::ExposureSettings {};
  source_settings.key = 12.5F;
  const auto source_handle = CompositionView::ViewStateHandle { 50U };
  PublishExposureOwner(frame, ViewId { 50U }, source_handle, source_settings);
  auto local = source_settings;
  const auto middle = PublishExposureOwner(frame, ViewId { 60U },
    CompositionView::ViewStateHandle { 60U }, local, ViewId { 50U });
  const auto leaf = PublishExposureOwner(frame, ViewId { 70U },
    CompositionView::ViewStateHandle { 70U }, local, ViewId { 60U });
  const auto seed = renderer_->QueueExposureTransition(
    source_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  // No SceneRenderer, source state or consumer state exists at removal.
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
  auto& service = OwnedExposureService();
  for (const auto [view, handle] :
    { std::pair { leaf, CompositionView::ViewStateHandle { 70U } },
      std::pair { middle, CompositionView::ViewStateHandle { 60U } } }) {
    ctx_.current_view.view_id = view;
    ctx_.current_view.view_state_handle = handle;
    ctx_.current_view.exposure_view_id = view;
    ctx_.current_view.exposure_view_state_handle = handle;
    EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), {}, false, 1.0F),
      .25F / 256.0F, 2e-5F);
  }
  EXPECT_FALSE(renderer_->RetryExposureTransition(*seed).has_value());
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticSiblingCannotReplayAnotherConsumersSourceLoss)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto signal = Uniform(8.0F, 4U, 4U);
  for (const bool retire_first : { false, true }) {
    const auto first = StartSharedServiceView(service, frame);
    const auto root = renderer_->ResolvePublishedRuntimeViewId(ViewId { 50U });
    const auto second = PublishExposureOwner(frame, ViewId { 70U },
      CompositionView::ViewStateHandle { 70U }, settings, ViewId { 50U });
    const auto select = [&](ViewId view, std::uint64_t handle) {
      ctx_.current_view.view_id = view;
      ctx_.current_view.view_state_handle
        = CompositionView::ViewStateHandle { handle };
      ctx_.current_view.exposure_view_id = view;
      ctx_.current_view.exposure_view_state_handle
        = ctx_.current_view.view_state_handle;
    };
    select(second, 70U);
    ctx_.current_view.exposure_view_id = root;
    ctx_.current_view.exposure_view_state_handle
      = CompositionView::ViewStateHandle { 50U };
    EXPECT_NEAR(ServicePixel(service, signal), .5F, 2e-5F);
    PublishExposureOwner(frame, ViewId { 70U },
      CompositionView::ViewStateHandle { 70U }, settings, ViewId { 50U }, true);
    renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
    select(first, 60U);
    EXPECT_NEAR(ServicePixel(service, signal, {}, false, 1.0F), .5F, 2e-5F);
    if (retire_first)
      renderer_->RemovePublishedRuntimeView(frame, ViewId { 60U });
    const double target = std::log2(.0225);
    for (unsigned i = 1U; i <= 3U; ++i) {
      select(second, 70U);
      EXPECT_NEAR(ServicePixel(service, signal, {}, true, 1.0F), 1.0F, 2e-5F);
      if (retire_first) {
        EXPECT_FALSE(
          vortex::testing::RendererPublicationProbe::HasExposureViewState(
            service, CompositionView::ViewStateHandle { 60U }));
      } else {
        select(first, 60U);
        EXPECT_NEAR(
          std::log2(ServicePixel(service, signal, {}, false, 1.0F) / 8.0),
          target + (-4.0 - target) * std::exp(-2.0 * i), 5e-4);
      }
    }
    PublishExposureOwner(frame, ViewId { 70U },
      CompositionView::ViewStateHandle { 70U }, settings);
    select(second, 70U);
    EXPECT_NEAR(ServicePixel(service, signal, {}, false, 1.0F), .5F, 2e-5F);
    if (!retire_first) {
      select(first, 60U);
      EXPECT_NEAR(
        std::log2(ServicePixel(service, signal, {}, false, 1.0F) / 8.0),
        target + (-4.0 - target) * std::exp(-8.0), 5e-4);
    } else {
      EXPECT_FALSE(
        vortex::testing::RendererPublicationProbe::HasExposureViewState(
          service, CompositionView::ViewStateHandle { 60U }));
    }
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, RemovedSourceHonorsLocalFixedZeroAndExplicitPolicies)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  for (unsigned kind = 0U; kind < 5U; ++kind) {
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    if (kind == 0U) {
      settings.mode = engine::ExposureMode::kManual;
      settings.manual_ev = 8.0F;
    }
    if (kind == 1U)
      settings.enabled = false;
    if (kind == 2U)
      settings.target_luminance = 0.0F;
    const auto consumer = StartSharedServiceView(service, frame, settings);
    std::optional<ExposureTransitionToken> explicit_request;
    if (kind >= 3U) {
      const auto issued = renderer_->QueueExposureTransition(
        ctx_.current_view.view_state_handle,
        kind == 3U ? ExposureTransitionPolicy::kSeedFromEv100
                   : ExposureTransitionPolicy::kRemeter,
        kind == 3U ? std::optional { 8.0F } : std::nullopt);
      ASSERT_TRUE(issued.has_value());
      explicit_request = *issued;
    }
    renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle
      = ctx_.current_view.view_state_handle;
    const float expected = kind == 0U || kind == 3U ? .25F / 256.0F
      : kind == 1U                                  ? .25F
      : kind == 2U                                  ? 0.0F
                                                    : .18F;
    EXPECT_NEAR(
      ServicePixel(service, Uniform(.25F, 4U, 4U), settings), expected, 2e-5F);
    if (explicit_request)
      EXPECT_EQ(
        renderer_->InspectExposureTransition(explicit_request->target)->request,
        *explicit_request);
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  RemovedSourceUsesLastSelectedFallbackAfterConsumerCopyFailure)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  const auto consumer_handle = CompositionView::ViewStateHandle { 60U };
  const auto old
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, consumer_handle);
  ASSERT_NE(old, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*old->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto source_handle = CompositionView::ViewStateHandle { 50U };
  const auto root
    = PublishExposureOwner(frame, ViewId { 50U }, source_handle, settings);
  for (unsigned i = 0; i < 4U; ++i) {
    const auto seed = renderer_->QueueExposureTransition(
      source_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    ASSERT_TRUE(seed.has_value());
  }
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle = source_handle;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  const auto signal = Uniform(.25F, 4U, 4U);
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle = consumer_handle;
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle = source_handle;
  static_cast<ExposureFailureGraphics&>(Backend()).fail_next_exposure_recorder
    = true;
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5F);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, consumer_handle),
    old);
  const auto selected
    = vortex::testing::RendererPublicationProbe::SelectedBorrowForView(
      service, consumer_handle);
  ASSERT_NE(selected, nullptr);
  EXPECT_NE(selected, old);
  EXPECT_EQ(
    Read<ExposureStateData>(*selected->buffer, ResourceStates::kShaderResource)
      .applied_generation[0],
    4U);
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle = consumer_handle;
  EXPECT_NEAR(
    ServicePixel(service, signal, {}, false, 1.0F), .25F / 256.0F, 2e-5F);
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, consumer_handle);
  const auto continuity
    = Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(continuity.displayed_scale, 0x1p-8F);
  EXPECT_EQ(continuity.latent_scale, 0x1p-8F);
  EXPECT_EQ(continuity.applied_generation[0], 1U);
  EXPECT_EQ(
    Read<ExposureStateData>(*selected->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-8F);
  EXPECT_NEAR(
    ServicePixel(service, signal, {}, false, 1.0F), .25F / 128.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, CameraCutRemetersOnceAndInvalidatesOnlyItsCameraHistory)
{
  auto service = PostProcessService(*renderer_);
  const auto handle = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  auto& history
    = vortex::testing::RendererPublicationProbe::PreviousViewHistory(
      *renderer_);
  auto state = internal::PreviousViewHistoryCache::CurrentState {};
  state.viewport = { .width = 4.0F, .height = 4.0F };
  history.BeginFrame(1U, {});
  history.TouchCurrent(handle, state);
  history.TouchCurrent(CompositionView::ViewStateHandle { 99U }, state);
  history.EndFrame();
  history.BeginFrame(2U, {});
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_FALSE(history.TouchCurrent(handle, state).previous_valid);
  EXPECT_TRUE(
    history.TouchCurrent(CompositionView::ViewStateHandle { 99U }, state)
      .previous_valid);
  const auto event = renderer_->InspectExposureTransition(handle);
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->request.policy, ExposureTransitionPolicy::kRemeter);
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F * .0225F, 2e-5F);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(handle)->request, event->request);
}

NOLINT_TEST_F(ExposureGpuTest, ExplicitSeedOverridesCameraCutDefaultPolicy)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  const auto seed = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F / 256.0F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, *seed);
}

NOLINT_TEST_F(ExposureGpuTest,
  BorrowingCameraCutPreservesRootExposureWithoutRequestingReset)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  StartSharedServiceView(service, frame);
  const auto consumer = ctx_.current_view.view_state_handle;
  const auto root_before
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, CompositionView::ViewStateHandle { 50U });
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(consumer, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .5F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(consumer).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, CompositionView::ViewStateHandle { 50U }),
    root_before);
}

NOLINT_TEST_F(
  ExposureGpuTest, WorldReplacementRemetersButOrdinaryImageChangesAdapt)
{
  auto service = PostProcessService(*renderer_);
  auto first = std::make_shared<scene::Scene>("FirstExposureWorld", 4U);
  auto second = std::make_shared<scene::Scene>("SecondExposureWorld", 4U);
  ctx_.scene = observer_ptr { first.get() };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), 1.0F, 2e-5F);
  ctx_.scene = observer_ptr { second.get() };
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle)
      ->request.policy,
    ExposureTransitionPolicy::kRemeter);
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticFramesDeferCameraCutUntilNormalExposureResumes)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U), {}, true), .25F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
}

NOLINT_TEST_F(ExposureGpuTest,
  RegisteredDebugOverrideControlsCameraCutsIndependentlyOfGlobalMode)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto normal_handle = CompositionView::ViewStateHandle { 81U };
  const auto debug_handle = CompositionView::ViewStateHandle { 82U };
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto normal = PublishExposureOwner(frame, ViewId { 81U }, normal_handle,
    settings, kInvalidViewId, false, ShaderDebugMode::kDisabled);
  auto debug = PublishExposureOwner(frame, ViewId { 82U }, debug_handle,
    settings, kInvalidViewId, false, ShaderDebugMode::kDisabled);
  const auto dim = Uniform(.25F, 4U, 4U);
  const auto bright = Uniform(8.0F, 4U, 4U);
  const auto run = [&](ViewId id, CompositionView::ViewStateHandle handle,
                     const Signal& signal, bool diagnostic) {
    ctx_.current_view.view_id = id;
    ctx_.current_view.view_state_handle = handle;
    ctx_.shader_debug_mode = diagnostic ? ShaderDebugMode::kWorldNormals
                                        : ShaderDebugMode::kDisabled;
    return ServicePixel(service, signal, settings, diagnostic, 0.0F, {},
      engine::ToneMapper::kNone, false);
  };
  const auto capture_frame = [&] {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    ctx_.shader_debug_mode = ShaderDebugMode::kWorldNormals;
    ctx_.render_mode = RenderMode::kSolid;
    service.CaptureRegisteredExposureControls(ctx_);
  };
  renderer_->SetShaderDebugMode(ShaderDebugMode::kWorldNormals);
  capture_frame();
  EXPECT_NEAR(run(normal, normal_handle, dim, false), .18F, 2e-5F);
  EXPECT_NEAR(run(debug, debug_handle, dim, false), .18F, 2e-5F);

  ASSERT_EQ(PublishExposureOwner(frame, ViewId { 82U }, debug_handle, settings,
              kInvalidViewId, false, ShaderDebugMode::kWorldNormals),
    debug);
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(normal_handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(debug_handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  capture_frame();
  EXPECT_NEAR(run(normal, normal_handle, bright, false), .18F, 2e-5F);
  EXPECT_NEAR(run(debug, debug_handle, dim, true), .25F, 2e-5F);
  EXPECT_TRUE(renderer_->InspectExposureTransition(normal_handle).has_value());
  EXPECT_FALSE(renderer_->InspectExposureTransition(debug_handle).has_value());

  ASSERT_EQ(PublishExposureOwner(frame, ViewId { 82U }, debug_handle, settings,
              kInvalidViewId, false, ShaderDebugMode::kDisabled),
    debug);
  capture_frame();
  EXPECT_NEAR(run(debug, debug_handle, bright, false), .18F, 2e-5F);
  EXPECT_TRUE(renderer_->InspectExposureTransition(debug_handle).has_value());
}

NOLINT_TEST_F(
  ExposureGpuTest, LateCameraCutWaitsForNextCaptureAndRecoveryRemeters)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(8.0F, 4U, 4U), {}, false, 0.0F,
      [&] {
        EXPECT_TRUE(renderer_
            ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
            .has_value());
      }),
    1.0F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  const auto cut
    = renderer_->InspectExposureTransition(handle)->request.generation;
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_GT(
    renderer_->InspectExposureTransition(handle)->request.generation, cut);
}

NOLINT_TEST_F(
  ExposureGpuTest, PublishingADifferentCameraTriggersTheDefaultCutPolicy)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto scene = std::make_shared<scene::Scene>("CameraSelection", 8U);
  auto first = scene->CreateNode("First");
  auto second = scene->CreateNode("Second");
  ASSERT_TRUE(first.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
  ASSERT_TRUE(
    second.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
  auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(texture));
  auto view = CompositionView {};
  view.id = ViewId { 50U };
  view.view_state_handle = CompositionView::ViewStateHandle { 50U };
  view.view.viewport = { .width = 4.0F, .height = 4.0F };
  view.camera = first;
  const auto publish = [&] {
    return renderer_->PublishRuntimeCompositionView(frame,
      { .composition_view = view,
        .render_target = observer_ptr { target.get() } });
  };
  const auto id = publish();
  ctx_.scene = observer_ptr { scene.get() };
  ctx_.current_view.view_id = id;
  ctx_.current_view.view_state_handle = view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  view.camera = second;
  ASSERT_EQ(publish(), id);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(view.view_state_handle)
              ->request.policy,
    ExposureTransitionPolicy::kRemeter);
}

NOLINT_TEST_F(ExposureGpuTest, ThreeFramesInFlightKeepDistinctExposureRecordsAndUploads)
{
  std::array<postprocess::ExposurePass::Result, 3> frames;
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (unsigned i = 0U; i < frames.size(); ++i) {
    settings.manual_ev = 4.0F + 4.0F * i;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { i };
    frames[i] = RecordShared(Signal {}, SharedConfig(settings));
    ASSERT_TRUE(frames[i].executed);
  }
  // No CPU fence wait occurred between the three submissions.
  for (unsigned i = 0U; i < frames.size(); ++i) {
    const auto state = ReadState(frames[i]);
    EXPECT_EQ(state.displayed_scale, std::exp2(-4.0F - 4.0F * i));
    EXPECT_EQ(state.frame_sequence[0], i + 1U);
    for (unsigned j = i + 1U; j < frames.size(); ++j)
      EXPECT_NE(frames[i].state, frames[j].state);
  }
}

auto ExposureGpuTest::CheckOffscreenSharing(const bool inside_frame) -> void
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto scene = std::make_shared<scene::Scene>("OffscreenExposure", 4U);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  const auto root = PublishExposureOwner(
    frame, ViewId { 800U }, CompositionView::ViewStateHandle { 90U }, settings);
  ASSERT_NE(root, kInvalidViewId);
  ASSERT_NE(root, ViewId { 800U });
  PublishExposureOwner(frame, ViewId { 801U },
    CompositionView::ViewStateHandle { 91U }, settings, ViewId { 800U });
  auto output = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto input = Renderer::OffscreenSceneViewInput::FromCamera(
    "Offscreen", root, view, camera);
  input.SetExposureSourceViewId(ViewId { 801U });
  input.SetViewStateHandle(CompositionView::ViewStateHandle { 92U });
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession({ .frame_slot = frame::Slot { 0U },
    .frame_sequence = frame::SequenceNumber { 1U },
    .delta_time_seconds = 0.0F });
  facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
  facade.SetOutputTarget({ .framebuffer = observer_ptr { framebuffer.get() } });
  facade.SetViewIntent(input);
  for (const auto& issue : facade.Validate().issues)
    ADD_FAILURE() << issue.code << ": " << issue.message;
  auto session = facade.Finalize();
  ASSERT_TRUE(session.has_value());
  if (inside_frame)
    session->ExecuteInsideFrame(frame);
  else
    session->ExecuteNow();
  WaitForQueueIdle();
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  EXPECT_TRUE(service->GetLastExecutionState().tonemap_executed);
  EXPECT_FALSE(service->GetLastExecutionState().auto_exposure_requested);
  const auto states
    = vortex::testing::RendererPublicationProbe::FrameExposureStates(
      *service, frame::Slot { 0U });
  ASSERT_GE(states.size(), 2U);
  const auto state = Read<ExposureStateData>(
    *states.back()->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(state.displayed_scale, 0x1p-4F);
  EXPECT_NE(state.flags & 128U, 0U);
  EXPECT_EQ(states.back()->histogram_buffer, nullptr);
  EXPECT_FALSE(vortex::testing::RendererPublicationProbe::HasExposureViewState(
    *service, CompositionView::kInvalidViewStateHandle));
  // Rejected borrowers must neither replace the source image history nor
  // consume its queued request, including ownership changes after Finalize.
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 90U };
  EXPECT_NEAR(
    ServicePixel(*service, Uniform(.25F, 4U, 4U), settings), .25F / 16.0F, 2e-5F);
  const auto source_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, CompositionView::ViewStateHandle { 90U });
  ASSERT_NE(source_state, nullptr);
  const auto request = renderer_->QueueExposureTransition(
    CompositionView::ViewStateHandle { 90U },
    ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(request.has_value());
  for (const auto handle : { CompositionView::kInvalidViewStateHandle,
         CompositionView::ViewStateHandle { 90U },
         CompositionView::ViewStateHandle { 91U } }) {
    input.SetViewStateHandle(handle);
    facade.SetViewIntent(input);
    EXPECT_FALSE(facade.Finalize().has_value());
  }
  ASSERT_NE(PublishExposureOwner(frame, ViewId { 802U },
              CompositionView::ViewStateHandle { 92U }, settings),
    kInvalidViewId);
  session->ExecuteNow();
  session->ExecuteInsideFrame(frame);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              *service, CompositionView::ViewStateHandle { 90U }),
    source_state);
  EXPECT_EQ(Read<ExposureStateData>(
              *source_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-4F);
  const auto status = renderer_->InspectExposureTransition(
    CompositionView::ViewStateHandle { 90U });
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->request, *request);
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  FlushBackend();
}

NOLINT_TEST_F(
  ExposureGpuTest, OffscreenFacadeResolvesSharedRootDespitePublishedIdCollision)
{
  CheckOffscreenSharing(false);
}

NOLINT_TEST_F(ExposureGpuTest, OffscreenFacadeSharesRootInsideFrame)
{
  CheckOffscreenSharing(true);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameResolvePinsManualGainAndDistinctInFlightRecords)
{
  const auto capture = BeginOptionalCapture();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto first = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(first, nullptr);
  settings.manual_ev = 4.0F;
  EXPECT_EQ(pass_->ResolveFrame(ctx_, SharedConfig(settings), {}), first);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  ctx_.frame_slot = frame::Slot { 1U };
  const auto second = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first->buffer, second->buffer);
  const auto a
    = Read<FrameExposureData>(*first->buffer, ResourceStates::kShaderResource);
  const auto b
    = Read<FrameExposureData>(*second->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(a.pre_exposure, 0x1p-14F);
  EXPECT_EQ(a.one_over_pre_exposure, 0x1p14F);
  EXPECT_EQ(a.global_exposure_state_slot, first->current_state->srv_index);
  EXPECT_EQ(b.pre_exposure, 0x1p-4F);
  EXPECT_EQ(b.flags, 0U);
  EXPECT_EQ(Read<ExposureStateData>(
              *first->current_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-14F);
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameResolveUsesGpuPriorGainAndPositiveLatentAfterZero)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.target_luminance = 0.0F;
  Run(signal, settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(resolved, nullptr);
  const auto frame = Read<FrameExposureData>(
    *resolved->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, initial.state.latent_scale);
  EXPECT_NEAR(frame.pre_exposure * frame.one_over_pre_exposure, 1.0F, 2e-6F);
  EXPECT_EQ(frame.flags, 0U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  const auto fresh = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(fresh, nullptr);
  const auto bootstrap
    = Read<FrameExposureData>(*fresh->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(bootstrap.pre_exposure, 1.0F);
  EXPECT_EQ(bootstrap.flags, 1U);
  const auto zero = Read<ExposureStateData>(
    *fresh->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(zero.displayed_scale, 0.0F);
  EXPECT_GT(zero.latent_scale, 0.0F);
}

NOLINT_TEST_F(ExposureGpuTest, FrameResolveBorrowsPriorRootAndTagsRootFallback)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto signal = Uniform(.25F);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto prior = RecordShared(signal, SharedConfig(settings));
  ASSERT_TRUE(prior.executed);
  auto source = postprocess::ExposurePass::Source { .handle
    = ctx_.current_view.view_state_handle,
    .config = SharedConfig(settings) };
  settings.manual_ev = 8.0F;
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  ASSERT_TRUE(RecordShared(signal, SharedConfig(settings)).executed);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  const auto borrowed
    = pass_->ResolveFrame(ctx_, SharedConfig(), { .source = &source });
  ASSERT_NE(borrowed, nullptr);
  const auto frame = Read<FrameExposureData>(
    *borrowed->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, 0x1p-4F);
  EXPECT_EQ(frame.global_exposure_state_slot, prior.state->srv_index);
  EXPECT_EQ(frame.flags, 2U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 21U };
  source.handle = CompositionView::ViewStateHandle { 30U };
  source.config = SharedConfig(settings);
  const auto fallback
    = pass_->ResolveFrame(ctx_, SharedConfig(), { .source = &source });
  ASSERT_NE(fallback, nullptr);
  const auto fallback_frame = Read<FrameExposureData>(
    *fallback->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(fallback_frame.pre_exposure, 1.0F);
  EXPECT_EQ(fallback_frame.flags, 11U);
  const auto fallback_state = Read<ExposureStateData>(
    *fallback->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(fallback_state.displayed_scale, 0x1p-8F);
  EXPECT_EQ(fallback_state.applied_generation[0], 0U);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameResolveSeedsWithoutAcknowledgingAndRetriesRecordingFailure)
{
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto seed
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(seed.has_value());
  static_cast<ExposureFailureGraphics&>(Backend()).fail_next_frame_recorder
    = true;
  EXPECT_EQ(pass_->ResolveFrame(ctx_, SharedConfig(),
              { .transition = *seed, .lifetime = seed->lifetime }),
    nullptr);
  const auto resolved = pass_->ResolveFrame(
    ctx_, SharedConfig(), { .transition = *seed, .lifetime = seed->lifetime });
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*resolved->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-12F);
  EXPECT_EQ(Read<ExposureStateData>(
              *resolved->current_state->buffer, ResourceStates::kShaderResource)
              .applied_generation[0],
    0U);
  EXPECT_EQ(renderer_->InspectExposureTransition(seed->target)->phase,
    ExposureTransitionPhase::kQueued);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto fp32 = pass_->ResolveFrame(ctx_, SharedConfig(),
    { .use_fp32 = true, .transition = *seed, .lifetime = seed->lifetime });
  ASSERT_NE(fp32, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*fp32->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    1.0F);
  ctx_.frame_sequence = frame::SequenceNumber { 3U };
  auto diagnostic = SharedConfig();
  diagnostic.temporary_unit_exposure = true;
  const auto unit = pass_->ResolveFrame(ctx_, diagnostic, {});
  ASSERT_NE(unit, nullptr);
  const auto unit_frame
    = Read<FrameExposureData>(*unit->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(unit_frame.pre_exposure, 1.0F);
  EXPECT_EQ(unit_frame.flags, 4U);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameResolvePinsQualifiedCandidateAndRejectsIneligibleCandidate)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto seed = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(seed, nullptr);
  auto candidate = ExposureStateData {};
  candidate.flags = 1U | 256U;
  candidate.fp16_candidate_pre_exposure = 0.125F;
  candidate.fp16_eligible_streak = 2U;
  auto upload = CreateUploadBuffer(SizeBytes { sizeof(candidate) });
  upload->Update(&candidate, sizeof(candidate), 0U);
  {
    auto recorder = AcquireRecorder("Qualified candidate fixture");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      recorder->AdoptKnownResourceState(*seed->current_state->buffer));
    recorder->RequireResourceState(
      *seed->current_state->buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(
      *seed->current_state->buffer, 0U, *upload, 0U, sizeof(candidate));
    recorder->RequireResourceStateFinal(
      *seed->current_state->buffer, ResourceStates::kShaderResource);
  }
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings),
    { .qualified_candidate = seed->current_state });
  ASSERT_NE(resolved, nullptr);
  const auto frame = Read<FrameExposureData>(
    *resolved->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, 0.125F);
  EXPECT_EQ(frame.one_over_pre_exposure, 8.0F);
  EXPECT_EQ(frame.flags, 0U);
  ctx_.frame_sequence = frame::SequenceNumber { 3U };
  const auto invalid = pass_->ResolveFrame(ctx_, SharedConfig(settings),
    { .qualified_candidate = resolved->current_state });
  ASSERT_NE(invalid, nullptr);
  const auto invalid_frame = Read<FrameExposureData>(
    *invalid->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(invalid_frame.pre_exposure, 1.0F);
  EXPECT_EQ(invalid_frame.flags, 1U);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameResolvePreservesOperationalEndpointsAndCameraGain)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (const float ev : { -32.0F, 32.0F }) {
    settings.manual_ev = ev;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
    ASSERT_NE(resolved, nullptr);
    const auto frame = Read<FrameExposureData>(
      *resolved->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(frame.pre_exposure, std::exp2(-ev));
    EXPECT_EQ(frame.one_over_pre_exposure, std::exp2(ev));
  }
  settings.mode = engine::ExposureMode::kManualCamera;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto camera
    = pass_->ResolveFrame(ctx_, SharedConfig(settings, 16.0F), {});
  ASSERT_NE(camera, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*camera->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-16F);
  settings.enabled = false;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto disabled
    = pass_->ResolveFrame(ctx_, SharedConfig(settings, 16.0F), {});
  ASSERT_NE(disabled, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*disabled->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    1.0F);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainSolvesReservedStateAndAppliesManualRatio)
{
  const auto capture = BeginOptionalCapture();
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  postprocess::ExposurePass::FrameLease frame;
  for (const float ev : { 4.0F, 8.0F }) {
    settings.manual_ev = ev;
    const auto pixel = ServicePixel(service,
      Uniform(.25F * std::exp2(-ev), 4U, 4U), settings, false, 0.0F, [&] {
        frame = service.PrepareFrameExposure(ctx_, false);
        ASSERT_NE(frame, nullptr);
      });
    EXPECT_NEAR(pixel, .25F * std::exp2(-ev), 2e-7F);
    ASSERT_NE(frame, nullptr);
    const auto state
      = vortex::testing::RendererPublicationProbe::ExposureStateForView(
        service, ctx_.current_view.view_state_handle);
    EXPECT_EQ(state, frame->current_state);
    EXPECT_EQ(
      Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource)
        .displayed_scale,
      std::exp2(-ev));
    EXPECT_EQ(
      Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
        .pre_exposure,
      std::exp2(-ev));
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainMetersWithGpuReciprocalAndPreservesZeroAndDisabled)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  postprocess::ExposurePass::FrameLease frame;
  const auto prepare = [&] {
    frame = service.PrepareFrameExposure(ctx_, false);
    ASSERT_NE(frame, nullptr);
  };
  EXPECT_NEAR(ServicePixel(
                service, Uniform(.25F, 4U, 4U), settings, false, 0.0F, prepare),
    .18F, 2e-5F);
  EXPECT_NEAR(ServicePixel(
                service, Uniform(.18F, 4U, 4U), settings, false, 0.0F, prepare),
    .18F, 2e-5F);
  ASSERT_NE(frame, nullptr);
  const auto state = Read<ExposureStateData>(
    *frame->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_NEAR(state.raw_metered_luminance, .25F, 2e-5F);
  const auto numerical
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_NEAR(numerical.pre_exposure, .72F, 2e-5F);
  settings.target_luminance = 0.0F;
  EXPECT_EQ(ServicePixel(
              service, Uniform(.18F, 4U, 4U), settings, false, 0.0F, prepare),
    0.0F);
  settings.enabled = false;
  EXPECT_NEAR(ServicePixel(
                service, Uniform(.25F, 4U, 4U), settings, false, 0.0F, prepare),
    .25F, 2e-6F);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainFailedSeedKeepsPriorDisplayedGainAndPendingRequest)
{
  auto service = PostProcessService(*renderer_);
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  const auto request
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(request.has_value());
  postprocess::ExposurePass::FrameLease frame;
  const auto pixel = ServicePixel(
    service, Uniform(.25F / 4096.0F, 4U, 4U), {}, false, 0.0F, [&] {
      frame = service.PrepareFrameExposure(ctx_, false);
      ASSERT_NE(frame, nullptr);
      static_cast<ExposureFailureGraphics&>(Backend())
        .fail_next_exposure_recorder = true;
    });
  ASSERT_NE(frame, nullptr);
  EXPECT_NEAR(pixel, .18F, 2e-5F);
  EXPECT_EQ(
    Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-12F);
  EXPECT_NEAR(Read<ExposureStateData>(
                *frame->current_state->buffer, ResourceStates::kShaderResource)
                .displayed_scale,
    .72F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(request->target)->phase,
    ExposureTransitionPhase::kQueued);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainSharedConsumerUsesOwnerGainAndItsOwnNumericalDomain)
{
  auto service = PostProcessService(*renderer_);
  auto frame_context = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto root = PublishExposureOwner(frame_context, ViewId { 50U },
    CompositionView::ViewStateHandle { 10U }, settings);
  const auto consumer = PublishExposureOwner(frame_context, ViewId { 60U },
    CompositionView::ViewStateHandle { 20U }, {}, ViewId { 50U });
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 16.0F, 2e-7F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F / 16.0F, 4U, 4U), {}, false, 0.0F,
      [&] {
        frame = service.PrepareFrameExposure(ctx_, false);
        ASSERT_NE(frame, nullptr);
      }),
    .25F / 16.0F, 2e-7F);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->current_state->histogram_buffer, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainToneCurvesRemainFiniteAtMaximumSceneTimesGain)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = -32.0F;
  for (const auto mapper : { engine::ToneMapper::kAcesFitted,
         engine::ToneMapper::kFilmic, engine::ToneMapper::kReinhard }) {
    const auto pixel = ServicePixel(
      service, Uniform(0x1p32F, 4U, 4U), settings, false, 0.0F,
      [&] { ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr); },
      mapper);
    EXPECT_TRUE(std::isfinite(pixel));
    EXPECT_NEAR(pixel, 1.0F, 2e-6F);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainToneCurvesPreserveOrdinaryNeutralResponse)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto filmic = [](const double x) {
    return (x * (.15 * x + .05) + .004) / (x * (.15 * x + .5) + .06) - .02 / .3;
  };
  for (const auto mapper : { engine::ToneMapper::kAcesFitted,
         engine::ToneMapper::kFilmic, engine::ToneMapper::kReinhard }) {
    for (const double x : { .01, .18, 1.0, 16.0 }) {
      const double reference = mapper == engine::ToneMapper::kAcesFitted
        ? (x * (x + .0245786) - .000090537)
          / (x * (.983729 * x + .4329510) + .238081)
        : mapper == engine::ToneMapper::kFilmic ? filmic(2.0 * x) / filmic(11.2)
                                                : x / (x + 1.0);
      const auto pixel = ServicePixel(
        service, Uniform(static_cast<float>(x), 4U, 4U), settings, false, 0.0F,
        [&] { ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr); },
        mapper);
      EXPECT_NEAR(pixel, std::clamp(reference, 0.0, 1.0), 2e-5);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, FrameDomainFailedSolveStillHonorsZeroTarget)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  auto settings = scene::ExposureSettings {};
  settings.target_luminance = 0.0F;
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_EQ(ServicePixel(service, Uniform(.18F, 4U, 4U), settings, false, 0.0F,
              [&] {
                frame = service.PrepareFrameExposure(ctx_, false);
                ASSERT_NE(frame, nullptr);
                static_cast<ExposureFailureGraphics&>(Backend())
                  .fail_next_exposure_recorder = true;
              }),
    0.0F);
  ASSERT_NE(frame, nullptr);
  const auto current = Read<ExposureStateData>(
    *frame->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(current.displayed_scale, 0.0F);
  EXPECT_GT(current.latent_scale, 0.0F);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainFailedSourceLossRetainsLatestBorrowInReservedState)
{
  auto& service = OwnedExposureService();
  auto publication = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, publication);
  const auto consumer_handle = ctx_.current_view.view_state_handle;
  const auto old
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, consumer_handle);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 8.0F;
  const auto root = PublishExposureOwner(publication, ViewId { 50U },
    CompositionView::ViewStateHandle { 50U }, settings);
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-7F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle = consumer_handle;
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  const auto fail_copy = [&] {
    ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr);
    static_cast<ExposureFailureGraphics&>(Backend()).fail_next_exposure_recorder
      = true;
  };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F / 256.0F, 4U, 4U), {}, false,
                0.0F, fail_copy),
    .25F / 256.0F, 2e-7F);
  renderer_->RemovePublishedRuntimeView(publication, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle = consumer_handle;
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), {}, false, 0.0F,
                [&] {
                  frame = service.PrepareFrameExposure(ctx_, true);
                  ASSERT_NE(frame, nullptr);
                  static_cast<ExposureFailureGraphics&>(Backend())
                    .fail_next_exposure_recorder = true;
                }),
    .25F / 256.0F, 2e-7F);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(Read<ExposureStateData>(
              *frame->current_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, consumer_handle),
    old);
  EXPECT_EQ(
    Read<ExposureStateData>(*old->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
}

NOLINT_TEST_F(ExposureGpuTest, FrameDomainSkipsTonemapWhenFallbackCannotSubmit)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto previous
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, ctx_.current_view.view_state_handle);
  static_cast<void>(
    ServicePixel(service, Uniform(.18F, 4U, 4U), {}, false, 0.0F, [&] {
      ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr);
      auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
      backend.fail_next_exposure_recorder = true;
      backend.fail_next_fallback_recorder = true;
    }));
  EXPECT_TRUE(service.GetLastExecutionState().tonemap_requested);
  EXPECT_FALSE(service.GetLastExecutionState().tonemap_executed);
  EXPECT_FALSE(service.GetLastExecutionState().wrote_visible_output);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, ctx_.current_view.view_state_handle),
    previous);
}

NOLINT_TEST_F(
  ExposureGpuTest, Fp32SceneColorPreservesWideRangeRadianceAndCoverage)
{
  auto textures = SceneTextures(Backend(),
    { .extent = { 1U, 1U }, .scene_color_format = Format::kRGBA32Float });
  auto color = textures.GetSceneColorResource();
  ASSERT_EQ(color->GetDescriptor().format, Format::kRGBA32Float);
  const Pixel expected { 0x1p30F, 0x1p-24F, 1.0F, .25F };
  auto upload = CreateUploadBuffer(SizeBytes { 256U });
  upload->Update(expected.data(), sizeof(expected), 0U);
  {
    auto recorder = AcquireRecorder("FP32 SceneColor fixture upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    if (!recorder->AdoptKnownResourceState(*color))
      recorder->BeginTrackingResourceState(
        *color, color->GetDescriptor().initial_state);
    recorder->RequireResourceState(*color, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 1U, .height = 1U, .depth = 1U } },
      *color);
    recorder->RequireResourceStateFinal(
      *color, ResourceStates::kShaderResource);
  }
  auto readback
    = GetReadbackManager()->CreateTextureReadback("FP32 SceneColor readback");
  {
    auto recorder = AcquireRecorder("FP32 SceneColor fixture readback");
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*color));
    ASSERT_TRUE(readback->EnqueueCopy(*recorder, *color, {}).has_value());
  }
  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());
  Pixel actual {};
  std::memcpy(actual.data(), mapped->Data(), sizeof(actual));
  EXPECT_EQ(actual, expected);
  FlushBackend();
}

auto ExposureGpuTest::CheckSceneExposureRetry(const bool inside_frame) -> void
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto scene = std::make_shared<scene::Scene>("ExposureRetryScene", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto output = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  const Pixel sentinel { .125F, .25F, .5F, 1.0F };
  std::array<std::byte, 1024U> bytes {};
  for (unsigned y = 0U; y < 4U; ++y)
    for (unsigned x = 0U; x < 4U; ++x)
      std::memcpy(bytes.data() + y * 256U + x * sizeof(Pixel), sentinel.data(),
        sizeof(Pixel));
  auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Prior offscreen output");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, output, ResourceStates::kCommon);
    recorder->RequireResourceState(*output, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 1024U,
        .dst_slice = { .width = 4U, .height = 4U, .depth = 1U } },
      *output);
    recorder->RequireResourceStateFinal(
      *output, ResourceStates::kShaderResource);
  }
  const auto read_pixel = [&]() {
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Offscreen retry pixel");
    {
      auto recorder = AcquireRecorder("Offscreen retry readback");
      CHECK_F(recorder->AdoptKnownResourceState(*output));
      CHECK_F(readback
          ->EnqueueCopy(*recorder, *output,
            { .src_slice
              = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U } })
          .has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Pixel pixel {};
    std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
    return pixel;
  };
  const auto handle = CompositionView::ViewStateHandle { 7000U };
  const auto seed = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  auto input = Renderer::OffscreenSceneViewInput::FromCamera(
    "Retry", ViewId { 7000U }, view, camera);
  input.SetViewStateHandle(handle);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  auto invoke = [&](const unsigned sequence) {
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = frame::Slot { sequence - 1U },
      .frame_sequence = frame::SequenceNumber { sequence },
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(input);
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr { framebuffer.get() } });
    auto session = facade.Finalize();
    CHECK_F(session.has_value());
    if (!inside_frame)
      return session->ExecuteNow();
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(
      frame::Slot { sequence - 1U }, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    const auto result = session->ExecuteInsideFrame(frame);
    renderer_->OnFrameEnd(observer_ptr { &frame });
    return result;
  };
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.recorder_names.clear();
  backend.fail_next_frame_recorder = true;
  EXPECT_FALSE(invoke(1U));
  for (const auto& name : backend.recorder_names) {
    EXPECT_EQ(name.find("BasePass"), std::string::npos);
    EXPECT_EQ(name.find("Tonemap"), std::string::npos);
    EXPECT_EQ(name.find("DeferredLight"), std::string::npos);
  }
  EXPECT_EQ(read_pixel(), sentinel);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_FALSE(
    scene_renderer->GetSceneTextureExtracts().resolved_scene_color.valid);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  EXPECT_FALSE(service->GetLastExecutionState().wrote_visible_output);
  EXPECT_TRUE(invoke(2U));
  EXPECT_EQ(read_pixel()[0], 0.0F);
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, handle);
  ASSERT_NE(state, nullptr);
  const auto solved
    = Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(solved.applied_generation[0], seed->generation);
  EXPECT_EQ(solved.displayed_scale, 0x1p-8F);
  FlushBackend();
}

NOLINT_TEST_F(ExposureGpuTest,
  SceneExposurePreparationFailurePreservesOutputAndRetriesStandalone)
{
  CheckSceneExposureRetry(false);
}

NOLINT_TEST_F(ExposureGpuTest,
  SceneExposurePreparationFailurePreservesOutputAndRetriesInsideFrame)
{
  CheckSceneExposureRetry(true);
}

NOLINT_TEST_F(ExposureGpuTest,
  SceneSkyRadianceIsInvariantToNumericalDomainAndPreservesHighRange)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto scene = std::make_shared<scene::Scene>("SceneDomainFixture", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto output = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  struct ExposureOverride final : IViewExtension {
    std::function<void(RenderContext&)> before;
    auto OnViewSetup(const ViewSetupContext& context) -> void override
    {
      before(context.render_context);
    }
  };
  bool force_nonunit = false;
  auto extension = std::make_shared<ExposureOverride>();
  extension->before = [&](RenderContext& ctx) {
    if (!force_nonunit)
      return;
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    service->SetConfig(SharedConfig(settings));
    ASSERT_NE(service->PrepareFrameExposure(ctx, false), nullptr);
  };
  renderer_->RegisterViewExtension(extension);
  unsigned sequence = 0U;
  for (const bool high_range : { false, true }) {
    settings.manual_ev = high_range ? 30.0F : 4.0F;
    post.SetExposureSettings(settings);
    sky.SetSolidColorRgb({ .25F, .5F, .75F });
    sky.SetIntensity(high_range ? 0x1p30F : 1.0F);
    scene->Update();
    const float expected_scale = high_range ? 1.0F : 1.0F / 16.0F;
    for (const bool nonunit : { false, true }) {
      force_nonunit = nonunit;
      ++sequence;
      auto input = Renderer::OffscreenSceneViewInput::FromCamera(
        "SkyDomain", ViewId { 8100U }, view, camera);
      input.SetViewStateHandle(CompositionView::ViewStateHandle { 8100U });
      input.SetWithAtmosphere(true);
      auto facade = renderer_->ForOffscreenScene();
      facade.SetFrameSession(
        { .frame_slot = frame::Slot { (sequence - 1U) % 3U },
          .frame_sequence = frame::SequenceNumber { sequence },
          .delta_time_seconds = 0.0F });
      facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
      facade.SetOutputTarget(
        { .framebuffer = observer_ptr { framebuffer.get() } });
      facade.SetViewIntent(input);
      auto session = facade.Finalize();
      ASSERT_TRUE(session.has_value());
      ASSERT_TRUE(session->ExecuteNow());
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Scene sky domain pixel");
      {
        auto recorder = AcquireRecorder("Scene sky domain readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*output));
        ASSERT_TRUE(readback
            ->EnqueueCopy(*recorder, *output,
              { .src_slice = { .x = 1U,
                  .y = 0U,
                  .width = 1U,
                  .height = 1U,
                  .depth = 1U } })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      Pixel pixel {};
      std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
      EXPECT_NEAR(pixel[0], .25F * expected_scale, 2e-5F);
      EXPECT_NEAR(pixel[1], .5F * expected_scale, 2e-5F);
      EXPECT_NEAR(pixel[2], .75F * expected_scale, 2e-5F);
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer_);
      const auto& product
        = owner->GetSceneTextureExtracts().resolved_scene_color;
      ASSERT_TRUE(product.valid);
      ASSERT_NE(product.exposure, nullptr);
      EXPECT_EQ(product.texture->GetDescriptor().format, Format::kRGBA32Float);
      const auto domain = Read<FrameExposureData>(
        *product.exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(
        domain.pre_exposure, nonunit ? std::exp2(-settings.manual_ev) : 1.0F);
    }
  }
  extension->before = [](RenderContext&) { };
  FlushBackend();
}

NOLINT_TEST_F(
  ExposureGpuTest, SceneFogHistoryRescalesRgbWithoutScalingTransmittance)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr { &console });
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("FogDomainFixture", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  struct DomainOverride final : IViewExtension {
    Renderer& renderer;
    explicit DomainOverride(Renderer& value)
      : renderer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      auto* service
        = vortex::testing::RendererPublicationProbe::GetPostProcessService(
          *owner);
      auto cfg = PostProcessConfig {};
      cfg.resolved_exposure = *scene::ResolveExposureSettings(
        *hook.render_context.current_view.exposure_override);
      service->SetConfig(cfg);
      ASSERT_NE(
        service->PrepareFrameExposure(hook.render_context, false), nullptr);
    }
  };
  renderer_->RegisterViewExtension(
    std::make_shared<DomainOverride>(*renderer_));
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  std::array<std::shared_ptr<Framebuffer>, 2> outputs;
  for (auto& output : outputs) {
    auto texture = CreateRegisteredTexture({ .width = 4U,
      .height = 4U,
      .format = Format::kRGBA32Float,
      .is_shader_resource = true,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    output = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(texture));
  }
  Pixel initial {};
  for (unsigned sequence = 1U; sequence <= 2U; ++sequence) {
    fog.SetVolumetricFogEmissive(
      sequence == 1U ? Vec3 { .1F, .2F, .3F } : Vec3 { 1.0F, 2.0F, 3.0F });
    scene->Update();
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(
      frame::Slot { sequence - 1U }, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    std::array<ViewId, 2> published;
    for (unsigned index = 0U; index < 2U; ++index) {
      auto input
        = CompositionView::ForScene(ViewId { 9100U + index }, view, camera);
      input.view_state_handle
        = CompositionView::ViewStateHandle { 9100U + index };
      input.with_height_fog = true;
      auto settings = scene::ExposureSettings {};
      settings.key = 12.5F;
      settings.mode = engine::ExposureMode::kManual;
      settings.manual_ev = sequence == 2U && index == 1U ? 8.0F : 4.0F;
      input.render_settings.exposure = settings;
      published[index] = renderer_->PublishRuntimeCompositionView(frame,
        { .composition_view = input,
          .render_target = observer_ptr { outputs[index].get() } });
      ASSERT_NE(published[index], kInvalidViewId);
    }
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr { &frame });
      co_await renderer_->OnRender(observer_ptr { &frame });
    });
    renderer_->OnFrameEnd(observer_ptr { &frame });
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    std::array<Pixel, 2> samples;
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto [texture, exposure]
        = vortex::testing::RendererPublicationProbe::FogHistory(
          *owner, published[index]);
      ASSERT_NE(texture, nullptr);
      ASSERT_NE(exposure, nullptr);
      const auto domain = Read<FrameExposureData>(
        *exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(domain.pre_exposure,
        sequence == 2U && index == 1U ? 1.0F / 256.0F : 1.0F / 16.0F);
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Fog domain voxel");
      {
        auto recorder = AcquireRecorder("Fog domain voxel readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
        ASSERT_TRUE(readback
            ->EnqueueCopy(*recorder, *texture,
              { .src_slice = { .x = 0U,
                  .y = 0U,
                  .z = 16U,
                  .width = 1U,
                  .height = 1U,
                  .depth = 1U } })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      std::memcpy(samples[index].data(), mapped->Data(), sizeof(Pixel));
    }
    ASSERT_GT(samples[0][0], 1e-8F);
    const float ratio = sequence == 2U ? 1.0F / 16.0F : 1.0F;
    for (unsigned channel = 0U; channel < 3U; ++channel)
      EXPECT_NEAR(samples[1][channel], samples[0][channel] * ratio,
        std::max(1e-7F, samples[0][channel] * ratio * 2e-4F));
    EXPECT_NEAR(samples[1][3], samples[0][3], 2e-6F);
    if (sequence == 1U)
      initial = samples[0];
    else {
      EXPECT_GT(samples[0][0], initial[0] * 1.1F);
      EXPECT_LT(samples[0][0], initial[0] * 5.0F);
    }
  }
  renderer_->RegisterConsoleBindings({});
  FlushBackend();
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilitySelectsGpuCandidateWithTwoStopMargin)
{
  const auto capture = BeginOptionalCapture();
  const auto result = Qualify(Uniform(1.0F, 4U, 4U), true);
  EXPECT_EQ(result.candidate_pre_exposure, 8192.0F);
  EXPECT_EQ(result.maximum_scene_rgb, 1.0F);
  EXPECT_EQ(result.failure_flags, 0U);
  EXPECT_EQ(result.checked_samples, 16U);
  EXPECT_EQ(result.checked_products, 1U);
  EXPECT_EQ(result.expected_products, 1U);
  if (capture) EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityRejectsRequiredFortySixStopSignal)
{
  const std::array pixels { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-16F, 0x1p-16F, 0x1p-16F, 1.0F } };
  auto settings = scene::ExposureSettings {};
  settings.min_log_luminance = -24.0F;
  settings.log_luminance_range = 56.0F;
  const auto result = Qualify(MakeSignal(2U, 1U, pixels), true, settings);
  EXPECT_EQ(result.candidate_pre_exposure, 0x1p-17F);
  EXPECT_NE(result.failure_flags & 8U, 0U);
  EXPECT_EQ(result.metering_failures, 1U);
  EXPECT_EQ(result.first_failure_product, 1U);
}

NOLINT_TEST_F(ExposureGpuTest,
  SuitabilityIgnoresBelowBudgetComponentsButRespectsDisplayedGain)
{
  const std::array<Pixel, 1> pixel { Pixel { 1.0F, .5F, 0x1p-40F, 1.0F } };
  EXPECT_EQ(Qualify(MakeSignal(1U, 1U, pixel), true).failure_flags, 0U);
  const std::array wide { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const auto signal = MakeSignal(2U, 1U, wide);
  EXPECT_EQ(Qualify(signal, false).failure_flags, 0U);
  auto bright = scene::ExposureSettings {};
  bright.manual_ev = -32.0F;
  EXPECT_NE(Qualify(signal, false, bright).failure_flags & 4U, 0U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityUsesMeterMaskAndCoverageWeights)
{
  const std::array wide { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const std::array mask_pixels { Pixel { 1.0F, 0, 0, 1 },
    Pixel { 0, 0, 0, 1 } };
  const auto mask = MakeSignal(2U, 1U, mask_pixels);
  const auto signal = MakeSignal(2U, 1U, wide);
  auto required_dark = scene::ExposureSettings {};
  required_dark.black_influence = 1.0F;
  EXPECT_NE(Qualify(signal, true, required_dark).failure_flags & 8U, 0U);
  EXPECT_EQ(Qualify(signal, true, required_dark, &mask).failure_flags, 0U);
  const std::array<Pixel, 1> covered { Pixel { .25F, .25F, .25F, .5F } };
  EXPECT_EQ(
    Qualify(MakeSignal(1U, 1U, covered), true, {}, nullptr, true).failure_flags,
    0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilityRejectsNonfiniteAndMissingRequiredProducts)
{
  const auto nonfinite
    = Qualify(Uniform(std::numeric_limits<float>::infinity()), false);
  EXPECT_NE(nonfinite.failure_flags & 1U, 0U);
  EXPECT_EQ(nonfinite.rejected_samples, 1U);
  const auto signal = Uniform(.25F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto config = SharedConfig();
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(), .srv = signal.srv, .id = 1U },
    postprocess::ExposurePass::HdrProduct { .id = 6U }
  };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  const auto missing = Read<HdrSuitabilityData>(
    *frame->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(missing.failure_flags, 16U);
  EXPECT_EQ(missing.first_failure_product, 6U);
  EXPECT_EQ(missing.expected_products, 33U);
  EXPECT_EQ(missing.checked_products, 1U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityChecksVolumeRgbAndTransmittance)
{
  auto texture = CreateRegisteredTexture({ .width = 2U,
    .height = 1U,
    .depth = 2U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .initial_state = ResourceStates::kCommon });
  std::array<std::byte, 512U> bytes {};
  const Pixel value { .25F, .5F, .75F, .5F };
  for (unsigned z = 0U; z < 2U; ++z)
    for (unsigned x = 0U; x < 2U; ++x)
      std::memcpy(bytes.data() + z * 256U + x * sizeof(Pixel), value.data(),
        sizeof(Pixel));
  auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Suitability volume upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 2U, .height = 1U, .depth = 2U } },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto srv = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*texture, std::move(handle),
    TextureViewDescription {
      .format = Format::kRGBA32Float, .dimension = TextureType::kTexture3D });
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = texture.get(), .srv = srv, .id = 10U, .transmittance = true } };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  const auto result = Read<HdrSuitabilityData>(
    *frame->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(result.maximum_scene_rgb, .75F);
  EXPECT_EQ(result.candidate_pre_exposure, 16384.0F);
  EXPECT_EQ(result.checked_samples, 4U);
  EXPECT_EQ(result.checked_products, 1U << 9U);
  EXPECT_EQ(result.failure_flags, 0U);
  const Pixel tiny_transmittance { 0.0F, 0.0F, 0.0F, 0x1p-25F };
  for (unsigned z = 0U; z < 2U; ++z)
    for (unsigned x = 0U; x < 2U; ++x)
      std::memcpy(bytes.data() + z * 256U + x * sizeof(Pixel),
        tiny_transmittance.data(), sizeof(Pixel));
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Suitability transmittance update");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 2U, .height = 1U, .depth = 2U } },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  const auto bright = Uniform(0x1p30F);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto next = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  const std::array combined {
    postprocess::ExposurePass::HdrProduct {
      .texture = bright.texture.get(), .srv = bright.srv, .id = 1U },
    products[0]
  };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, next, config, combined, {}));
  const auto failed = Read<HdrSuitabilityData>(
    *next->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(failed.failure_flags, 4U);
  EXPECT_EQ(failed.first_failure_product, 10U);
  EXPECT_EQ(failed.image_failures, 4U);
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilitySharesDiscardedDarkAndSyntheticFallbackSemantics)
{
  const std::array original { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const std::array narrowed { original[0], Pixel { 0, 0, 0, 1 } };
  auto settings = scene::ExposureSettings {};
  settings.black_influence = 0.0F;
  const auto signal = MakeSignal(2U, 1U, original);
  EXPECT_EQ(Qualify(signal, true, settings).failure_flags, 0U);
  ResetHistory();
  const auto before = Run(signal, settings);
  ResetHistory();
  const auto after = Run(MakeSignal(2U, 1U, narrowed), settings);
  EXPECT_TRUE(std::equal(before.histogram.begin(),
    before.histogram.begin() + 256, after.histogram.begin()));
  EXPECT_EQ(before.histogram[257], after.histogram[257]);
  EXPECT_EQ(before.histogram[261], after.histogram[261]);
  EXPECT_EQ(before.state.displayed_scale, after.state.displayed_scale);
  EXPECT_EQ(before.state.raw_metered_ev, after.state.raw_metered_ev);
  ResetHistory();
  const auto dark_before = Run(Uniform(0x1p-24F), settings);
  ResetHistory();
  const auto dark_after = Run(Uniform(0.0F), settings);
  EXPECT_EQ(
    dark_before.state.displayed_scale, dark_after.state.displayed_scale);
  EXPECT_NE(dark_before.state.flags & 16U, 0U);
  EXPECT_NE(dark_after.state.flags & 16U, 0U);
  EXPECT_EQ(Qualify(Uniform(0x1p-24F), true, settings).failure_flags, 0U);
  settings.black_influence = 1.0F;
  EXPECT_NE(Qualify(signal, true, settings).failure_flags & 8U, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilityIgnoresCoverageMassChangesAfterDarkDiscard)
{
  auto scene = scene::Scene("DiscardedCoverage", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene.GetEnvironment()
    ->AddSystem<scene::environment::Background>()
    .SetEnabled(true);
  ctx_.scene = observer_ptr { &scene };
  const std::array original { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F * .1F, 0x1p-24F * .1F, 0x1p-24F * .1F, .1F } };
  const std::array narrowed { original[0], Pixel { 0, 0, 0, .0999755859375F } };
  auto settings = scene::ExposureSettings {};
  settings.black_influence = 0.0F;
  const auto signal = MakeSignal(2U, 1U, original);
  EXPECT_EQ(Qualify(signal, true, settings, nullptr, true).failure_flags, 0U);
  ResetHistory();
  const auto before = Run(signal, settings);
  ResetHistory();
  const auto after = Run(MakeSignal(2U, 1U, narrowed), settings);
  EXPECT_TRUE(std::equal(before.histogram.begin(),
    before.histogram.begin() + 256, after.histogram.begin()));
  EXPECT_EQ(before.histogram[257], after.histogram[257]);
  EXPECT_EQ(before.histogram[261], after.histogram[261]);
  EXPECT_EQ(before.state.displayed_scale, after.state.displayed_scale);
  ctx_.scene.reset();
}

NOLINT_TEST_F(ExposureGpuTest,
  StaticSkyUploadKeepsHalfAndFloatStorageCoherentAcrossFacesAndMips)
{
  for (const bool wide : { false, true }) {
    data::pak::core::TextureResourceDesc desc {};
    desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
    desc.width = desc.height = 2U;
    desc.depth = 1U;
    desc.array_layers = 6U;
    desc.mip_levels = 1U;
    desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
    desc.alignment = 256U;
    desc.content_hash = wide ? 2U : 1U;
    std::vector<std::uint8_t> data_region(6U * 4U * sizeof(Pixel));
    std::vector<data::pak::render::SubresourceLayout> layouts;
    std::array<Pixel, 6U> colors;
    for (unsigned face = 0U; face < 6U; ++face) {
      colors[face] = { wide ? 0x1p30F : .5F, wide ? 0x1p-24F : .25F,
        .25F + face * .125F, 1.0F };
      for (unsigned pixel = 0U; pixel < 4U; ++pixel)
        std::memcpy(data_region.data() + (face * 4U + pixel) * sizeof(Pixel),
          colors[face].data(), sizeof(Pixel));
      layouts.push_back({ .offset_bytes = face * 64U,
        .row_pitch_bytes = 32U,
        .size_bytes = 64U });
    }
    auto payload = vortex::testing::detail::BuildV4TexturePayload(
      desc, layouts, data_region);
    desc.size_bytes = static_cast<std::uint32_t>(payload.size());
    auto source = data::TextureResource(desc, std::move(payload));
    auto model = environment::SkyLightEnvironmentModel {};
    model.enabled = true;
    model.source = environment::kSkyLightSourceSpecifiedCubemap;
    model.cubemap_resource = content::ResourceKey { wide ? 502U : 501U };
    model.lower_hemisphere_is_solid_color = false;
    auto processor = environment::internal::IblProcessor(*renderer_);
    const auto first
      = processor.RefreshStaticSkyLightProducts({}, model, &source);
    auto texture
      = static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock();
    ASSERT_NE(texture, nullptr);
    ASSERT_EQ(texture->GetDescriptor().format,
      wide ? Format::kRGBA32Float : Format::kRGBA16Float);
    ASSERT_EQ(texture->GetDescriptor().mip_levels, 2U);
    WaitForQueueIdle();
    renderer_->GetUploadCoordinator().OnFrameStart(
      vortex::internal::RendererTagFactory::Get(),
      frame::Slot { wide ? 1U : 0U });
    const auto ready = processor.RefreshStaticSkyLightProducts(
      first.probe_state, model, &source);
    ASSERT_TRUE(ready.probe_state.valid);
    const auto scale = ready.probe_state.static_sky_light.source_radiance_scale;
    for (unsigned face = 0U; face < 6U; ++face) {
      for (unsigned mip = 0U; mip < 2U; ++mip) {
        auto readback
          = GetReadbackManager()->CreateTextureReadback("Processed sky texel");
        {
          auto recorder = AcquireRecorder("Processed sky readback");
          ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
          ASSERT_TRUE(readback
              ->EnqueueCopy(*recorder, *texture,
                { .src_slice = { .width = 1U,
                    .height = 1U,
                    .depth = 1U,
                    .mip_level = mip,
                    .array_slice = face } })
              .has_value());
        }
        const auto mapped = readback->MapNow();
        ASSERT_TRUE(mapped.has_value());
        Pixel pixel {};
        if (wide)
          std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
        else {
          std::array<std::uint16_t, 4U> packed {};
          std::memcpy(packed.data(), mapped->Data(), sizeof(packed));
          for (unsigned channel = 0U; channel < 4U; ++channel)
            pixel[channel] = data::HalfFloat { packed[channel] }.ToFloat();
        }
        for (unsigned channel = 0U; channel < 3U; ++channel)
          EXPECT_NEAR(static_cast<double>(pixel[channel]) * scale,
            colors[face][channel],
            std::abs(static_cast<double>(colors[face][channel])) * 2e-5
              + 0x1p-120);
        EXPECT_EQ(pixel[3], 1.0F);
      }
    }
    FlushBackend();
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  StaticSkyIntensityEditPromotesBeforePublishingAmplifiedHalfLoss)
{
  data::pak::core::TextureResourceDesc desc {};
  desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
  desc.width = desc.height = desc.depth = 1U;
  desc.array_layers = 6U;
  desc.mip_levels = 1U;
  desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
  desc.alignment = 256U;
  desc.content_hash = 99U;
  std::vector<std::uint8_t> bytes(6U * sizeof(Pixel));
  std::vector<data::pak::render::SubresourceLayout> layouts;
  const Pixel color { 0x1p-50F, 0x1p-50F, 0x1p-50F, 1.0F };
  for (unsigned face = 0U; face < 6U; ++face) {
    std::memcpy(
      bytes.data() + face * sizeof(Pixel), color.data(), sizeof(Pixel));
    layouts.push_back({ .offset_bytes = face * 16U,
      .row_pitch_bytes = 16U,
      .size_bytes = 16U });
  }
  auto payload
    = vortex::testing::detail::BuildV4TexturePayload(desc, layouts, bytes);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());
  auto source = data::TextureResource(desc, std::move(payload));
  auto model = environment::SkyLightEnvironmentModel {};
  model.enabled = true;
  model.source = environment::kSkyLightSourceSpecifiedCubemap;
  model.cubemap_resource = content::ResourceKey { 511U };
  model.lower_hemisphere_is_solid_color = false;
  auto processor = environment::internal::IblProcessor(*renderer_);
  auto state
    = processor.RefreshStaticSkyLightProducts({}, model, &source).probe_state;
  auto half
    = static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock();
  ASSERT_NE(half, nullptr);
  EXPECT_EQ(half->GetDescriptor().format, Format::kRGBA16Float);
  WaitForQueueIdle();
  renderer_->GetUploadCoordinator().OnFrameStart(
    vortex::internal::RendererTagFactory::Get(), frame::Slot { 0U });
  state = processor.RefreshStaticSkyLightProducts(state, model, &source)
            .probe_state;
  ASSERT_TRUE(state.valid);
  const auto original_key = state.static_sky_light.key;
  const auto original_revision = state.static_sky_light.product_revision;
  model.intensity_mul = 2.0F;
  const auto harmless
    = processor.RefreshStaticSkyLightProducts(state, model, &source);
  EXPECT_FALSE(harmless.refreshed);
  EXPECT_EQ(
    harmless.probe_state.static_sky_light.product_revision, original_revision);
  EXPECT_EQ(
    static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock(),
    half);
  model.intensity_mul = 0x1p50F;
  const auto pending = processor.RefreshStaticSkyLightProducts(
    harmless.probe_state, model, &source);
  EXPECT_FALSE(pending.probe_state.valid);
  EXPECT_EQ(pending.probe_state.static_sky_light.processed_cubemap_srv,
    kInvalidShaderVisibleIndex);
  auto full
    = static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock();
  ASSERT_NE(full, nullptr);
  EXPECT_NE(full, half);
  EXPECT_EQ(full->GetDescriptor().format, Format::kRGBA32Float);
  WaitForQueueIdle();
  renderer_->GetUploadCoordinator().OnFrameStart(
    vortex::internal::RendererTagFactory::Get(), frame::Slot { 1U });
  state = processor
            .RefreshStaticSkyLightProducts(pending.probe_state, model, &source)
            .probe_state;
  ASSERT_TRUE(state.valid);
  EXPECT_EQ(state.static_sky_light.key, original_key);
  EXPECT_GT(state.static_sky_light.product_revision, original_revision);
  auto service = EnvironmentLightingService(*renderer_);
  const auto published
    = vortex::testing::RendererPublicationProbe::BuildStaticSkyPublication(
      service, ctx_, state, model);
  EXPECT_EQ(published.sky_light.radiance_scale, 0x1p50F);
  auto readback
    = GetReadbackManager()->CreateTextureReadback("Promoted sky pixel");
  {
    auto recorder = AcquireRecorder("Promoted sky readback");
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*full));
    ASSERT_TRUE(readback
        ->EnqueueCopy(*recorder, *full,
          { .src_slice = { .width = 1U, .height = 1U, .depth = 1U } })
        .has_value());
  }
  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());
  Pixel actual {};
  std::memcpy(actual.data(), mapped->Data(), sizeof(actual));
  for (unsigned channel = 0U; channel < 3U; ++channel)
    EXPECT_EQ(actual[channel] * published.sky_light.radiance_scale, 1.0F);
  model.intensity_mul = 1.0F;
  const auto dimmed
    = processor.RefreshStaticSkyLightProducts(state, model, &source);
  EXPECT_FALSE(dimmed.refreshed);
  EXPECT_EQ(dimmed.probe_state.static_sky_light.product_revision,
    state.static_sky_light.product_revision);
  EXPECT_EQ(
    static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock(),
    full);
  FlushBackend();
}

} // namespace
