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
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
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
  bool track_resources { false };
  mutable std::vector<std::weak_ptr<graphics::Texture>> tracked_textures;
  mutable std::vector<std::weak_ptr<graphics::Buffer>> tracked_buffers;
  auto CreateBuffer(const BufferDesc& desc) const
    -> std::shared_ptr<graphics::Buffer> override
  {
    auto buffer = graphics::d3d12::Graphics::CreateBuffer(desc);
    if (track_resources)
      tracked_buffers.push_back(buffer);
    return buffer;
  }
  auto CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture> override
  {
    auto texture = graphics::d3d12::Graphics::CreateTexture(desc);
    if (desc.debug_name == "Vortex.StaticSkyLight.ProcessedCubemap")
      processed_sky = texture;
    if (track_resources)
      tracked_textures.push_back(texture);
    return texture;
  }
  std::vector<std::string> recorder_names;
  bool fail_next_exposure_recorder { false };
  bool fail_next_frame_recorder { false };
  bool fail_next_fallback_recorder { false };
  bool fail_status_recorder { false };
  std::string fail_recorder_name;
  bool fail_next_suitability_recorder { false };
  auto AcquireCommandRecorder(const graphics::QueueKey& queue,
    std::string_view name, bool immediate = true)
    -> std::unique_ptr<graphics::CommandRecorder,
      std::function<void(graphics::CommandRecorder*)>> override
  {
    recorder_names.emplace_back(name);
    if (!fail_recorder_name.empty() && name == fail_recorder_name)
      return { nullptr, [](graphics::CommandRecorder*) { } };
    if (fail_status_recorder && name == "Exposure status readback")
      return { nullptr, [](graphics::CommandRecorder*) { } };
    if (fail_next_suitability_recorder
      && name == "Vortex Exposure Suitability") {
      fail_next_suitability_recorder = false;
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
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
  auto CheckSceneExposureRetry(bool inside_frame, bool late_failure = false)
    -> void;
  auto CheckFogViewRetirement(bool persistent, bool temporal) -> void;
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
    auto authored = PostProcessConfig { .exposure = settings };
    authored.temporary_unit_exposure = temporary_unit;
    const auto resolved
      = ResolvedPostProcessConfig::Resolve(authored, camera_ev, ++sequence_);
    CHECK_F(resolved.has_value());
    const auto& config = *resolved;
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
    bool start_new_frame = true,
    const PostProcessService::PreparedExposure* prepared = nullptr,
    const Signal* fallback = nullptr,
    postprocess::ExposurePass::FrameLease checked_resolution = {}) -> float
  {
    settings.key = 12.5F;
    if (start_new_frame)
      ++sequence_;
    ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
    ctx_.delta_time = dt;
    ctx_.render_mode = diagnostic ? RenderMode::kWireframe : RenderMode::kSolid;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings,
      {}, diagnostic, ctx_.GetScene());
    auto config = PostProcessConfig {};
    config.enable_bloom = false;
    config.bloom_intensity = 0.0F;
    config.tone_mapper = tone_mapper;
    config.gamma = 1.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
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
        .scene_fallback = fallback ? fallback->texture.get() : nullptr,
        .scene_fallback_srv
        = fallback ? fallback->srv : kInvalidShaderVisibleIndex,
        .checked_resolution = std::move(checked_resolution),
      },
      prepared);
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

  auto EligibilityStep(const Signal& signal,
    const ResolvedPostProcessConfig& config, std::uint64_t sequence,
    std::uint64_t layout = 7U, std::uint32_t expected = 1024U,
    bool invalidate_previous = false,
    const postprocess::ExposurePass::Source* source = nullptr,
    std::optional<ExposureTransitionToken> transition = {},
    bool metering_available = true, bool capture_eligibility = false)
    -> std::pair<ExposureStateData, ExposureCompletedStatus>
  {
    ctx_.frame_sequence = frame::SequenceNumber { sequence };
    const auto lifetime = transition ? transition->lifetime : 0U;
    const auto frame = pass_->ResolveFrame(ctx_, config,
      { .use_fp32 = true,
        .source = source,
        .transition = transition,
        .lifetime = lifetime });
    CHECK_NOTNULL_F(frame.get());
    const auto solved = pass_->Execute(ctx_, config,
      { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .metering_available = metering_available,
        .transition = transition,
        .source = source,
        .lifetime = lifetime });
    CHECK_F(solved.executed);
    const auto before = ReadState(solved);
    EXPECT_EQ(before.flags & 256U, 0U);
    EXPECT_EQ(before.fp16_eligible_streak, 0U);
    EXPECT_FALSE(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = layout, .expected_products = expected }));
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    const auto capture = capture_eligibility
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    CHECK_F(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    CHECK_F(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = layout,
        .expected_products = expected,
        .invalidate_previous = invalidate_previous }));
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    const auto state = ReadState(solved);
    const auto status = Read<ExposureCompletedStatus>(
      *solved.state->status_buffer, ResourceStates::kCopySource);
    EXPECT_EQ(std::memcmp(&before, &state, 24U), 0);
    EXPECT_EQ(before.settings_revision, state.settings_revision);
    EXPECT_EQ(before.fallback_reason, state.fallback_reason);
    EXPECT_EQ(before.requested_generation, state.requested_generation);
    EXPECT_EQ(before.applied_generation, state.applied_generation);
    EXPECT_EQ(before.frame_sequence, state.frame_sequence);
    EXPECT_EQ((before.flags ^ state.flags) & ~(32U | 256U), 0U);
    EXPECT_EQ(status.fp16_eligible_streak, state.fp16_eligible_streak);
    EXPECT_EQ(status.product_layout_revision, state.product_layout_revision);
    EXPECT_EQ(status.candidate_state_generation, state.frame_sequence);
    EXPECT_EQ(status.frame_sequence, state.frame_sequence);
    EXPECT_EQ(status.settings_revision, state.settings_revision);
    EXPECT_EQ(status.requested_generation, state.requested_generation);
    EXPECT_EQ(status.applied_generation, state.applied_generation);
    EXPECT_EQ(
      status.view_state_identity[0], static_cast<std::uint32_t>(lifetime));
    EXPECT_EQ(status.view_state_identity[1],
      static_cast<std::uint32_t>(lifetime >> 32U));
    EXPECT_EQ(status.reserved, 0U);
    EXPECT_EQ(status.flags & 4U, state.fp16_eligible_streak >= 2U ? 4U : 0U);
    CHECK_F(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = layout,
        .expected_products = expected,
        .invalidate_previous = invalidate_previous }));
    const auto duplicate = ReadState(solved);
    EXPECT_EQ(std::memcmp(&state, &duplicate, sizeof(state)), 0);
    return { state, status };
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
    std::optional<float> camera_ev = {}, std::uint64_t revision = 1U)
    -> ResolvedPostProcessConfig
  {
    settings.key = 12.5F;
    const auto config = ResolvedPostProcessConfig::Resolve(
      PostProcessConfig { .exposure = settings }, camera_ev, revision);
    CHECK_F(config.has_value());
    return *config;
  }
  auto RecordShared(const Signal& signal,
    const ResolvedPostProcessConfig& config,
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
  ExposureGpuTest, SetConfigLoadsMasksAndRetainsAcceptedReplacementAtomically)
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
  auto config = PostProcessConfig {};
  config.exposure.key = 12.5F;
  config.exposure.metering_mask
    = loader.PreloadCookedTexture(std::span(payload));
  service.SetConfig(config);
  const auto signal = Uniform(.25F);
  const auto render = [&] {
    ++sequence_;
    ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    renderer_->GetUploadCoordinator().OnFrameStart(
      internal::RendererTagFactory::Get(), ctx_.frame_slot);
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
  };
  auto prepared = render();
  ASSERT_TRUE(prepared.has_value());
  EXPECT_EQ(prepared->config.Exposure().authored.metering_mask.get(), 0U);
  EXPECT_EQ(ReadState(prepared->exposure).flags & 4U, 0U);
  for (unsigned i = 0U; i < 8U
    && prepared->config.Exposure().authored.metering_mask
      != config.exposure.metering_mask;
    ++i) {
    WaitForQueueIdle();
    prepared = render();
    ASSERT_TRUE(prepared.has_value());
  }
  ASSERT_EQ(prepared->config.Exposure().authored, config.exposure);
  const auto histogram = Read<std::array<std::uint32_t, 264>>(
    *prepared->exposure.histogram_buffer, ResourceStates::kCommon);
  EXPECT_EQ(histogram[102], 2056U);
  EXPECT_EQ(histogram[257], 1U);
  const auto accepted = prepared->config;
  const auto accepted_state = ReadState(prepared->exposure);
  EXPECT_NEAR(accepted_state.displayed_scale, .72F, 2e-4F);
  config.exposure.metering_mask = loader.MintSyntheticTextureKey();
  config.exposure.compensation_ev = 1.0F;
  service.SetConfig(config);
  for (unsigned i = 0U; i < 3U; ++i) {
    WaitForQueueIdle();
    prepared = render();
    ASSERT_TRUE(prepared.has_value());
    EXPECT_EQ(
      prepared->config.Exposure().authored, accepted.Exposure().authored);
    EXPECT_EQ(prepared->config.Revision(), accepted.Revision());
    EXPECT_EQ(
      (Read<std::array<std::uint32_t, 264>>(
        *prepared->exposure.histogram_buffer, ResourceStates::kCommon)[102]),
      2056U);
    EXPECT_EQ(ReadState(prepared->exposure).displayed_scale,
      accepted_state.displayed_scale);
  }
  // The scene entry point supplies the same authored request through capture.
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 902U };
  ctx_.current_view.view_id = ViewId { 902U };
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
  static_cast<void>(
    service.CaptureViewExposureSettings(ctx_.current_view.view_id,
      ctx_.current_view.view_state_handle, accepted.Exposure().authored));
  const auto scene_result
    = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
  ASSERT_TRUE(scene_result.has_value());
  EXPECT_EQ(ReadState(scene_result->exposure).displayed_scale,
    accepted_state.displayed_scale);
  EXPECT_EQ(
    (Read<std::array<std::uint32_t, 264>>(
      *scene_result->exposure.histogram_buffer, ResourceStates::kCommon)),
    histogram);
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
      const auto& accepted = service.CaptureViewExposureSettings(
        ctx_.current_view.view_id, handle, requested);
      ASSERT_EQ(accepted.resolved.authored.min_ev, 2.0F);
      auto config = PostProcessConfig {};
      config.enable_bloom = false;
      config.bloom_intensity = 0.0F;
      config.tone_mapper = engine::ToneMapper::kNone;
      config.gamma = 1.0F;
      service.SetResolvedConfig(service.BuildPassConfig(config,
        ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
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

NOLINT_TEST_F(ExposureGpuTest, SceneAndDirectSettingsUseIdenticalGainsAndRates)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.speed_up = .875F;
  settings.speed_down = .625F;
  const auto initial = Uniform(.25F, 4U, 4U);
  const auto bright = Uniform(.5F, 4U, 4U);
  const auto dark = Uniform(.015625F, 4U, 4U);
  const auto compare = [&](const Signal& signal, float luminance, float dt) {
    const auto direct = Run(signal, settings, dt);
    const auto pixel = ServicePixel(service, signal, settings, false, dt);
    EXPECT_NEAR(pixel, luminance * direct.state.displayed_scale, 2e-5F);
  };
  compare(initial, .25F, 0.0F);
  compare(bright, .5F, .25F);
  compare(dark, .015625F, .25F);
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  compare(bright, .5F, 0.0F);
  EXPECT_NEAR(ServicePixel(service, bright, settings), 0x1p-15F, 1e-7F);
}

NOLINT_TEST_F(ExposureGpuTest,
  TwoViewsInitializeIndependentlyWithoutWaitingBetweenSubmissions)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto first = pass_->Execute(ctx_, config, {});
  ASSERT_TRUE(first.executed);
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.current_view.view_id = ViewId { 2U };
  settings.manual_ev = 8.0F;
  config = SharedConfig(settings);
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
    config.exposure = late.resolved.authored;
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
    config.exposure = late.resolved.authored;
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
  diagnostic = diagnostic.WithDiagnosticOverride(true);
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

auto ExposureGpuTest::CheckSceneExposureRetry(
  const bool inside_frame, const bool late_failure) -> void
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
  auto seed = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  auto input = Renderer::OffscreenSceneViewInput::FromCamera(
    "Retry", ViewId { 7000U }, view, camera);
  input.SetViewStateHandle(handle);
  auto successful_input = Renderer::OffscreenSceneViewInput::FromCamera(
    "Successful sibling", ViewId { 6999U }, view, camera);
  successful_input.SetViewStateHandle(
    CompositionView::ViewStateHandle { 6999U });
  auto successful_output = CreateRegisteredTexture(output->GetDescriptor());
  auto successful_target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(successful_output));
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  auto invoke = [&](const unsigned sequence, const bool sibling = false) {
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = frame::Slot { sequence - 1U },
      .frame_sequence = frame::SequenceNumber { sequence },
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(sibling ? successful_input : input);
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr {
          sibling ? successful_target.get() : framebuffer.get() } });
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
  auto prior_output = sentinel;
  if (late_failure) {
    ASSERT_TRUE(invoke(1U));
    prior_output = read_pixel();
    seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_TRUE(invoke(2U, true));
  }
  backend.recorder_names.clear();
  backend.fail_next_frame_recorder = !late_failure;
  backend.fail_next_exposure_recorder = late_failure;
  backend.fail_next_fallback_recorder = late_failure;
  EXPECT_FALSE(invoke(late_failure ? 2U : 1U));
  for (const auto& name : backend.recorder_names) {
    if (!late_failure) {
      EXPECT_EQ(name.find("BasePass"), std::string::npos);
      EXPECT_EQ(name.find("DeferredLight"), std::string::npos);
    }
    EXPECT_EQ(name.find("Tonemap"), std::string::npos);
    EXPECT_EQ(name.find("ResolveSceneColor"), std::string::npos);
  }
  EXPECT_EQ(read_pixel(), prior_output);
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
  EXPECT_TRUE(invoke(late_failure ? 3U : 2U));
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
  LateExposureFailureAfterSuccessfulSiblingPreservesStandaloneOutput)
{
  CheckSceneExposureRetry(false, true);
}

