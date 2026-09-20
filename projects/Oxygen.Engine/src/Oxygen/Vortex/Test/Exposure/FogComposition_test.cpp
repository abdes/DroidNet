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
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereComposePass.h>
#include <Oxygen/Vortex/Environment/Passes/FogPass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Color;

using graphics::BufferMemory;
using graphics::BufferUsage;
using graphics::BufferViewDescription;
using graphics::DescriptorVisibility;
using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::ResourceViewType;
using graphics::Texture;
using graphics::TextureViewDescription;

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
  renderer_->RegisterConsoleBindings(observer_ptr {
    &console,
  });
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
  view.viewport = {
    .width = 4.0F,
    .height = 4.0F,
  };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  struct DomainOverride final : IViewExtension {
    observer_ptr<Renderer> renderer;
    explicit DomainOverride(Renderer& value)
      : renderer(&value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer);
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
  frame.SetScene(observer_ptr {
    scene.get(),
  });
  std::array<std::shared_ptr<Framebuffer>, 2> outputs;
  for (auto& output : outputs) {
    auto texture = CreateRegisteredTexture({
      .width = 4U,
      .height = 4U,
      .format = Format::kRGBA32Float,
      .is_shader_resource = true,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    output = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(texture));
  }
  Pixel initial {};
  for (unsigned sequence = 1U; sequence <= 2U; ++sequence) {
    fog.SetVolumetricFogEmissive(
      sequence == 1U ? Vec3 { .1F, .2F, .3F, } : Vec3 { 1.0F, 2.0F, 3.0F, });
    scene->Update();
    frame.SetFrameSequenceNumber(
      frame::SequenceNumber {
        sequence,
      },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(
      frame::Slot {
        sequence - 1U,
      },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr {
      &frame,
    });
    std::array<ViewId, 2> published {};
    for (unsigned index = 0U; index < 2U; ++index) {
      auto input = CompositionView::ForScene(
        ViewId {
          9100U + index,
        },
        view, camera);
      input.view_state_handle = CompositionView::ViewStateHandle {
        9100U + index,
      };
      input.with_height_fog = true;
      auto settings = scene::ExposureSettings {};
      settings.key = 12.5F;
      settings.mode = engine::ExposureMode::kManual;
      settings.manual_ev = sequence == 2U && index == 1U ? 8.0F : 4.0F;
      input.render_settings.exposure = settings;
      published.at(index) = renderer_->PublishRuntimeCompositionView(frame,
        { .composition_view = input,
          .render_target = observer_ptr { outputs.at(index).get(), }, });
      ASSERT_NE(published.at(index), kInvalidViewId);
    }
    auto loop = co::testing::TestEventLoop {};
    // Run waits for completion, so the closure and captured locals outlive the
    // coroutine.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr {
        &frame,
      });
      co_await renderer_->OnRender(observer_ptr {
        &frame,
      });
    });
    renderer_->OnFrameEnd(observer_ptr {
      &frame,
    });
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    std::array<Pixel, 2> samples {};
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto [texture, exposure]
        = vortex::testing::RendererPublicationProbe::FogHistory(
          *owner, published.at(index));
      ASSERT_NE(texture, nullptr);
      ASSERT_NE(exposure, nullptr);
      const auto certificate = Read<ExposureStatusStorage>(
        *exposure->current_state->status_buffer, ResourceStates::kCopySource);
      const auto& fog_error = certificate.producer_errors.at(2);
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
                  .depth = 1U, }, })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      if (!mapped.has_value()) {
        FAIL() << "Expected mapped to contain a value";
      }
      std::memcpy(samples.at(index).data(), mapped->Data(), sizeof(Pixel));
    }
    ASSERT_GT(samples.at(0).at(0), 1e-8F);
    const float ratio = sequence == 2U ? 1.0F / 16.0F : 1.0F;
    for (unsigned channel = 0U; channel < 3U; ++channel) {
      EXPECT_NEAR(samples.at(1).at(channel), samples.at(0).at(channel) * ratio,
        std::max(1e-7F, samples.at(0).at(channel) * ratio * 2e-4F));
    }
    EXPECT_NEAR(samples.at(1).at(3), samples.at(0).at(3), 2e-6F);
    if (sequence == 1U) {
      initial = samples.at(0);
    } else {
      EXPECT_GT(samples.at(0).at(0), initial.at(0) * 1.1F);
      EXPECT_LT(samples.at(0).at(0), initial.at(0) * 5.0F);
    }
  }
  renderer_->RegisterConsoleBindings({});
  FlushBackend();
}

