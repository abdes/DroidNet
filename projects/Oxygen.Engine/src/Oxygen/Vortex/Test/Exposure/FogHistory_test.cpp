//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Passes/FogPass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>
#include <Oxygen/Vortex/Types/ViewHistoryFrameBindings.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(
  ExposureGpuTest, FogHistoryClampsEdgesAndRejectsOutsideCoordinates)
{
  namespace root = oxygen::bindless::generated::d3d12;
  const auto bindings = ExposureProbeRootBindings();
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest { .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS" })
        .SetRootBindings(bindings)
        .SetDebugName("Fog history edge fixture")
        .Build();
  auto output = CreateRegisteredTexture({ .width = 1U,
    .height = 1U,
    .depth = 2U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon });
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*output, std::move(handle),
    TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D });
  std::array<Pixel, 8> samples;
  for (unsigned z = 0; z < 2; ++z) {
    for (unsigned y = 0; y < 2; ++y) {
      for (unsigned x = 0; x < 2; ++x) {
        samples[(z * 2 + y) * 2 + x]
          = Pixel { float(x), float(y), float(z), 1.0F - float(z) };
      }
    }
  }
  const std::array coordinates { glm::vec2 { 0, .5F }, glm::vec2 { 1, .5F },
    glm::vec2 { .5F, 0 }, glm::vec2 { .5F, 1 }, glm::vec2 { .0625F, .5F },
    glm::vec2 { .9375F, .5F }, glm::vec2 { .5F, .0625F },
    glm::vec2 { .5F, .9375F }, glm::vec2 { .25F, .25F }, glm::vec2 { .5F, .5F },
    glm::vec2 { .75F, .75F }, glm::vec2 { -.01F, .5F },
    glm::vec2 { 1.01F, .5F }, glm::vec2 { .5F, -.01F },
    glm::vec2 { .5F, 1.01F } };
  const auto capture = BeginOptionalCapture();
  for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float }) {
    const auto volume = MakeSignal(2, 2, samples, 2, format, true);
    auto params
      = vortex::testing::RendererPublicationProbe::FogPassConstants {};
    params.output_header = { output_slot.get(), 1U, 1U, 2U };
    params.grid.end_distance_m = 10.0F;
    params.grid_z.grid_z_params[0] = 1.0F;
    params.grid_z.grid_z_params[1] = 0.0F;
    params.grid_z.grid_z_params[2] = 1.0F;
    params.temporal_history0 = { volume.srv.get(), 1U, 1.0F, 1U };
    const auto pass_slot = PublishFixtureData(params);
    for (const auto uv : coordinates) {
      for (const float depth : { 1.0F, std::sqrt(2.0F), 3.0F, .5F, 8.0F }) {
        SCOPED_TRACE(uv.x);
        SCOPED_TRACE(uv.y);
        SCOPED_TRACE(depth);
        auto history = ViewHistoryFrameBindings {};
        history.validity_flags = static_cast<std::uint32_t>(
          ViewHistoryValidityFlagBits::kPreviousViewValid);
        history.previous_view_matrix[3].z = -depth;
        history.previous_projection_matrix = glm::mat4 { 0.0F };
        history.previous_projection_matrix[3]
          = { 2 * uv.x - 1, 1 - 2 * uv.y, 0, 1 };
        auto frame_bindings = ViewFrameBindings {};
        frame_bindings.history_frame_slot = PublishFixtureData(history);
        auto view = ViewConstants::GpuData {};
        view.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
          PublishFixtureData(frame_bindings)
        };
        view.inverse_view_projection_matrix = glm::mat4 { 0.0F };
        view.inverse_view_projection_matrix[3].w = 1.0F;
        auto view_buffer
          = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
        view_buffer->Update(&view, sizeof(view), 0U);
        {
          auto recorder = AcquireRecorder("Fog history edge dispatch");
          if (!recorder->AdoptKnownResourceState(*output)) {
            recorder->BeginTrackingResourceState(
              *output, ResourceStates::kCommon, false);
          }
          recorder->RequireResourceState(
            *output, ResourceStates::kUnorderedAccess);
          recorder->FlushBarriers();
          recorder->SetPipelineState(pipeline);
          recorder->SetComputeRootConstantBufferView(
            static_cast<std::uint32_t>(root::RootParam::kViewConstants),
            view_buffer->GetGPUVirtualAddress());
          recorder->SetComputeRoot32BitConstant(
            static_cast<std::uint32_t>(root::RootParam::kRootConstants), 0U,
            0U);
          recorder->SetComputeRoot32BitConstant(
            static_cast<std::uint32_t>(root::RootParam::kRootConstants),
            pass_slot.get(), 1U);
          recorder->Dispatch(1U, 1U, 1U);
        }
        const bool rejected = uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1
          || depth < 1 || depth >= 4;
        const double w = std::clamp(std::log2(double(depth)) / 2, 0.0, 1.0);
        const double z = std::clamp(2 * w - .5, 0.0, 1.0);
        const std::array expected = rejected
          ? std::array<double, 4> { 0, 0, 0, 1 }
          : std::array<double, 4> { std::clamp(2 * double(uv.x) - .5, 0.0, 1.0),
              std::clamp(2 * double(uv.y) - .5, 0.0, 1.0), z, 1 - z };
        for (const auto& actual : ReadFloatTexture(*output)) {
          for (unsigned c = 0; c < 4; ++c) {
            EXPECT_NEAR(actual[c], expected[c], 3e-6);
          }
        }
      }
    }
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, FogStableCellsPreserveHistoryAndHomogeneousMedia)
{
  namespace root = oxygen::bindless::generated::d3d12;
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest { .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS" })
        .SetRootBindings(ExposureProbeRootBindings())
        .SetDebugName("Fog stable cell fixture")
        .Build();
  auto output = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .depth = 4U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon });
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*output, std::move(handle),
    TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D });
  std::array<Pixel, 64> samples;
  for (unsigned z = 0; z < 4; ++z) {
    for (unsigned y = 0; y < 4; ++y) {
      for (unsigned x = 0; x < 4; ++x) {
        samples[(z * 4 + y) * 4 + x]
          = { float(x) / 4, float(y) / 4, float(z) / 4, 1 - float(z) / 4 };
      }
    }
  }
  auto history = ViewHistoryFrameBindings {};
  history.validity_flags = static_cast<std::uint32_t>(
    ViewHistoryValidityFlagBits::kPreviousViewValid);
  history.previous_projection_matrix[2][2] = -1.0F / 32;
  auto frame_bindings = ViewFrameBindings {};
  frame_bindings.history_frame_slot = PublishFixtureData(history);
  auto view = ViewConstants::GpuData {};
  view.view_frame_bindings_bslot
    = BindlessViewFrameBindingsSlot { PublishFixtureData(frame_bindings) };
  view.projection_matrix = history.previous_projection_matrix;
  view.inverse_view_projection_matrix = glm::mat4 { 1.0F };
  view.inverse_view_projection_matrix[2][2] = -32.0F;
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  view_buffer->Update(&view, sizeof(view), 0U);
  const std::array offsets { glm::vec4 { .5F, .5F, .5F, 0 },
    glm::vec4 { .125F, .375F, .75F, 0 }, glm::vec4 { .875F, .625F, .25F, 0 } };
  const auto capture = BeginOptionalCapture();
  for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float }) {
    const auto volume = MakeSignal(4, 4, samples, 4, format, true);
    for (const bool reuse_history : { false, true }) {
      for (const float density : { .06F, 0x1p-40F }) {
        for (const float fade_distance : { 0.0F, 2.0F }) {
          for (const auto offset : offsets) {
            SCOPED_TRACE(reuse_history);
            SCOPED_TRACE(fade_distance);
            SCOPED_TRACE(offset.z);
            SCOPED_TRACE(density);
            const float emission_scale = density < .001F ? 1e9F : 1.0F;
            auto params
              = vortex::testing::RendererPublicationProbe::FogPassConstants {};
            params.output_header = { output_slot.get(), 4U, 4U, 4U };
            params.grid = { 1.0F, 32.0F, fade_distance, 1.0F };
            params.grid_z = { { 1.0F, 0.0F, 1.0F }, 0.0F };
            params.height_fog0.primary_density = density;
            params.height_fog1.match_height_fog_factor = 1.0F;
            params.height_fog1.enabled = 1U;
            params.media1 = { { 2 * emission_scale, 3 * emission_scale,
                                4 * emission_scale },
              1.0F };
            params.temporal_history0 = { volume.srv.get(),
              reuse_history ? (format == Format::kRGBA16Float ? 3U : 1U) : 0U,
              1.0F, 1U };
            for (unsigned c = 0; c < 4; ++c) {
              params.temporal_history1.frame_jitter_offsets[0][c] = offset[c];
            }
            const auto pass_slot = PublishFixtureData(params);
            {
              auto recorder = AcquireRecorder("Fog stable cell dispatch");
              if (!recorder->AdoptKnownResourceState(*output)) {
                recorder->BeginTrackingResourceState(
                  *output, ResourceStates::kCommon, false);
              }
              recorder->RequireResourceState(
                *output, ResourceStates::kUnorderedAccess);
              recorder->FlushBarriers();
              recorder->SetPipelineState(pipeline);
              recorder->SetComputeRootConstantBufferView(
                static_cast<std::uint32_t>(root::RootParam::kViewConstants),
                view_buffer->GetGPUVirtualAddress());
              recorder->SetComputeRoot32BitConstant(
                static_cast<std::uint32_t>(root::RootParam::kRootConstants), 0U,
                0U);
              recorder->SetComputeRoot32BitConstant(
                static_cast<std::uint32_t>(root::RootParam::kRootConstants),
                pass_slot.get(), 1U);
              recorder->Dispatch(1U, 1U, 1U);
            }
            const auto actual = ReadFloatTexture(*output);
            ASSERT_EQ(actual.size(), samples.size());
            for (unsigned i = 0; i < samples.size(); ++i) {
              // Independent fixed-depth Beer-Lambert oracle, including near
              // fade.
              const double length = std::exp2(double(i / 16) + .5) - 1;
              const double fade = fade_distance > 0
                ? std::min(length / double(fade_distance), 1.0)
                : 1.0;
              const double t = std::exp(-double(density) * fade * length);
              const double opacity
                = -std::expm1(-double(density) * fade * length);
              for (unsigned c = 0; c < 4; ++c) {
                const double expected = reuse_history ? double(samples[i][c])
                  : c == 3                            ? t
                           : double(params.media1.emissive_rgb[c]) * opacity;
                const double tolerance = reuse_history
                  ? 3e-6
                  : std::min(3e-6, std::abs(expected) * 2e-5 + 0x1p-120);
                EXPECT_NEAR(actual[i][c], expected, tolerance)
                  << "voxel=" << i << " channel=" << c;
              }
            }
          }
        }
      }
    }
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
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
    for (unsigned z = 0U; z < desc.depth; ++z) {
      for (unsigned y = 0U; y < desc.height; ++y) {
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
  unsigned capture_step = 64U;
  char* capture_step_text = nullptr;
  std::size_t capture_step_size = 0U;
  if (_dupenv_s(&capture_step_text, &capture_step_size,
        "OXYGEN_EXPOSURE_FOG_CAPTURE_STEP")
      == 0
    && capture_step_text) {
    capture_step
      = static_cast<unsigned>(std::strtoul(capture_step_text, nullptr, 10));
    std::free(capture_step_text);
    ASSERT_GE(capture_step, 1U);
    ASSERT_LE(capture_step, 74U);
  }
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
    const auto capture = step == capture_step
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
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
    for (std::size_t i = 0U; i < observed.size(); ++i) {
      for (unsigned c = 0U; c < 4U; ++c) {
        const double error = std::abs(observed[i][c] - reference[i][c]);
        const double allowance = c == 3U
          ? double(bounds.transmittance_relative) * reference[i][c]
            + bounds.transmittance_absolute
          : double(bounds.rgb_relative) * reference[i][c] + bounds.rgb_absolute;
        EXPECT_LE(error, allowance) << "voxel=" << i << " channel=" << c;
      }
    }
    const double error = std::abs(observed.back()[0] - reference.back()[0]);
    maximum_error = std::max(maximum_error, error);
    if (step == 1U) {
      opacity = reference.back()[0];
    }
    previous_observed = observed.back()[0];
    if (step == 64U) {
      last_half_error = error;
      last_half_bounds = bounds;
    }
    if (step == 65U) {
      EXPECT_GT(error,
        0.0); // FP32 storage does not erase reused history error.
      EXPECT_GT(bounds.rgb_absolute + bounds.rgb_relative, 0.0F);
      const auto qualification = Read<HdrSuitabilityData>(
        *probe->frame->suitability_buffer, ResourceStates::kShaderResource);
      EXPECT_NE(qualification.failure_flags & 4U, 0U);
      EXPECT_EQ(qualification.first_failure_product, 10U);
      EXPECT_EQ(storage.completed.fp16_eligible_streak, 0U);
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
      const auto qualification = Read<HdrSuitabilityData>(
        *probe->frame->suitability_buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(qualification.failure_flags, 0U);
      EXPECT_EQ(storage.completed.fp16_eligible_streak, 1U);
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

} // namespace oxygen::vortex::testing::exposure