NOLINT_TEST_F(ExposureGpuTest,
  LateExposureFailureAfterSuccessfulSiblingPreservesInsideFrameOutput)
{
  CheckSceneExposureRetry(true, true);
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
    service->SetResolvedConfig(SharedConfig(settings));
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

auto ExposureGpuTest::CheckFogViewRetirement(
  const bool persistent, const bool temporal) -> void
{
  auto& tracked = static_cast<ExposureFailureGraphics&>(Backend());
  tracked.track_resources = true;
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
  ASSERT_EQ(
    console
      .Execute(temporal ? "vtx.volumetric_fog.temporal_reprojection true"
                        : "vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("Fog retirement", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  fog.SetVolumetricFogEmissive({ .1F, .2F, .3F });
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto color = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target
    = Backend().CreateFramebuffer(FramebufferDesc {}.AddColorAttachment(color));
  struct Capture final : IViewExtension {
    Renderer& renderer;
    std::vector<std::shared_ptr<Texture>> textures;
    explicit Capture(Renderer& value)
      : renderer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      textures = vortex::testing::RendererPublicationProbe::EnvironmentTextures(
        *owner, hook.render_context.current_view.view_id);
    }
  };
  auto capture = std::make_shared<Capture>(*renderer_);
  renderer_->RegisterViewExtension(capture);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  std::optional<std::size_t> baseline_resources;
  std::uint64_t sequence = 0U;
  auto& registry = Backend().GetResourceRegistry();
  auto& reclaimer = Backend().GetDeferredReclaimer();
  for (unsigned iteration = 0U; iteration < 8U; ++iteration) {
    SCOPED_TRACE(iteration);
    const auto slot = frame::Slot { 0U };
    reclaimer.OnBeginFrame(slot);
    frame.SetFrameSequenceNumber(frame::SequenceNumber { ++sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    const auto intent = ViewId { 12000U + iteration };
    ViewId published = intent;
    if (persistent) {
      auto input = CompositionView::ForScene(intent, view, camera);
      input.view_state_handle
        = CompositionView::ViewStateHandle { intent.get() };
      input.with_height_fog = true;
      published = renderer_->PublishRuntimeCompositionView(frame,
        { .composition_view = input,
          .render_target = observer_ptr { target.get() } });
      ASSERT_NE(published, kInvalidViewId);
      auto loop = co::testing::TestEventLoop {};
      co::Run(loop, [&]() -> co::Co<void> {
        co_await renderer_->OnPreRender(observer_ptr { &frame });
        co_await renderer_->OnRender(observer_ptr { &frame });
      });
    } else {
      auto input = Renderer::OffscreenSceneViewInput::FromCamera(
        "Stateless fog", intent, view, camera);
      auto facade = renderer_->ForOffscreenScene();
      facade.SetFrameSession({ .frame_slot = slot,
        .frame_sequence = frame::SequenceNumber { sequence },
        .delta_time_seconds = 0.0F });
      facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
      facade.SetViewIntent(input);
      facade.SetOutputTarget({ .framebuffer = observer_ptr { target.get() } });
      auto session = facade.Finalize();
      ASSERT_TRUE(session.has_value());
      ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    }
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(
      vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner),
      persistent && temporal ? 1U : 0U);
    ASSERT_EQ(capture->textures.size(), 1U);
    auto texture = capture->textures.front();
    ASSERT_TRUE(registry.Contains(*texture));
    if (persistent) {
      owner->OnFrameStart(frame);
      EXPECT_EQ(
        vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner),
        temporal ? 1U : 0U);
      renderer_->RemovePublishedRuntimeView(frame, intent);
      EXPECT_EQ(
        vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner), 0U);
    }
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Retiring fog voxel");
    {
      auto recorder = AcquireRecorder("Retiring fog output readback");
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
      ASSERT_TRUE(readback
          ->EnqueueCopy(*recorder, *texture,
            { .src_slice
              = { .z = 16U, .width = 1U, .height = 1U, .depth = 1U } })
          .has_value());
    }
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    Pixel voxel {};
    std::memcpy(voxel.data(), mapped->Data(), sizeof(voxel));
    EXPECT_GT(voxel[0], 0.0F);
    EXPECT_TRUE(std::isfinite(voxel[0]));
    renderer_->OnFrameEnd(observer_ptr { &frame });
    capture->textures.clear();
    WaitForQueueIdle();
    for (unsigned retire = 0U; retire < frame::kFramesInFlight.get();
      ++retire) {
      const auto retired_slot = frame::Slot { retire };
      owner->OnStandaloneFrameStart(
        frame::SequenceNumber { ++sequence }, retired_slot, std::nullopt);
      reclaimer.OnBeginFrame(retired_slot);
    }
    EXPECT_FALSE(registry.Contains(*texture));
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    EXPECT_EQ(
      vortex::testing::RendererPublicationProbe::RetainedExposureFrameCount(
        *service),
      0U);
    const auto resources = registry.GetRegisteredResourceCount();
    if (iteration == 2U)
      baseline_resources = resources;
    if (baseline_resources)
      EXPECT_EQ(resources, *baseline_resources);
    if (baseline_resources && resources != *baseline_resources
      && iteration == 3U) {
      std::map<std::string, unsigned> names;
      for (auto weak : tracked.tracked_buffers)
        if (auto resource = weak.lock();
          resource && registry.Contains(*resource))
          ++names[std::string(resource->GetName())];
      for (auto weak : tracked.tracked_textures)
        if (auto resource = weak.lock();
          resource && registry.Contains(*resource))
          ++names[std::string(resource->GetName())];
      for (const auto& [name, count] : names)
        LOG_F(ERROR, "retained {} {}", count, name);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, QueuedHzbBuildsKeepTheirOwnDepthPyramids)
{
  auto module = ScreenHzbModule(*renderer_, SceneTexturesConfig {});
  const std::array extents { glm::uvec2 { 8U, 8U }, glm::uvec2 { 16U, 8U },
    glm::uvec2 { 8U, 16U } };
  std::vector<std::unique_ptr<SceneTextures>> textures;
  std::vector<std::vector<float>> sources;
  std::vector<ScreenHzbModule::Output> outputs;
  for (unsigned view = 0U; view < extents.size(); ++view) {
    const auto extent = extents[view];
    textures.push_back(std::make_unique<SceneTextures>(
      Backend(), SceneTexturesConfig { .extent = extent }));
    auto depth = textures.back()->GetSceneDepthResource();
    sources.emplace_back(extent.x * extent.y);
    auto& registry = Backend().GetResourceRegistry();
    if (!registry.Contains(*depth))
      registry.Register(depth);
    const auto dsv_desc
      = TextureViewDescription { .view_type = ResourceViewType::kTexture_DSV,
          .visibility = DescriptorVisibility::kCpuOnly,
          .format = depth->GetDescriptor().format,
          .dimension = TextureType::kTexture2D };
    auto dsv = registry.Find(*depth, dsv_desc);
    if (!dsv->IsValid()) {
      auto allocation
        = renderer_->GetGraphics()->GetDescriptorAllocator().AllocateRaw(
          ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
      dsv = registry.RegisterView(*depth, std::move(allocation), dsv_desc);
    }
    CHECK_F(dsv->IsValid());
    auto recorder = AcquireRecorder("HZB depth fixture pattern");
    EnsureTracked(*recorder, depth, depth->GetDescriptor().initial_state);
    recorder->RequireResourceState(*depth, ResourceStates::kDepthWrite);
    recorder->FlushBarriers();
    for (unsigned y = 0U; y < extent.y; ++y)
      for (unsigned x = 0U; x < extent.x; ++x) {
        const float value
          = static_cast<float>((x * 3U + y * 5U + view * 17U) % 63U + 1U)
          / 64.0F;
        sources.back()[y * extent.x + x] = value;
        const std::array rects { Scissors { .left = static_cast<int>(x),
          .top = static_cast<int>(y),
          .right = static_cast<int>(x + 1U),
          .bottom = static_cast<int>(y + 1U) } };
        recorder->ClearDepthStencilView(
          *depth, dsv, ClearFlags::kDepth, value, 0U, rects);
      }
    recorder->RequireResourceStateFinal(
      *depth, ResourceStates::kShaderResource);
  }
  WaitForQueueIdle();
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  ctx_.frame_slot = frame::Slot { 0U };
  ctx_.current_view.screen_hzb_request
    = { .current_furthest = true, .current_closest = true };
  const auto capture = BeginOptionalCapture();
  for (unsigned view = 0U; view < extents.size(); ++view) {
    module.OnFrameStart();
    ctx_.current_view.view_id = ViewId { 14000U + view };
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 14000U + view };
    module.Execute(ctx_, *textures[view]);
    outputs.push_back(module.GetCurrentOutput());
    ASSERT_TRUE(outputs.back().available);
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  for (unsigned view = 0U; view < extents.size(); ++view) {
    for (const bool closest : { true, false }) {
      SCOPED_TRACE(view);
      SCOPED_TRACE(closest);
      auto reference = sources[view];
      auto extent = extents[view];
      const auto& texture = closest ? outputs[view].closest_texture
                                    : outputs[view].furthest_texture;
      ASSERT_NE(texture, nullptr);
      for (unsigned mip = 0U; mip < texture->GetDescriptor().mip_levels;
        ++mip) {
        const glm::uvec2 reduced_extent { std::max(1U, extent.x / 2U),
          std::max(1U, extent.y / 2U) };
        std::vector<float> reduced(reduced_extent.x * reduced_extent.y);
        for (unsigned y = 0U; y < reduced_extent.y; ++y)
          for (unsigned x = 0U; x < reduced_extent.x; ++x) {
            float value = closest ? 0.0F : 1.0F;
            for (unsigned dy = 0U; dy < 2U; ++dy)
              for (unsigned dx = 0U; dx < 2U; ++dx) {
                const float sample
                  = reference[std::min(y * 2U + dy, extent.y - 1U) * extent.x
                    + std::min(x * 2U + dx, extent.x - 1U)];
                value
                  = closest ? std::max(value, sample) : std::min(value, sample);
              }
            reduced[y * reduced_extent.x + x] = value;
          }
        auto readback = GetReadbackManager()->CreateTextureReadback(
          "HZB independent oracle");
        {
          auto recorder = AcquireRecorder("HZB pyramid readback");
          ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
          ASSERT_TRUE(readback
              ->EnqueueCopy(*recorder, *texture,
                { .src_slice = { .width = reduced_extent.x,
                    .height = reduced_extent.y,
                    .depth = 1U,
                    .mip_level = mip } })
              .has_value());
        }
        const auto mapped = readback->MapNow();
        ASSERT_TRUE(mapped.has_value());
        for (unsigned y = 0U; y < reduced_extent.y; ++y)
          for (unsigned x = 0U; x < reduced_extent.x; ++x) {
            float actual;
            std::memcpy(&actual,
              mapped->Data() + y * mapped->Layout().row_pitch.get()
                + x * sizeof(float),
              sizeof(float));
            EXPECT_EQ(actual, reduced[y * reduced_extent.x + x]);
          }
        reference = std::move(reduced);
        extent = reduced_extent;
      }
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, RemovedViewsRetireFogHistoryAndExposureLeases)
{
  CheckFogViewRetirement(true, true);
}

NOLINT_TEST_F(ExposureGpuTest, StatelessFogViewsRetainNoPersistentHistory)
{
  CheckFogViewRetirement(false, true);
}

NOLINT_TEST_F(
  ExposureGpuTest, NonTemporalFogOutputsRetireWithoutPersistentHistory)
{
  CheckFogViewRetirement(true, false);
}

NOLINT_TEST_F(
  ExposureGpuTest, SameFrameOffscreenEnvironmentDescriptorsSurviveQueuedViews)
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
  auto scene = std::make_shared<scene::Scene>("OffscreenRetirement", 8U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene->GetEnvironment()
    ->AddSystem<scene::environment::SkyAtmosphere>()
    .SetEnabled(true);
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  fog.SetVolumetricFogEmissive({ .125F, .25F, .5F });
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto sun = scene->CreateNode("Sun");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetEnvironmentContribution(true);
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  light->SetIntensityLux(1000.0F);
  ASSERT_TRUE(sun.AttachLight(std::move(light)));
  auto view = View {};
  view.viewport = { .width = 16.0F, .height = 16.0F };
  std::array<scene::SceneNode, 2> cameras;
  std::array<std::shared_ptr<Texture>, 2> colors;
  std::array<std::shared_ptr<Framebuffer>, 2> targets;
  for (unsigned i = 0U; i < 2U; ++i) {
    cameras[i] = scene->CreateNode(i ? "High camera" : "Low camera");
    auto lens = std::make_unique<scene::PerspectiveCamera>();
    lens->SetViewport(view.viewport);
    ASSERT_TRUE(cameras[i].AttachCamera(std::move(lens)));
    cameras[i].GetTransform().SetLocalPosition({ 0, -10, i ? 2000.0F : 2.0F });
    colors[i] = CreateRegisteredTexture({ .width = 16U,
      .height = 16U,
      .format = Format::kRGBA32Float,
      .is_shader_resource = true,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    targets[i] = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(colors[i]));
  }
  scene->Update();
  struct Capture final : IViewExtension {
    Renderer& renderer;
    std::unordered_map<ViewId, std::vector<std::shared_ptr<Texture>>> textures;
    std::unordered_map<ViewId, std::vector<ShaderVisibleIndex>> slots;
    std::unordered_map<ViewId, postprocess::ExposurePass::FrameLease> exposure;
    explicit Capture(Renderer& value)
      : renderer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      // The public extension supplies the fog participation flag absent from
      // the offscreen builder's current authoring surface.
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      const auto id = hook.render_context.current_view.view_id;
      exposure[id] = hook.render_context.current_view.frame_exposure;
      textures[id]
        = vortex::testing::RendererPublicationProbe::EnvironmentTextures(
          *owner, id);
      slots[id].clear();
      for (const auto& texture : textures[id]) {
        const auto slot
          = renderer.GetGraphics()
              ->GetResourceRegistry()
              .FindShaderVisibleIndex(*texture,
                TextureViewDescription {
                  .format = texture->GetDescriptor().format,
                  .dimension = texture->GetDescriptor().texture_type });
        CHECK_F(slot.has_value());
        slots[id].push_back(*slot);
      }
    }
  };
  auto capture = std::make_shared<Capture>(*renderer_);
  renderer_->RegisterViewExtension(capture);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  const auto begin = [&](std::uint64_t sequence, frame::Slot slot) {
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
  };
  const auto render = [&](unsigned index) {
    auto input = Renderer::OffscreenSceneViewInput::FromCamera(
      "Environment retirement", ViewId { 111U + index }, view, cameras[index]);
    input.SetWithAtmosphere(true);
    input.SetViewStateHandle(CompositionView::ViewStateHandle { 111U + index });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = frame.GetFrameSlot(),
      .frame_sequence = frame.GetFrameSequenceNumber(),
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(input);
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr { targets[index].get() } });
    auto session = facade.Finalize();
    CHECK_F(session.has_value());
    return session->ExecuteInsideFrame(frame);
  };
  const auto read = [&] {
    auto readback = GetReadbackManager()->CreateTextureReadback(
      "Offscreen environment image");
    {
      auto recorder = AcquireRecorder("Offscreen environment readback");
      CHECK_F(recorder->AdoptKnownResourceState(*colors[0]));
      CHECK_F(readback->EnqueueCopy(*recorder, *colors[0], {}).has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    std::array<Pixel, 256U> pixels;
    for (unsigned y = 0; y < 16U; ++y)
      std::memcpy(pixels.data() + y * 16U,
        mapped->Data() + y * mapped->Layout().row_pitch.get(),
        16U * sizeof(Pixel));
    return pixels;
  };
  auto& reclaimer = Backend().GetDeferredReclaimer();
  reclaimer.OnBeginFrame(frame::Slot { 0U });
  begin(1U, frame::Slot { 0U });
  ASSERT_TRUE(render(0U));
  const auto reference = read();
  renderer_->OnFrameEnd(observer_ptr { &frame });
  reclaimer.OnBeginFrame(frame::Slot { 1U });
  begin(2U, frame::Slot { 1U });
  const auto gpu_capture = BeginOptionalCapture();
  ASSERT_TRUE(render(0U));
  const auto retained = capture->textures.at(ViewId { 111U });
  const auto retained_slots = capture->slots.at(ViewId { 111U });
  ASSERT_GE(
    retained.size(), 4U); // Sky/AP plus replaced and current fog history.
  auto& registry = Backend().GetResourceRegistry();
  for (const auto& texture : retained) {
    ASSERT_NE(texture, nullptr);
    ASSERT_TRUE(registry.Contains(*texture)) << texture->GetName();
  }
  // No queue-idle wait or readback map occurs between these offscreen views.
  ASSERT_TRUE(render(1U));
  if (gpu_capture)
    EXPECT_TRUE(gpu_capture->EndCapture());
  for (std::size_t i = 0U; i < retained.size(); ++i) {
    const auto& texture = retained[i];
    EXPECT_TRUE(registry.Contains(*texture)) << texture->GetName();
    EXPECT_EQ(
      registry.FindShaderVisibleIndex(*texture,
        TextureViewDescription { .format = texture->GetDescriptor().format,
          .dimension = texture->GetDescriptor().texture_type }),
      retained_slots[i]);
  }
  constexpr std::uint32_t required
    = (1U << 4U) | (1U << 5U) | (1U << 9U) | (1U << 10U);
  for (const auto id : { ViewId { 111U }, ViewId { 112U } }) {
    const auto& exposure = capture->exposure.at(id);
    ASSERT_NE(exposure, nullptr);
    const auto report = Read<HdrSuitabilityData>(
      *exposure->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.expected_products, required);
    EXPECT_EQ(report.checked_products, required);
    EXPECT_GT(report.checked_samples, 256U);
    const auto state = Read<ExposureStateData>(
      *exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(state.displayed_scale, .0625F);
    EXPECT_NE(state.product_layout_revision[0], 0U);
  }
  const auto actual = read();
  unsigned nontrivial = 0U;
  for (unsigned i = 0; i < actual.size(); ++i)
    for (unsigned c = 0; c < 3U; ++c) {
      EXPECT_TRUE(std::isfinite(actual[i][c]));
      EXPECT_NEAR(actual[i][c], reference[i][c],
        2e-5F + .005F * std::abs(reference[i][c]));
      nontrivial += reference[i][c] > .001F && reference[i][c] < .99F ? 1U : 0U;
    }
  EXPECT_GT(nontrivial, 0U);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
  reclaimer.OnBeginFrame(frame::Slot { 2U });
  EXPECT_TRUE(registry.Contains(*retained.front()));
  reclaimer.OnBeginFrame(frame::Slot { 1U });
  EXPECT_FALSE(registry.Contains(*retained.front()));
  begin(3U, frame::Slot { 2U });
  static_cast<ExposureFailureGraphics&>(Backend()).fail_recorder_name
    = "EnvironmentLightingService AtmosphereSkyViewLut";
  ASSERT_TRUE(render(0U));
  static_cast<ExposureFailureGraphics&>(Backend()).fail_recorder_name.clear();
  const auto& missing = capture->exposure.at(ViewId { 111U });
  const auto report = Read<HdrSuitabilityData>(
    *missing->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(report.expected_products, required);
  EXPECT_EQ(report.checked_products, required & ~(1U << 4U));
  const auto status = Read<ExposureCompletedStatus>(
    *missing->current_state->status_buffer, ResourceStates::kCopySource);
  EXPECT_NE(status.first_failure_kind & 16U, 0U);
  EXPECT_EQ(status.fp16_eligible_streak, 0U);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
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
      cfg.exposure = *hook.render_context.current_view.exposure_override;
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
      const auto certificate = Read<ExposureStatusStorage>(
        *exposure->current_state->status_buffer, ResourceStates::kCopySource);
      const auto& fog_error = certificate.producer_errors[2];
      EXPECT_EQ(fog_error.rgb_relative, 0.0F);
      EXPECT_EQ(fog_error.rgb_absolute, 0.0F);
      EXPECT_EQ(fog_error.transmittance_relative, 0.0F);
      EXPECT_EQ(fog_error.transmittance_absolute, 0.0F);
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
  CheckedSceneColorConversionUsesCurrentScaleAndRejectsTheWholeImage)
{
  constexpr std::uint32_t width = 9U;
  constexpr std::uint32_t height = 3U;
  using PackedPixel = std::array<std::uint16_t, 4U>;
  constexpr PackedPixel sentinel { 0x3400U, 0x3800U, 0x3a00U, 0x3c00U };
  constexpr PackedPixel expected { 0x3000U, 0x3555U, 0x3800U, 0x3800U };
  const Pixel ordinary { .125F, 1.0F / 3.0F, .5F, .5F };
  const float sensitive = 256.4375F * 0x1p-24F;
  struct Case {
    const char* name;
    Pixel pixel;
    float ev;
    bool fp32;
    bool background;
    std::uint32_t failure;
    PackedPixel last_pixel;
    bool automatic { false };
  };
  const std::array cases {
    Case { "ordinary", ordinary, 0, true, false, 0U, expected },
    Case { "overflow", { 0x1p20F, 0x1p20F, 0x1p20F, 1 }, 0, true, false, 2U,
      sentinel },
    Case { "nonfinite", { std::numeric_limits<float>::quiet_NaN(), 0, 0, 1 }, 0,
      true, false, 1U, sentinel },
    Case { "displayed dark loss", { 0x1p-30F, 0x1p-30F, 0x1p-30F, 1 }, -30,
      true, false, 4U, sentinel },
    Case { "nonunit P", ordinary, -2, false, false, 0U, expected },
    Case { "opaque zero alpha", { sensitive, sensitive, sensitive, 0 }, 0, true,
      false, 8U, sentinel, true },
    Case { "background zero alpha", { sensitive, sensitive, sensitive, 0 }, 0,
      true, true, 0U, { 0x0100U, 0x0100U, 0x0100U, 0U }, true },
    Case { "opaque partial alpha", { sensitive, sensitive, sensitive, .5F }, 0,
      true, false, 8U, sentinel, true },
    Case { "background partial alpha", { sensitive, sensitive, sensitive, .5F },
      0, true, true, 8U, sentinel, true },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto scene = scene::Scene("CheckedResolveCoverage", 1U);
    scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    scene.GetEnvironment()
      ->AddSystem<scene::environment::Background>()
      .SetEnabled(test_case.background);
    ctx_.scene = observer_ptr { &scene };
    auto settings = scene::ExposureSettings {};
    settings.mode = test_case.automatic ? engine::ExposureMode::kAuto
                                        : engine::ExposureMode::kManual;
    settings.manual_ev = test_case.ev;
    settings.min_log_luminance = -24.0F;
    auto config = SharedConfig(settings);
    std::vector<Pixel> pixels(width * height, ordinary);
    pixels.back() = test_case.pixel;
    const auto signal = MakeSignal(width, height, pixels);
    auto destination = CreateRegisteredTexture(TextureDesc { .width = width,
      .height = height,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "CheckedSceneColorDestination",
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon });
    std::array<std::byte, height * 256U> initial {};
    for (unsigned y = 0U; y < height; ++y)
      for (unsigned x = 0U; x < width; ++x)
        std::memcpy(initial.data() + y * 256U + x * sizeof(PackedPixel),
          sentinel.data(), sizeof(PackedPixel));
    auto upload = CreateUploadBuffer(SizeBytes { initial.size() });
    upload->Update(initial.data(), initial.size(), 0U);
    {
      auto recorder = AcquireRecorder("Initialize checked resolve sentinel");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, destination, ResourceStates::kCommon);
      recorder->RequireResourceState(*destination, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        { .buffer_offset = 0U,
          .buffer_row_pitch = 256U,
          .buffer_slice_pitch = height * 256U,
          .dst_slice = { .width = width, .height = height, .depth = 1U } },
        *destination);
      recorder->RequireResourceStateFinal(
        *destination, ResourceStates::kShaderResource);
    }
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(
      ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
    const auto uav = allocator.GetShaderVisibleIndex(handle);
    ASSERT_TRUE(Backend()
        .GetResourceRegistry()
        .RegisterView(*destination, std::move(handle),
          TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
            .format = Format::kRGBA16Float,
            .dimension = TextureType::kTexture2D })
        ->IsValid());
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame
      = pass_->ResolveFrame(ctx_, config, { .use_fp32 = test_case.fp32 });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    const auto capture = &test_case == &cases.front()
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    ASSERT_TRUE(pass_->ConvertCheckedSceneColor(ctx_, frame, config,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv },
      *destination, uav));
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    const auto result = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(result.candidate_pre_exposure, test_case.fp32 ? 1.0F : 4.0F);
    EXPECT_EQ(result.expected_products, 1U << 10U);
    EXPECT_EQ(result.checked_products, result.expected_products);
    const bool accepted = test_case.failure == 0U;
    if (accepted)
      EXPECT_EQ(result.failure_flags, 0U);
    else {
      EXPECT_NE(result.failure_flags & test_case.failure, 0U);
      EXPECT_EQ(result.first_failure_product, 11U);
    }
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Checked resolve result");
    {
      auto recorder = AcquireRecorder("Read checked resolve result");
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*destination));
      ASSERT_TRUE(
        readback->EnqueueCopy(*recorder, *destination, {}).has_value());
    }
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    for (unsigned y = 0U; y < height; ++y)
      for (unsigned x = 0U; x < width; ++x) {
        PackedPixel actual {};
        std::memcpy(actual.data(),
          mapped->Data() + y * mapped->Layout().row_pitch.get()
            + x * sizeof(PackedPixel),
          sizeof(PackedPixel));
        EXPECT_EQ(actual,
          !accepted                               ? sentinel
            : y == height - 1U && x == width - 1U ? test_case.last_pixel
                                                  : expected);
      }
    ctx_.scene = {};
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  PreparedSceneExposureIsSolvedOnceAndPinnedAcrossResolvedColorConsumption)
{
  for (const bool persistent : { true, false }) {
    SCOPED_TRACE(persistent);
    auto service = PostProcessService(*renderer_);
    ctx_.current_view.view_state_handle = persistent
      ? CompositionView::ViewStateHandle { 91U }
      : CompositionView::kInvalidViewStateHandle;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
    auto config = PostProcessConfig { .exposure = settings };
    config.tone_mapper = engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    config.enable_bloom = false;
    config.bloom_intensity = 0.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto accumulation = Uniform(.25F, 4U, 4U);
    const auto resolved = Uniform(.5F, 4U, 4U);
    auto& recorder_names
      = static_cast<ExposureFailureGraphics&>(Backend()).recorder_names;
    recorder_names.clear();
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
        { .scene_signal = accumulation.texture.get(),
          .scene_signal_srv = accumulation.srv });
    ASSERT_TRUE(prepared.has_value());
    ASSERT_NE(prepared->exposure.state, nullptr);
    const auto before = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_NEAR(before.displayed_scale, .72F, 2e-5F);
    EXPECT_NEAR(before.raw_metered_luminance, .25F, 2e-5F);
    // A different signal and mapper at Stage 22 must neither remeter nor
    // replace the configuration pinned when the accumulation was solved.
    EXPECT_NEAR(ServicePixel(service, resolved, settings, false, 0.0F, {},
                  engine::ToneMapper::kAcesFitted, false, &*prepared),
      .36F, 2e-5F);
    const auto after = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&before, &after, sizeof(before)), 0);
    EXPECT_EQ(std::count(recorder_names.begin(), recorder_names.end(),
                "Vortex Exposure"),
      1);
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  TonemapSelectsCheckedHalfOrOriginalFloatWithoutChangingExposure)
{
  struct Case {
    const char* name;
    float value;
    float ev;
    bool fp32;
    bool overflow;
    float expected;
  };
  const std::array cases {
    Case { "accepted half", 1.0F / 3.0F, 0, true, false, .333251953125F },
    Case { "overflow fallback", 1.0F / 3.0F, 0, true, true, 1.0F / 3.0F },
    Case { "dark loss fallback", 0x1p-30F, -30, true, false, 1.0F },
    Case { "future candidate cannot approve current conversion", 0x1p20F, 20,
      true, false, 1.0F },
    Case { "nonunit P", 1.0F / 3.0F, -2, false, false, .333251953125F },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto service = PostProcessService(*renderer_);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = test_case.ev;
    settings.key = 12.5F;
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
    auto config = PostProcessConfig { .exposure = settings };
    config.tone_mapper = engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    config.enable_bloom = false;
    config.bloom_intensity = 0.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    static_cast<void>(service.SelectPrecisionCandidate(
      ctx_, { .product_layout_revision = 1U, .expected_products = 1024U }));
    const auto frame = service.PrepareFrameExposure(ctx_, test_case.fp32);
    ASSERT_NE(frame, nullptr);
    std::array<Pixel, 16U> pixels;
    pixels.fill(
      Pixel { test_case.value, test_case.value, test_case.value, 1.0F });
    if (test_case.overflow)
      pixels.back() = Pixel { 0x1p20F, 0x1p20F, 0x1p20F, 1.0F };
    const auto accumulation = MakeSignal(4U, 4U, pixels);
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
        { .scene_signal = accumulation.texture.get(),
          .scene_signal_srv = accumulation.srv });
    ASSERT_TRUE(prepared.has_value());
    const auto before = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    auto destination = CreateRegisteredTexture(TextureDesc { .width = 4U,
      .height = 4U,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "CheckedTonemapHalf",
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon });
    constexpr std::array<std::uint16_t, 4U> sentinel { 0x3400U, 0x3400U,
      0x3400U, 0x3c00U };
    std::array<std::byte, 1024U> initial {};
    for (unsigned y = 0U; y < 4U; ++y)
      for (unsigned x = 0U; x < 4U; ++x)
        std::memcpy(initial.data() + y * 256U + x * sizeof(sentinel),
          sentinel.data(), sizeof(sentinel));
    auto upload = CreateUploadBuffer(SizeBytes { initial.size() });
    upload->Update(initial.data(), initial.size(), 0U);
    {
      auto recorder = AcquireRecorder("Initialize checked tonemap sentinel");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, destination, ResourceStates::kCommon);
      recorder->RequireResourceState(*destination, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        { .buffer_offset = 0U,
          .buffer_row_pitch = 256U,
          .buffer_slice_pitch = 1024U,
          .dst_slice = { .width = 4U, .height = 4U, .depth = 1U } },
        *destination);
      recorder->RequireResourceStateFinal(
        *destination, ResourceStates::kShaderResource);
    }
    const auto bind = [&](ResourceViewType type) {
      auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
      auto allocation
        = allocator.AllocateRaw(type, DescriptorVisibility::kShaderVisible);
      const auto index = allocator.GetShaderVisibleIndex(allocation);
      CHECK_F(Backend()
          .GetResourceRegistry()
          .RegisterView(*destination, std::move(allocation),
            TextureViewDescription { .view_type = type,
              .format = Format::kRGBA16Float,
              .dimension = TextureType::kTexture2D })
          ->IsValid());
      return index;
    };
    const auto uav = bind(ResourceViewType::kTexture_UAV);
    const Signal resolved { destination, bind(ResourceViewType::kTexture_SRV) };
    const auto capture = test_case.overflow
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    ASSERT_TRUE(service.ConvertSceneColor(ctx_, *prepared,
      { .scene_signal = accumulation.texture.get(),
        .scene_signal_srv = accumulation.srv },
      *destination, uav));
    const auto converted_state = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&before, &converted_state, sizeof(before)), 0);
    const auto conversion_before = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = accumulation.texture.get(),
      .srv = accumulation.srv,
      .id = 11U,
      .metering = true } };
    ASSERT_TRUE(service.FinalizeScenePrecision(ctx_, *prepared, products));
    const auto finalized = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_NEAR(
      ServicePixel(service, resolved, settings, false, 0.0F, {},
        engine::ToneMapper::kNone, false, &*prepared, &accumulation, frame),
      test_case.expected, 1e-7F);
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    const auto report = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&conversion_before, &report, sizeof(report)), 0);
    const bool rejected = test_case.overflow || test_case.value == 0x1p-30F
      || test_case.value == 0x1p20F;
    EXPECT_EQ(report.failure_flags != 0U, rejected);
    if (!test_case.fp32 && rejected)
      EXPECT_EQ(finalized.fp16_eligible_streak, 0U);
    if (test_case.fp32 && test_case.overflow)
      EXPECT_EQ(finalized.fp16_eligible_streak, 1U);
    const auto after = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&finalized, &after, sizeof(finalized)), 0);
    const auto* bindings = service.InspectBindings(ctx_.current_view.view_id);
    ASSERT_NE(bindings, nullptr);
    EXPECT_EQ(bindings->scene_fallback_srv, accumulation.srv);
    EXPECT_EQ(bindings->conversion_report_srv, frame->conversion_srv);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, CheckedConversionWaitsForInitialMeteringMaskPolicy)
{
  for (const bool pending : { true, false }) {
    SCOPED_TRACE(pending);
    auto loader = vortex::testing::FakeAssetLoader {};
    auto service = PostProcessService(*renderer_,
      pending ? observer_ptr { &loader }
              : observer_ptr<vortex::testing::FakeAssetLoader> {});
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    auto requested = scene::ExposureSettings {};
    requested.metering_mask = pending ? loader.MintSyntheticTextureKey()
                                      : content::ResourceKey { 123U };
    const auto& captured
      = service.CaptureViewExposureSettings(ctx_.current_view.view_id,
        ctx_.current_view.view_state_handle, requested);
    EXPECT_EQ(captured.revision, 0U);
    EXPECT_EQ(captured.mask_status,
      pending ? PostProcessService::ExposureMaskStatus::kPending
              : PostProcessService::ExposureMaskStatus::kFailed);
    auto config = PostProcessConfig {};
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto source = Uniform(.25F, 4U, 4U);
    const auto inputs
      = PostProcessService::Inputs { .scene_signal = source.texture.get(),
          .scene_signal_srv = source.srv };
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_, inputs);
    ASSERT_TRUE(prepared.has_value());
    auto destination = CreateRegisteredTexture(TextureDesc { .width = 4U,
      .height = 4U,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon });
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto allocation = allocator.AllocateRaw(
      ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
    const auto index = allocator.GetShaderVisibleIndex(allocation);
    ASSERT_TRUE(Backend()
        .GetResourceRegistry()
        .RegisterView(*destination, std::move(allocation),
          TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
            .format = Format::kRGBA16Float,
            .dimension = TextureType::kTexture2D })
        ->IsValid());
    auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
    backend.recorder_names.clear();
    EXPECT_FALSE(
      service.ConvertSceneColor(ctx_, *prepared, inputs, *destination, index));
    EXPECT_TRUE(backend.recorder_names.empty());
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedPreparationCannotReuseAnotherViewsSuccessfulOutputStatus)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  ctx_.current_view.view_id = ViewId { 92U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 92U };
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5F);
  ctx_.current_view.view_id = ViewId { 1U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 1U };
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5F);
  ASSERT_TRUE(service.GetLastExecutionState().wrote_visible_output);
  ctx_.current_view.view_id = ViewId { 92U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 92U };
  ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_next_exposure_recorder = true;
  backend.fail_next_fallback_recorder = true;
  const auto prepared
    = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
  EXPECT_FALSE(prepared.has_value());
  EXPECT_FALSE(service.GetLastExecutionState().wrote_visible_output);
  EXPECT_EQ(service.GetLastExecutionState().view_id, ctx_.current_view.view_id);
}