NOLINT_TEST_F(
  ExposureGpuTest, HeightFogInputPeakUsesSceneUnitsAndRejectsNonfiniteInput)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto renderer_config = RendererConfig {};
  renderer_config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), renderer_config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  auto scene = scene::Scene("Height fog source range", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene.GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  ctx_.scene = observer_ptr {
    &scene,
  };
  ctx_.current_view.with_height_fog = true;
  auto textures = SceneTextures(Backend(),
    { .extent = { 8U, 8U, },
      .enable_velocity = false,
      .scene_color_format = Format::kRGBA32Float, });
  auto framebuffer = Backend().CreateFramebuffer(FramebufferDesc {}
      .AddColorAttachment(textures.GetSceneColorResource())
      .SetDepthAttachment({
        .texture = textures.GetSceneDepthResource(),
      }));
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto depth_handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto depth_slot = allocator.GetShaderVisibleIndex(depth_handle);
  Backend().GetResourceRegistry().RegisterView(textures.GetSceneDepth(),
    std::move(depth_handle),
    TextureViewDescription {
      .format = textures.GetSceneDepth().GetDescriptor().format,
      .dimension = TextureType::kTexture2D,
    });
  auto scene_bindings = SceneTextureBindings {};
  scene_bindings.scene_depth_srv = depth_slot.get();
  const auto scene_slot = PublishFixtureData(scene_bindings);
  const auto environment_view_slot = PublishFixtureData(EnvironmentViewData {});
  auto compose = environment::FogPass(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev
    = -2.0F; // P=4; the collected source peak must remain scene referred.
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const float scale : {
         .8F,
         .2F,
         std::numeric_limits<float>::quiet_NaN(),
       }) {
    SCOPED_TRACE(scale);
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = false;
    const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
    ASSERT_NE(frame, nullptr);
    ctx_.current_view.frame_exposure = frame;
    auto environment_static = EnvironmentStaticData {};
    environment_static.fog.flags = kGpuFogFlagEnabled
      | kGpuFogFlagRenderInMainPass | kGpuFogFlagHeightFogEnabled;
    environment_static.fog.primary_density = .1F;
    environment_static.fog.primary_height_falloff = 0.0F;
    environment_static.fog.fog_inscattering_luminance_rgb = {
      scale * .25F,
      scale * .5F,
      scale,
    };
    auto environment_bindings = EnvironmentFrameBindings {};
    environment_bindings.environment_static_slot
      = PublishFixtureData(environment_static);
    environment_bindings.environment_view_slot = environment_view_slot;
    auto view_bindings = ViewFrameBindings {};
    view_bindings.environment_frame_slot
      = PublishFixtureData(environment_bindings);
    view_bindings.scene_texture_frame_slot = scene_slot;
    view_bindings.frame_exposure_slot = frame->srv_index;
    view_bindings.exposure_status_uav = frame->current_state->status_uav_index;
    auto view = ViewConstants::GpuData {};
    view.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
      PublishFixtureData(view_bindings),
    };
    view.reverse_z = 0U;
    view.inverse_view_projection_matrix = glm::mat4 {
      0.0F,
    };
    view.inverse_view_projection_matrix
      = glm::column(view.inverse_view_projection_matrix, 3,
        glm::vec4 {
          0,
          0,
          -1,
          1,
        });
    auto constants = CreateUploadBuffer(
      SizeBytes {
        256U,
      },
      BufferUsage::kConstant);
    constants->Update(&view, sizeof(view), 0U);
    ctx_.view_constants = constants;
    {
      auto recorder = AcquireRecorder("Height fog source initialization");
      for (const auto& texture : {
             textures.GetSceneColorResource(),
             textures.GetSceneDepthResource(),
           }) {
        if (!recorder->AdoptKnownResourceState(*texture)) {
          recorder->BeginTrackingResourceState(
            *texture, texture->GetDescriptor().initial_state);
        }
      }
      recorder->RequireResourceState(
        textures.GetSceneColor(), ResourceStates::kRenderTarget);
      recorder->RequireResourceState(
        textures.GetSceneDepth(), ResourceStates::kDepthWrite);
      recorder->FlushBarriers();
      recorder->ClearFramebuffer(*framebuffer,
        std::vector<std::optional<Color>> {
          Color {},
        },
        0.0F);
    }
    ASSERT_TRUE(compose.Record(ctx_, textures).executed);
    const auto pixels = ReadFloatTexture(textures.GetSceneColor());
    const auto input = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess)
                         .consumer_inputs;
    EXPECT_EQ(input.translucent_rgb_max, 0.0F);
    EXPECT_EQ(input.sky_rgb_gain_max, 0.0F);
    if (std::isfinite(scale)) {
      EXPECT_EQ(input.flags, 2U);
      const double expected = static_cast<double>(scale)
        * (1.0 - std::exp2(-std::numbers::ln2 * static_cast<double>(.1F)));
      EXPECT_NEAR(input.height_fog_rgb_max, expected, 2e-6);
      for (const auto& pixel : pixels) {
        EXPECT_EQ(input.height_fog_rgb_max, pixel.at(2) / 4.0F);
      }
    } else {
      EXPECT_EQ(input.flags, 10U);
      EXPECT_EQ(input.height_fog_rgb_max, 0.0F);
    }
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  ctx_.view_constants.reset();
  ctx_.current_view.frame_exposure.reset();
  ctx_.scene.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, FogCompositionClampsViewportAndDepthEdges)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  auto scene = scene::Scene("Fog composition edges", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene.GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableVolumetricFog(true);
  ctx_.scene = observer_ptr {
    &scene,
  };
  ctx_.current_view.with_height_fog = true;
  auto textures = SceneTextures(Backend(),
    { .extent = { 8U, 8U, },
      .enable_velocity = false,
      .scene_color_format = Format::kRGBA32Float, });
  auto framebuffer = Backend().CreateFramebuffer(FramebufferDesc {}
      .AddColorAttachment(textures.GetSceneColorResource())
      .SetDepthAttachment({
        .texture = textures.GetSceneDepthResource(),
      }));
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto depth_handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto depth_slot = allocator.GetShaderVisibleIndex(depth_handle);
  Backend().GetResourceRegistry().RegisterView(textures.GetSceneDepth(),
    std::move(depth_handle),
    TextureViewDescription {
      .format = textures.GetSceneDepth().GetDescriptor().format,
      .dimension = TextureType::kTexture2D,
    });
  auto scene_bindings = SceneTextureBindings {};
  scene_bindings.scene_depth_srv = depth_slot.get();
  const auto scene_slot = PublishFixtureData(scene_bindings);
  const auto environment_view_slot = PublishFixtureData(EnvironmentViewData {});
  auto compose = environment::FogPass(*renderer_);
  std::array<Pixel, 8> pixels {};
  for (unsigned z = 0; z < 2; ++z) {
    for (unsigned y = 0; y < 2; ++y) {
      for (unsigned x = 0; x < 2; ++x) {
        pixels.at((((z * 2) + y) * 2) + x) = Pixel {
          static_cast<float>(x),
          static_cast<float>(y),
          static_cast<float>(z),
          1.0F - static_cast<float>(z),
        };
      }
    }
  }
  const auto capture = BeginOptionalCapture();
  for (const auto format : {
         Format::kRGBA32Float,
         Format::kRGBA16Float,
       }) {
    const auto volume = MakeSignal(2, 2, pixels, 2, format, true);
    auto environment_static = EnvironmentStaticData {};
    environment_static.fog.flags
      = kGpuFogFlagEnabled | kGpuFogFlagRenderInMainPass;
    environment_static.volumetric_fog.flags = kGpuVolumetricFogFlagEnabled
      | kGpuVolumetricFogFlagIntegratedScatteringValid;
    environment_static.volumetric_fog.integrated_light_scattering_srv
      = volume.srv.get();
    environment_static.volumetric_fog.distance_m = 1.0F;
    environment_static.volumetric_fog.grid_depth = 2U;
    auto environment_bindings = EnvironmentFrameBindings {};
    environment_bindings.environment_static_slot
      = PublishFixtureData(environment_static);
    environment_bindings.environment_view_slot = environment_view_slot;
    auto view_bindings = ViewFrameBindings {};
    view_bindings.environment_frame_slot
      = PublishFixtureData(environment_bindings);
    view_bindings.scene_texture_frame_slot = scene_slot;
    const auto view_slot = PublishFixtureData(view_bindings);
    for (const auto distance : {
           0.0F,
           .25F,
           1.0F,
           4.0F,
         }) {
      SCOPED_TRACE(distance);
      auto view = ViewConstants::GpuData {};
      view.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
        view_slot,
      };
      view.reverse_z = 0U;
      view.inverse_view_projection_matrix = glm::mat4 {
        0.0F,
      };
      view.inverse_view_projection_matrix
        = glm::column(view.inverse_view_projection_matrix, 3,
          glm::vec4 {
            0,
            0,
            -distance,
            1,
          });
      auto constants = CreateUploadBuffer(
        SizeBytes {
          256U,
        },
        BufferUsage::kConstant);
      constants->Update(&view, sizeof(view), 0U);
      ctx_.view_constants = constants;
      {
        auto recorder = AcquireRecorder("Fog edge initialization");
        for (const auto& texture : {
               textures.GetSceneColorResource(),
               textures.GetSceneDepthResource(),
             }) {
          if (!recorder->AdoptKnownResourceState(*texture)) {
            recorder->BeginTrackingResourceState(
              *texture, texture->GetDescriptor().initial_state);
          }
        }
        recorder->RequireResourceState(
          textures.GetSceneColor(), ResourceStates::kRenderTarget);
        recorder->RequireResourceState(
          textures.GetSceneDepth(), ResourceStates::kDepthWrite);
        recorder->FlushBarriers();
        recorder->ClearFramebuffer(*framebuffer,
          std::vector<std::optional<Color>> {
            Color {},
          },
          0.0F);
      }
      ASSERT_TRUE(compose.Record(ctx_, textures).executed);
      const auto result = ReadFloatTexture(textures.GetSceneColor());
      const double z = std::clamp(
        (2 * std::sqrt(std::clamp(static_cast<double>(distance), 0.0, 1.0)))
          - .5,
        0.0, 1.0);
      for (unsigned y = 0; y < 8; ++y) {
        for (unsigned x = 0; x < 8; ++x) {
          const auto& pixel = result.at((y * 8) + x);
          EXPECT_NEAR(pixel.at(0),
            std::clamp(((static_cast<double>(x) + .5) / 4) - .5, 0.0, 1.0),
            3e-7);
          EXPECT_NEAR(pixel.at(1),
            std::clamp(((static_cast<double>(y) + .5) / 4) - .5, 0.0, 1.0),
            3e-7);
          EXPECT_NEAR(pixel.at(2), z, 3e-7);
          EXPECT_NEAR(pixel.at(3), z, 3e-7);
        }
      }
    }
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  ctx_.view_constants.reset();
  ctx_.scene.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, DeferredApPreservesInscatterAtLowAndZeroOpacity)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  auto scene = scene::Scene("ApBlend", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene.GetEnvironment()
    ->AddSystem<scene::environment::SkyAtmosphere>()
    .SetEnabled(true);
  ctx_.scene = observer_ptr {
    &scene,
  };
  ctx_.current_view.with_atmosphere = true;
  auto textures = SceneTextures(Backend(),
    { .extent = { 4U, 4U, },
      .enable_velocity = false,
      .scene_color_format = Format::kRGBA32Float, });
  auto framebuffer
    = Backend().CreateFramebuffer(FramebufferDesc {}.SetDepthAttachment({
      .texture = textures.GetSceneDepthResource(),
    }));
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto& registry = Backend().GetResourceRegistry();
  const auto texture_srv
    = [&](const Texture& texture, Format format,
        TextureType dimension) -> bindless::ShaderVisibleIndex {
    auto handle = allocator.AllocateRaw(
      ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const auto index = allocator.GetShaderVisibleIndex(handle);
    registry.RegisterView(texture, std::move(handle),
      TextureViewDescription {
        .format = format,
        .dimension = dimension,
      });
    return index;
  };
  const auto publish = [&]<typename T>(const T& value) -> auto {
    auto buffer = CreateRegisteredBuffer({
      .size_bytes = sizeof(T),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = "AP blend fixture bindings",
    });
    buffer->Update(&value, sizeof(T), 0U);
    auto handle = allocator.AllocateBindless(
      oxygen::bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
    const auto index = allocator.GetShaderVisibleIndex(handle);
    registry.RegisterView(*buffer, std::move(handle),
      BufferViewDescription {
        .view_type = ResourceViewType::kStructuredBuffer_SRV,
        .range = { 0U, sizeof(T), },
        .stride = sizeof(T), });
    return index;
  };
  auto scene_bindings = SceneTextureBindings {};
  scene_bindings.scene_depth_srv = texture_srv(textures.GetSceneDepth(),
    textures.GetSceneDepth().GetDescriptor().format, TextureType::kTexture2D)
                                     .get();
  const auto scene_slot = publish(scene_bindings);
  auto environment_view = EnvironmentViewData {};
  environment_view.sky_aerial_luminance_aerial_start_depth_km.w = 0.0F;
  environment_view.camera_aerial_volume_depth_params = {
    1,
    1,
    1,
    10000,
  };
  const auto environment_view_slot = publish(environment_view);
  auto compose = environment::AtmosphereComposePass(*renderer_);
  const auto capture = BeginOptionalCapture();
  // 1e-5 is not a representable value of 1-T near T=1 in binary32.
  // Exercise the adjacent representable opacities on each side, plus zero,
  // the reported FP32-loss case, an ordinary opacity and full attenuation.
  const std::array transmittances {
    1.0F,
    1.0F - 0x1p-17F,
    1.0F - (167.0F * 0x1p-24F),
    1.0F - (168.0F * 0x1p-24F),
    .5F,
    0.0F,
  };
  for (const auto format : {
         Format::kRGBA32Float,
         Format::kRGBA16Float,
       }) {
    SCOPED_TRACE(static_cast<unsigned>(format));
    for (const auto transmittance : transmittances) {
      for (const float background : {
             0.0F,
             .5F,
           }) {
        for (const float coverage : {
               0.0F,
               .25F,
               1.0F,
             }) {
          SCOPED_TRACE(transmittance);
          SCOPED_TRACE(background);
          SCOPED_TRACE(coverage);
          auto volume = CreateRegisteredTexture({
            .width = 1U,
            .height = 1U,
            .depth = 1U,
            .format = format,
            .texture_type = TextureType::kTexture3D,
            .is_shader_resource = true,
            .initial_state = ResourceStates::kCommon,
          });
          Pixel sample {
            .01F,
            .02F,
            .04F,
            transmittance,
          };
          std::array<std::byte, 1536U> bytes {};
          if (format == Format::kRGBA16Float) {
            // Independently specified binary16 payload and exact decoded
            // values. All four near-one T cases round to 1; .5 and 0 are exact.
            std::uint16_t transmittance_bits = 0U;
            if (transmittance > .5F) {
              transmittance_bits = 0x3c00U;
            } else if (transmittance == .5F) {
              transmittance_bits = 0x3800U;
            }
            const std::array<std::uint16_t, 4> bits {
              0x211fU,
              0x251fU,
              0x291fU,
              transmittance_bits,
            };
            std::memcpy(bytes.data(), bits.data(), sizeof(bits));
            sample = {
              1311.0F / 131072.0F,
              1311.0F / 65536.0F,
              1311.0F / 32768.0F,
              transmittance > .5F ? 1.0F : transmittance,
            };
          } else {
            std::memcpy(bytes.data(), sample.data(), sizeof(sample));
          }
          const Pixel destination {
            background,
            background,
            background,
            coverage,
          };
          for (unsigned y = 0; y < 4U; ++y) {
            for (unsigned x = 0; x < 4U; ++x) {
              std::memcpy(
                std::span {
                  bytes,
                }
                  .subspan(512U + (static_cast<std::size_t>(y) * 256U)
                      + (x * sizeof(Pixel)),
                    sizeof(Pixel))
                  .data(),
                destination.data(), sizeof(destination));
            }
          }
          auto upload = CreateUploadBuffer(SizeBytes {
            bytes.size(),
          });
          upload->Update(bytes.data(), bytes.size(), 0U);
          {
            auto recorder = AcquireRecorder("AP blend fixture initialization");
            EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
            EnsureTracked(*recorder, volume, ResourceStates::kCommon);
            recorder->RequireResourceState(*volume, ResourceStates::kCopyDest);
            for (const auto& texture : {
                   textures.GetSceneColorResource(),
                   textures.GetSceneDepthResource(),
                 }) {
              if (!recorder->AdoptKnownResourceState(*texture)) {
                recorder->BeginTrackingResourceState(
                  *texture, texture->GetDescriptor().initial_state);
              }
            }
            recorder->RequireResourceState(
              textures.GetSceneColor(), ResourceStates::kCopyDest);
            recorder->RequireResourceState(
              textures.GetSceneDepth(), ResourceStates::kDepthWrite);
            recorder->FlushBarriers();
            recorder->CopyBufferToTexture(*upload,
              { .buffer_row_pitch = 256U,
                .buffer_slice_pitch = 256U,
                .dst_slice = { .width = 1U, .height = 1U, .depth = 1U, }, },
              *volume);
            recorder->CopyBufferToTexture(*upload,
              { .buffer_offset = 512U,
                .buffer_row_pitch = 256U,
                .buffer_slice_pitch = 1024U,
                .dst_slice = { .width = 4U, .height = 4U, .depth = 1U, }, },
              textures.GetSceneColor());
            recorder->ClearFramebuffer(*framebuffer, std::nullopt, 0.0F);
            recorder->RequireResourceStateFinal(
              *volume, ResourceStates::kShaderResource);
          }
          auto environment_static = EnvironmentStaticData {};
          environment_static.atmosphere.camera_volume_lut_slot
            = texture_srv(*volume, format, TextureType::kTexture3D).get();
          auto environment_bindings = EnvironmentFrameBindings {};
          environment_bindings.environment_static_slot
            = publish(environment_static);
          environment_bindings.environment_view_slot = environment_view_slot;
          auto view_bindings = ViewFrameBindings {};
          view_bindings.environment_frame_slot = publish(environment_bindings);
          view_bindings.scene_texture_frame_slot = scene_slot;
          auto view = ViewConstants::GpuData {};
          view.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
            publish(view_bindings),
          };
          view.reverse_z = 0U;
          auto constants = CreateUploadBuffer(
            SizeBytes {
              256U,
            },
            BufferUsage::kConstant);
          constants->Update(&view, sizeof(view), 0U);
          ctx_.view_constants = constants;
          ASSERT_TRUE(compose.Record(ctx_, textures).executed);
          auto readback
            = GetReadbackManager()->CreateTextureReadback("AP composed pixel");
          {
            auto recorder = AcquireRecorder("AP composed pixel readback");
            ASSERT_TRUE(
              recorder->AdoptKnownResourceState(textures.GetSceneColor()));
            ASSERT_TRUE(readback
                ->EnqueueCopy(*recorder, textures.GetSceneColor(),
                  { .src_slice = { .x = 1U,
                      .y = 1U,
                      .width = 1U,
                      .height = 1U,
                      .depth = 1U, }, })
                .has_value());
          }
          const auto mapped = readback->MapNow();
          if (!mapped.has_value()) {
            FAIL() << "Expected mapped to contain a value";
          }
          Pixel result {};
          std::memcpy(result.data(), mapped->Data(), sizeof(result));
          // Independent double-precision transfer also describes forward AP.
          // 3e-7 bounds FP32 sample/arithmetic/blend rounding for these unit
          // inputs.
          for (unsigned c = 0U; c < 3U; ++c) {
            EXPECT_NEAR(result.at(c),
              static_cast<double>(sample.at(c))
                + (static_cast<double>(background) * sample.at(3)),
              3e-7);
          }
          EXPECT_NEAR(result.at(3),
            1.0 - static_cast<double>(sample.at(3))
              + (static_cast<double>(coverage) * sample.at(3)),
            3e-7);
        }
      }
    }
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  ctx_.view_constants.reset();
  ctx_.scene.reset();
  WaitForQueueIdle();
}

} // namespace oxygen::vortex::testing::exposure