NOLINT_TEST_F(ExposureGpuTest,
  Fp16EligibilityRequiresStableCompleteFramesAndPreservesExposure)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  auto config = SharedConfig(settings);
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const auto brighter = Uniform(2.0F, 4U, 4U);
  const auto invalid = Uniform(std::numeric_limits<float>::infinity(), 4U, 4U);
  auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  const auto step
    = [&](std::uint64_t sequence, std::uint64_t layout, std::uint64_t revision,
        const Signal& signal, std::uint32_t expected = 1024U,
        bool invalidate = false, bool metering_available = true) {
        config = SharedConfig(settings, {}, revision);
        return EligibilityStep(signal, config, sequence, layout, expected,
          invalidate, nullptr, *token, metering_available, sequence == 2U);
      };
  EXPECT_EQ(step(1U, 7U, 1U, ordinary).first.fp16_eligible_streak, 1U);
  const auto eligible = step(2U, 7U, 1U, ordinary);
  EXPECT_EQ(eligible.first.fp16_eligible_streak, 2U);
  EXPECT_EQ(eligible.first.flags & 256U, 256U);
  EXPECT_EQ(eligible.first.fp16_candidate_pre_exposure, 8192.0F);
  const auto failure = step(3U, 7U, 1U, invalid);
  EXPECT_EQ(failure.first.fp16_eligible_streak, 0U);
  EXPECT_NE(failure.second.flags & 2U, 0U);
  EXPECT_EQ(failure.second.first_failure_product, 11U);
  EXPECT_NE(failure.second.first_failure_kind & 1U, 0U);
  EXPECT_EQ(step(4U, 7U, 1U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(5U, 7U, 1U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(6U, 7U, 2U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(7U, 7U, 2U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(8U, 8U, 2U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(9U, 8U, 2U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(10U, 8U, 2U, brighter).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(12U, 8U, 2U, brighter).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(
    step(13U, 8U, 2U, brighter, 1024U, true).first.fp16_eligible_streak, 1U);
  const auto missing = step(14U, 8U, 2U, brighter, 1025U);
  EXPECT_EQ(missing.first.fp16_eligible_streak, 0U);
  EXPECT_EQ(missing.second.first_failure_product, 1U);
  EXPECT_NE(missing.second.first_failure_kind & 16U, 0U);
  EXPECT_EQ(
    step(0xffffffffULL, 8U, 2U, brighter).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(
    step(0x100000000ULL, 8U, 2U, brighter).first.fp16_eligible_streak, 2U);
  settings.mode = engine::ExposureMode::kAuto;
  config = SharedConfig(settings);
  token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(token.has_value());
  const auto pending
    = step(0x100000001ULL, 8U, 3U, ordinary, 1024U, false, false);
  EXPECT_EQ(pending.first.fp16_eligible_streak, 0U);
  EXPECT_NE(
    pending.first.requested_generation, pending.first.applied_generation);
  EXPECT_EQ(
    step(0x100000002ULL, 8U, 3U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(
    step(0x100000003ULL, 8U, 3U, ordinary).first.fp16_eligible_streak, 2U);
}

NOLINT_TEST_F(ExposureGpuTest, Fp16EligibilityBelongsToEachBorrowingImage)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto root = CompositionView::ViewStateHandle { 10U };
  const auto borrower = CompositionView::ViewStateHandle { 20U };
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const std::array wide_pixels { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1 },
    Pixel { 0x1p-16F, 0x1p-16F, 0x1p-16F, 1 } };
  const auto wide = MakeSignal(2U, 1U, wide_pixels);
  const auto source
    = postprocess::ExposurePass::Source { .handle = root, .config = config };
  const auto step
    = [&](std::uint64_t sequence, bool sharing, const Signal& signal) {
        ctx_.current_view.view_state_handle = sharing ? borrower : root;
        ctx_.current_view.view_id = ViewId { sharing ? 20U : 10U };
        return EligibilityStep(signal, config, sequence, 1U, 1024U, false,
          sharing ? &source : nullptr)
          .first;
      };
  EXPECT_EQ(step(1U, false, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(1U, true, ordinary).fp16_eligible_streak, 0U);
  EXPECT_EQ(step(2U, false, ordinary).fp16_eligible_streak, 2U);
  EXPECT_EQ(step(2U, true, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(3U, false, ordinary).fp16_eligible_streak, 2U);
  const auto rejected = step(3U, true, wide);
  EXPECT_EQ(rejected.fp16_eligible_streak, 0U);
  EXPECT_EQ(rejected.displayed_scale, 1.0F);
  EXPECT_EQ(step(4U, true, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(5U, true, ordinary).fp16_eligible_streak, 2U);
}

NOLINT_TEST_F(
  ExposureGpuTest, Fp16EligibilityExcludesStatelessAndDiagnosticFrames)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  auto config = SharedConfig(settings);
  const auto signal = Uniform(1.0F, 4U, 4U);
  ctx_.current_view.view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_EQ(EligibilityStep(signal, config, 1U).first.fp16_eligible_streak, 0U);
  EXPECT_EQ(EligibilityStep(signal, config, 2U).first.fp16_eligible_streak, 0U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 30U };
  config = config.WithDiagnosticOverride(true);
  EXPECT_EQ(EligibilityStep(signal, config, 3U).first.fp16_eligible_streak, 0U);
  EXPECT_EQ(EligibilityStep(signal, config, 4U).first.fp16_eligible_streak, 0U);
  config = config.WithDiagnosticOverride(false);
  EXPECT_EQ(EligibilityStep(signal, config, 5U).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(EligibilityStep(signal, config, 6U).first.fp16_eligible_streak, 2U);
  ctx_.frame_sequence = frame::SequenceNumber { 7U };
  ASSERT_TRUE(RecordShared(signal, config).executed);
  EXPECT_EQ(EligibilityStep(signal, config, 8U).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(EligibilityStep(signal, config, 9U).first.fp16_eligible_streak, 2U);
  ctx_.frame_sequence = frame::SequenceNumber { 10U };
  const auto unsolved = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(unsolved, nullptr);
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = signal.texture.get(), .srv = signal.srv, .id = 11U } };
  ASSERT_TRUE(
    pass_->EvaluateFp16Products(ctx_, unsolved, config, products, {}));
  ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, unsolved,
    { .product_layout_revision = 7U, .expected_products = 1024U }));
  const auto status = Read<ExposureCompletedStatus>(
    *unsolved->current_state->status_buffer, ResourceStates::kCopySource);
  EXPECT_EQ(status.flags & (1U | 4U), 0U);
  EXPECT_EQ(status.fp16_eligible_streak, 0U);
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

NOLINT_TEST_F(ExposureGpuTest,
  CompletedPrecisionAdmissionTracksViewSettingsLayoutAndDiagnostics)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const auto invalid = Uniform(std::numeric_limits<float>::infinity(), 4U, 4U);
  auto layout = std::uint64_t { 7U };
  const auto step = [&](bool expected_candidate, std::uint32_t expected_streak,
                      const Signal& signal, bool diagnostic = false) {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    ctx_.render_mode = diagnostic ? RenderMode::kWireframe : RenderMode::kSolid;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    const auto requirements = postprocess::ExposurePass::EligibilityInputs {
      .product_layout_revision = layout, .expected_products = 1024U
    };
    const auto candidate = service.SelectPrecisionCandidate(ctx_, requirements);
    EXPECT_EQ(candidate != nullptr, expected_candidate) << sequence_;
    EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), candidate);
    EXPECT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    CHECK_F(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    EXPECT_EQ(
      service.FinalizeScenePrecision(ctx_, *prepared, products), !diagnostic);
    if (!diagnostic) {
      EXPECT_TRUE(service.FinalizeScenePrecision(ctx_, *prepared, products));
      EXPECT_EQ(
        ReadState(prepared->exposure).fp16_eligible_streak, expected_streak);
    }
    const auto counts
      = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
        service, ctx_.current_view.view_state_handle);
    EXPECT_LE(counts.first, 1U);
    EXPECT_EQ(counts.second, 0U);
    return candidate;
  };
  EXPECT_EQ(step(false, 1U, ordinary), nullptr);
  EXPECT_EQ(step(false, 2U, ordinary), nullptr);
  const auto first = step(true, 2U, ordinary);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*first->buffer, ResourceStates::kShaderResource)
      .frame_sequence[0],
    2U);
  layout = 8U;
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  config.exposure.manual_ev = 1.0F;
  service.SetConfig(config);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  step(false, 0U, ordinary, true);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  step(true, 0U, invalid);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  service.RemoveViewState(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle);
  step(false, 1U, ordinary);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, PrecisionStatusRetriesTransportWithoutEarlyAdmission)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_status_recorder = true;
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  for (unsigned i = 0U; i < 6U; ++i) {
    EXPECT_EQ(begin(), nullptr);
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    ASSERT_TRUE(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    ASSERT_TRUE(service.FinalizeScenePrecision(ctx_, *prepared, products));
    const auto counts
      = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
        service, ctx_.current_view.view_state_handle);
    EXPECT_EQ(counts.first, 0U);
    EXPECT_EQ(counts.second, 1U);
  }
  backend.fail_status_recorder = false;
  EXPECT_EQ(
    begin(), nullptr); // Retry records a copy; it is not an acknowledgement.
  const auto candidate = begin();
  ASSERT_NE(candidate, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*candidate->buffer, ResourceStates::kShaderResource)
      .frame_sequence[0],
    6U);
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), nullptr);
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedPrecisionRecordingCannotAdmitAnOlderDeferredCertificate)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_status_recorder = true;
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  for (unsigned i = 0U; i < 3U; ++i) {
    EXPECT_EQ(begin(), nullptr);
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    ASSERT_TRUE(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    backend.fail_next_suitability_recorder = i == 2U;
    EXPECT_EQ(
      service.FinalizeScenePrecision(ctx_, *prepared, products), i != 2U);
  }
  backend.fail_status_recorder = false;
  EXPECT_EQ(begin(), nullptr);
  EXPECT_EQ(begin(), nullptr);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, ProducerRangeFailureSurvivesSolveAndPreventsAutoAdaptation)
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
  ASSERT_EQ(
    console.Execute("vtx.volumetric_fog.temporal_reprojection false").status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("Producer range", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto color = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target
    = Backend().CreateFramebuffer(FramebufferDesc {}.AddColorAttachment(color));
  struct Probe final : IViewExtension {
    EnvironmentLightingService half_producer;
    bool inject_half { false };
    postprocess::ExposurePass::FrameLease frame;
    std::shared_ptr<const Texture> half_texture;
    explicit Probe(Renderer& renderer)
      : half_producer(renderer)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPreRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      EXPECT_FLOAT_EQ(hook.render_context.delta_time, 1.0F);
      if (!inject_half)
        return;
      auto& ctx = hook.render_context;
      const auto old_format = ctx.current_view.hdr_color_format;
      ctx.current_view.hdr_color_format = Format::kRGBA16Float;
      half_producer.OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
      static_cast<void>(half_producer.PublishEnvironmentBindings(ctx));
      const auto* resources
        = half_producer.InspectViewRadianceResources(ctx.current_view.view_id);
      CHECK_NOTNULL_F(resources);
      half_texture = resources->volumetric_fog;
      ctx.current_view.hdr_color_format = old_format;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      frame = hook.render_context.current_view.frame_exposure;
    }
  };
  auto probe = std::make_shared<Probe>(*renderer_);
  renderer_->RegisterViewExtension(probe);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  auto timing = engine::ModuleTimingData {};
  timing.game_delta_time
    = time::CanonicalDuration { std::chrono::nanoseconds { 1000000000 } };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  ExposureStateData previous {};
  for (unsigned step = 1U; step <= 4U; ++step) {
    SCOPED_TRACE(step);
    const float sky_value = step == 1U ? .25F : 4.0F;
    sky.SetSolidColorRgb({ sky_value, sky_value, sky_value });
    const auto emissive = step == 1U ? .125F
      : step == 3U                   ? std::numeric_limits<float>::infinity()
                                     : 0x1p26F;
    fog.SetVolumetricFogEmissive({ emissive, emissive, emissive });
    probe->inject_half = step == 2U;
    scene->Update();
    const auto slot = frame::Slot { (step - 1U) % 3U };
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { step },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    auto input = Renderer::OffscreenSceneViewInput::FromCamera(
      "Producer range", ViewId { 941U }, view, camera);
    input.SetViewStateHandle(CompositionView::ViewStateHandle { 941U });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = slot,
      .frame_sequence = frame::SequenceNumber { step },
      .delta_time_seconds = 1.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(input);
    facade.SetOutputTarget({ .framebuffer = observer_ptr { target.get() } });
    auto session = facade.Finalize();
    ASSERT_TRUE(session.has_value());
    const auto capture = step == 2U ? BeginOptionalCapture()
                                    : observer_ptr<FrameCaptureController> {};
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    ASSERT_NE(probe->frame, nullptr);
    const auto state = Read<ExposureStateData>(
      *probe->frame->current_state->buffer, ResourceStates::kShaderResource);
    const auto status = Read<ExposureCompletedStatus>(
      *probe->frame->current_state->status_buffer, ResourceStates::kCopySource);
    if (step == 2U || step == 3U) {
      EXPECT_EQ(status.flags & 18U, 18U);
      EXPECT_EQ(status.first_failure_product, 10U);
      EXPECT_NE(status.first_failure_kind & (step == 2U ? 2U : 1U), 0U);
      EXPECT_EQ(status.fp16_eligible_streak, 0U);
      EXPECT_EQ(state.displayed_scale, previous.displayed_scale);
      EXPECT_EQ(state.latent_scale, previous.latent_scale);
      EXPECT_EQ(state.flags & 12U, 0U);
      EXPECT_NE(state.flags & 32U, 0U);
    } else {
      EXPECT_EQ(status.flags & 16U, 0U);
      EXPECT_NE(state.flags & 4U, 0U);
      if (step == 4U)
        EXPECT_LT(state.displayed_scale, previous.displayed_scale);
    }
    if (step == 2U) {
      ASSERT_NE(probe->half_texture, nullptr);
      EXPECT_EQ(
        probe->half_texture->GetDescriptor().format, Format::kRGBA16Float);
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Overflowed half fog");
      {
        auto recorder = AcquireRecorder("Overflowed half fog readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*probe->half_texture));
        ASSERT_TRUE(readback
            ->EnqueueCopy(*recorder, *probe->half_texture,
              { .src_slice
                = { .z = 31U, .width = 1U, .height = 1U, .depth = 1U } })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      std::uint16_t red;
      std::memcpy(&red, mapped->Data(), sizeof(red));
      EXPECT_EQ(red, 0x7bffU); // The stored half has clipped to 65504.
    }
    previous = state;
    renderer_->OnFrameEnd(observer_ptr { &frame });
    WaitForQueueIdle();
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedSolveFallbackRestartsPrecisionWithoutRejectingSuccessfulReuse)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto inputs
    = PostProcessService::Inputs { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv };
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = signal.texture.get(),
    .srv = signal.srv,
    .id = 11U,
    .metering = true } };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  const auto finish = [&](std::uint32_t streak, bool fail, bool reuse) {
    SCOPED_TRACE(sequence_);
    CHECK_NOTNULL_F(service.PrepareFrameExposure(ctx_, true).get());
    backend.fail_next_exposure_recorder = fail;
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_, inputs);
    CHECK_F(prepared.has_value());
    EXPECT_EQ(prepared->exposure.executed, !fail);
    CHECK_NOTNULL_F(prepared->exposure.state.get());
    if (reuse) {
      const auto reused
        = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_, inputs);
      CHECK_F(reused.has_value());
      EXPECT_FALSE(reused->exposure.executed);
      EXPECT_EQ(reused->exposure.state, prepared->exposure.state);
      EXPECT_TRUE(service.FinalizeScenePrecision(ctx_, *reused, products));
    }
    EXPECT_EQ(service.FinalizeScenePrecision(ctx_, *prepared, products), !fail);
    const auto state = ReadState(prepared->exposure);
    if (!fail)
      EXPECT_EQ(state.fp16_eligible_streak, streak);
    return state;
  };
  EXPECT_EQ(begin(), nullptr);
  const auto initial = finish(1U, false, false);
  EXPECT_EQ(begin(), nullptr);
  finish(2U, false, false);
  ASSERT_NE(begin(), nullptr);
  backend.fail_status_recorder = true;
  finish(2U, false, false);
  ASSERT_NE(begin(), nullptr);
  const auto fallback = finish(0U, true, false);
  EXPECT_EQ(fallback.displayed_scale, initial.displayed_scale);
  EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), nullptr);
  backend.fail_status_recorder = false;
  EXPECT_EQ(begin(), nullptr);
  finish(1U, false, true);
  EXPECT_EQ(begin(), nullptr);
  finish(2U, false, true);
  ASSERT_NE(begin(), nullptr);
  finish(2U, false, true);

  const auto seed
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
  ASSERT_TRUE(seed.has_value());
  EXPECT_EQ(begin(), nullptr);
  const auto pending_fallback = finish(0U, true, false);
  EXPECT_EQ(pending_fallback.displayed_scale, initial.displayed_scale);
  const auto pending = renderer_->InspectExposureTransition(seed->target);
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending->phase, ExposureTransitionPhase::kQueued);
  EXPECT_LT(pending->applied_generation, seed->generation);
  EXPECT_EQ(begin(), nullptr);
  const auto seeded = finish(1U, false, true);
  EXPECT_EQ(seeded.displayed_scale, 0x1p-4F);
  EXPECT_EQ(begin(), nullptr);
  const auto applied = renderer_->InspectExposureTransition(seed->target);
  ASSERT_TRUE(applied.has_value());
  EXPECT_EQ(applied->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(applied->applied_generation, seed->generation);
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest,
  FogErrorBoundsContainRepeatedHalfHistoryAndSurviveFloatRecovery)
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
  ASSERT_EQ(
    console.Execute("vtx.volumetric_fog.temporal_reprojection true").status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("Fog error bounds", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  post.SetExposureSettings(settings);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto color = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target
    = Backend().CreateFramebuffer(FramebufferDesc {}.AddColorAttachment(color));
  struct Probe final : IViewExtension {
    Renderer& renderer;
    EnvironmentLightingService producer;
    bool use_half { true };
    postprocess::ExposurePass::FrameLease frame;
    std::shared_ptr<const Texture> observed;
    std::shared_ptr<const Texture> reference;
    explicit Probe(Renderer& value)
      : renderer(value)
      , producer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPreRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto& ctx = hook.render_context;
      const auto old_format = ctx.current_view.hdr_color_format;
      ctx.current_view.hdr_color_format
        = use_half ? Format::kRGBA16Float : Format::kRGBA32Float;
      producer.OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
      static_cast<void>(producer.PublishEnvironmentBindings(ctx));
      if (ctx.frame_sequence.get() == 1U) {
        static_cast<void>(producer.PublishEnvironmentBindings(ctx));
        EXPECT_FALSE(producer.GetLastViewProductGenerationState()
            .volumetric_fog_temporal_history_reprojection_executed);
      }
      const auto* resources
        = producer.InspectViewRadianceResources(ctx.current_view.view_id);
      CHECK_NOTNULL_F(resources);
      observed = resources->volumetric_fog;
      ctx.current_view.hdr_color_format = old_format;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      reference = vortex::testing::RendererPublicationProbe::FogHistory(
        *owner, hook.render_context.current_view.view_id)
                    .first;
      frame = hook.render_context.current_view.frame_exposure;
    }
  };
  auto probe = std::make_shared<Probe>(*renderer_);
  renderer_->RegisterViewExtension(probe);
  const auto half_to_double = [](std::uint16_t bits) {
    const auto exponent = (bits >> 10U) & 31U;
    const auto mantissa = bits & 1023U;
    const double magnitude = exponent == 0U
      ? std::ldexp(double(mantissa), -24)
      : std::ldexp(double(1024U + mantissa), int(exponent) - 25);
    return bits & 0x8000U ? -magnitude : magnitude;
  };
  const auto read_volume = [&](const Texture& texture) {
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Fog bound volume");
    {
      auto recorder = AcquireRecorder("Fog bound volume readback");
      CHECK_F(recorder->AdoptKnownResourceState(texture));
      CHECK_F(readback->EnqueueCopy(*recorder, texture, {}).has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    const auto& desc = texture.GetDescriptor();
    std::vector<std::array<double, 4>> values(
      desc.width * desc.height * desc.depth);
    const bool half = desc.format == Format::kRGBA16Float;
    for (unsigned z = 0U; z < desc.depth; ++z)
      for (unsigned y = 0U; y < desc.height; ++y)
        for (unsigned x = 0U; x < desc.width; ++x) {
          const auto* bytes = mapped->Data()
            + z * mapped->Layout().slice_pitch.get()
            + y * mapped->Layout().row_pitch.get() + x * (half ? 8U : 16U);
          auto& value = values[(z * desc.height + y) * desc.width + x];
          for (unsigned c = 0U; c < 4U; ++c) {
            if (half) {
              std::uint16_t bits;
              std::memcpy(&bits, bytes + c * 2U, 2U);
              value[c] = half_to_double(bits);
            } else {
              float component;
              std::memcpy(&component, bytes + c * 4U, 4U);
              value[c] = component;
            }
          }
        }
    return values;
  };
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  double opacity = 1.0;
  double previous_observed = 1.0;
  double maximum_error = 0.0;
  double last_half_error = 0.0;
  HdrErrorBoundsData last_half_bounds;
  for (unsigned step = 1U; step <= 74U; ++step) {
    SCOPED_TRACE(step);
    const double desired = 1.0 + 1.0 / 2048.0 + 1.0 / 4194304.0;
    const double weight = double(.9F);
    const double fresh = step == 1U ? 1.0
      : step == 73U                 ? std::numeric_limits<double>::infinity()
      : step == 74U                 ? .25
                    : (desired - weight * previous_observed) / (1.0 - weight);
    const float emissive = float(std::max(fresh, 0.0) / opacity);
    fog.SetVolumetricFogEmissive({ emissive, emissive, emissive });
    probe->use_half = step <= 64U;
    scene->Update();
    const auto slot = frame::Slot { (step - 1U) % 3U };
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { step },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    auto input = Renderer::OffscreenSceneViewInput::FromCamera(
      "Fog bounds", ViewId { 942U }, view, camera);
    input.SetViewStateHandle(CompositionView::ViewStateHandle { 942U });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = slot,
      .frame_sequence = frame::SequenceNumber { step },
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(input);
    facade.SetOutputTarget({ .framebuffer = observer_ptr { target.get() } });
    auto session = facade.Finalize();
    ASSERT_TRUE(session.has_value());
    const auto capture = step == 64U ? BeginOptionalCapture()
                                     : observer_ptr<FrameCaptureController> {};
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    ASSERT_NE(probe->observed, nullptr);
    ASSERT_NE(probe->reference, nullptr);
    const auto observed = read_volume(*probe->observed);
    const auto reference = read_volume(*probe->reference);
    ASSERT_EQ(observed.size(), reference.size());
    const auto storage = Read<ExposureStatusStorage>(
      *probe->frame->current_state->status_buffer, ResourceStates::kCopySource);
    const auto& bounds = storage.producer_errors[2];
    if (step == 73U) {
      EXPECT_TRUE(std::isinf(bounds.rgb_absolute));
      EXPECT_NE(storage.completed.flags & 16U, 0U);
      renderer_->OnFrameEnd(observer_ptr { &frame });
      WaitForQueueIdle();
      continue;
    }
    EXPECT_TRUE(std::isfinite(bounds.rgb_relative));
    EXPECT_TRUE(std::isfinite(bounds.rgb_absolute));
    for (std::size_t i = 0U; i < observed.size(); ++i)
      for (unsigned c = 0U; c < 4U; ++c) {
        const double error = std::abs(observed[i][c] - reference[i][c]);
        const double allowance = c == 3U
          ? double(bounds.transmittance_relative) * reference[i][c]
            + bounds.transmittance_absolute
          : double(bounds.rgb_relative) * reference[i][c] + bounds.rgb_absolute;
        EXPECT_LE(error, allowance) << "voxel=" << i << " channel=" << c;
      }
    const double error = std::abs(observed.back()[0] - reference.back()[0]);
    maximum_error = std::max(maximum_error, error);
    if (step == 1U)
      opacity = reference.back()[0];
    previous_observed = observed.back()[0];
    if (step == 64U) {
      last_half_error = error;
      last_half_bounds = bounds;
    }
    if (step == 65U) {
      EXPECT_GT(
        error, 0.0); // FP32 storage does not erase reused history error.
      EXPECT_GT(bounds.rgb_absolute + bounds.rgb_relative, 0.0F);
    }
    if (step == 72U) {
      EXPECT_LT(error, last_half_error);
      EXPECT_LT(bounds.rgb_absolute, last_half_bounds.rgb_absolute);
    }
    if (step == 74U) {
      EXPECT_EQ(error, 0.0);
      EXPECT_EQ(bounds.rgb_relative, 0.0F);
      EXPECT_EQ(bounds.rgb_absolute, 0.0F);
      EXPECT_EQ(bounds.transmittance_relative, 0.0F);
      EXPECT_EQ(bounds.transmittance_absolute, 0.0F);
      EXPECT_EQ(storage.completed.flags & 16U, 0U);
    }
    renderer_->OnFrameEnd(observer_ptr { &frame });
    WaitForQueueIdle();
  }
  EXPECT_GT(maximum_error, .001);
  RecordProperty("maximum_observed_error", std::to_string(maximum_error));
  RecordProperty("last_half_error", std::to_string(last_half_error));
  RecordProperty(
    "last_half_relative_bound", std::to_string(last_half_bounds.rgb_relative));
  RecordProperty(
    "last_half_absolute_bound", std::to_string(last_half_bounds.rgb_absolute));
}

} // namespace
