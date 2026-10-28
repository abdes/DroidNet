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
#include <numbers>
// MSVC declares the platform-specific _dupenv_s in the C runtime header.
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <stdlib.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/SubmissionCallback.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>
#include <Oxygen/Vortex/Environment/Passes/FogPass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Types/Generated.ExposureContracts.h>
#include <Oxygen/Vortex/Types/Generated.ViewContracts.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewHistoryFrameBindings.h>
#include <Oxygen/Vortex/ViewExtension.h>
#include <Oxygen/Vortex/test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {

using graphics::BufferUsage;
using graphics::ComputePipelineDesc;
using graphics::DescriptorVisibility;
using graphics::FramebufferDesc;
using graphics::FrameCaptureController;
using graphics::ResourceStates;
using graphics::ResourceViewType;
using graphics::ShaderRequest;
using graphics::Texture;
using graphics::TextureViewDescription;

NOLINT_TEST_F(
  ExposureGpuTest, FogHistoryClampsEdgesAndRejectsOutsideCoordinates)
{
  namespace root = oxygen::bindless::generated::d3d12;
  const auto bindings = ExposureProbeRootBindings();
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest {
          .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS",
        })
        .SetRootBindings(bindings)
        .SetDebugName("Fog history edge fixture")
        .Build();
  auto output = CreateRegisteredTexture({
    .width = 1U,
    .height = 1U,
    .depth = 2U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*output, std::move(handle),
    TextureViewDescription {
      .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D,
    });
  std::array<Pixel, 8> samples {};
  for (unsigned z = 0; z < 2; ++z) {
    for (unsigned y = 0; y < 2; ++y) {
      for (unsigned x = 0; x < 2; ++x) {
        samples.at((((z * 2) + y) * 2) + x) = Pixel {
          static_cast<float>(x),
          static_cast<float>(y),
          static_cast<float>(z),
          1.0F - static_cast<float>(z),
        };
      }
    }
  }
  const std::array coordinates {
    glm::vec2 {
      0,
      .5F,
    },
    glm::vec2 {
      1,
      .5F,
    },
    glm::vec2 {
      .5F,
      0,
    },
    glm::vec2 {
      .5F,
      1,
    },
    glm::vec2 {
      .0625F,
      .5F,
    },
    glm::vec2 {
      .9375F,
      .5F,
    },
    glm::vec2 {
      .5F,
      .0625F,
    },
    glm::vec2 {
      .5F,
      .9375F,
    },
    glm::vec2 {
      .25F,
      .25F,
    },
    glm::vec2 {
      .5F,
      .5F,
    },
    glm::vec2 {
      .75F,
      .75F,
    },
    glm::vec2 {
      -.01F,
      .5F,
    },
    glm::vec2 {
      1.01F,
      .5F,
    },
    glm::vec2 {
      .5F,
      -.01F,
    },
    glm::vec2 {
      .5F,
      1.01F,
    },
  };
  const auto capture = BeginOptionalCapture();
  for (const auto format : {
         Format::kRGBA32Float,
         Format::kRGBA16Float,
       }) {
    const auto volume = MakeSignal(2, 2, samples, 2, format, true);
    auto params
      = vortex::testing::RendererPublicationProbe::FogPassConstants {};
    params.output_header = {
      .output_texture_uav = output_slot.get(),
      .output_width = 1U,
      .output_height = 1U,
      .output_depth = 2U,
    };
    params.grid.end_distance_m = 10.0F;
    params.grid_z.grid_z_params.at(0) = 1.0F;
    params.grid_z.grid_z_params.at(1) = 0.0F;
    params.grid_z.grid_z_params.at(2) = 1.0F;
    params.temporal_history0 = {
      .previous_light_scattering_srv = volume.srv.get(),
      .enabled = 1U,
      .history_weight = 1.0F,
      .history_miss_supersample_count = 1U,
    };
    const auto pass_slot = PublishFixtureData(params);
    for (const auto uv : coordinates) {
      for (const float depth : {
             1.0F,
             std::numbers::sqrt2_v<float>,
             3.0F,
             .5F,
             8.0F,
           }) {
        SCOPED_TRACE(uv.x);
        SCOPED_TRACE(uv.y);
        SCOPED_TRACE(depth);
        auto history = ViewHistoryFrameBindings {};
        history.validity_flags = static_cast<std::uint32_t>(
          ViewHistoryValidityFlagBits::kPreviousViewValid);
        auto previous_translation
          = glm::column(history.previous_view_matrix, 3);
        previous_translation.z = -depth;
        history.previous_view_matrix
          = glm::column(history.previous_view_matrix, 3, previous_translation);
        history.previous_projection_matrix = glm::mat4 {
          0.0F,
        };
        history.previous_projection_matrix
          = glm::column(history.previous_projection_matrix, 3,
            glm::vec4 {
              (2 * uv.x) - 1,
              1 - (2 * uv.y),
              0,
              1,
            });
        auto frame_bindings = ViewFrameBindings {};
        frame_bindings.history_frame_slot = PublishFixtureData(history);
        frame_bindings.frame_exposure_slot = PublishUnitFrameExposure();
        auto view = ViewConstants::GpuData {};
        view.bindless_view_frame_bindings_slot = BindlessViewFrameBindingsSlot {
          PublishFixtureData(frame_bindings),
        };
        view.inverse_view_projection_matrix = glm::mat4 {
          0.0F,
        };
        view.inverse_view_projection_matrix
          = glm::column(view.inverse_view_projection_matrix, 3,
            glm::vec4 {
              0,
              0,
              0,
              1,
            });
        auto view_buffer = CreateUploadBuffer(
          SizeBytes {
            256U,
          },
          BufferUsage::kConstant);
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
        const double w
          = std::clamp(std::log2(static_cast<double>(depth)) / 2, 0.0, 1.0);
        const double z = std::clamp((2 * w) - .5, 0.0, 1.0);
        const std::array expected = rejected
          ? std::array<double, 4> { 0, 0, 0, 1 }
          : std::array<double, 4> {
              std::clamp(2 * static_cast<double>(uv.x) - .5, 0.0, 1.0),
              std::clamp(2 * static_cast<double>(uv.y) - .5, 0.0, 1.0),
              z,
              1 - z,
            };
        for (const auto& actual : ReadFloatTexture(*output)) {
          for (unsigned c = 0; c < 4; ++c) {
            EXPECT_NEAR(actual.at(c), expected.at(c), 3e-6);
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
        .SetComputeShader(ShaderRequest {
          .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS",
        })
        .SetRootBindings(ExposureProbeRootBindings())
        .SetDebugName("Fog stable cell fixture")
        .Build();
  auto output = CreateRegisteredTexture({
    .width = 4U,
    .height = 4U,
    .depth = 4U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*output, std::move(handle),
    TextureViewDescription {
      .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D,
    });
  std::array<Pixel, 64> samples {};
  for (unsigned z = 0; z < 4; ++z) {
    for (unsigned y = 0; y < 4; ++y) {
      for (unsigned x = 0; x < 4; ++x) {
        samples.at((((z * 4) + y) * 4) + x) = {
          static_cast<float>(x) / 4,
          static_cast<float>(y) / 4,
          static_cast<float>(z) / 4,
          1 - (static_cast<float>(z) / 4),
        };
      }
    }
  }
  auto history = ViewHistoryFrameBindings {};
  history.validity_flags = static_cast<std::uint32_t>(
    ViewHistoryValidityFlagBits::kPreviousViewValid);
  auto previous_projection_depth
    = glm::column(history.previous_projection_matrix, 2);
  previous_projection_depth.z = -1.0F / 32;
  history.previous_projection_matrix = glm::column(
    history.previous_projection_matrix, 2, previous_projection_depth);
  auto frame_bindings = ViewFrameBindings {};
  frame_bindings.history_frame_slot = PublishFixtureData(history);
  frame_bindings.frame_exposure_slot = PublishUnitFrameExposure();
  auto view = ViewConstants::GpuData {};
  view.bindless_view_frame_bindings_slot = BindlessViewFrameBindingsSlot {
    PublishFixtureData(frame_bindings),
  };
  view.projection_matrix = history.previous_projection_matrix;
  view.inverse_view_projection_matrix = glm::mat4 {
    1.0F,
  };
  auto inverse_projection_depth
    = glm::column(view.inverse_view_projection_matrix, 2);
  inverse_projection_depth.z = -32.0F;
  view.inverse_view_projection_matrix = glm::column(
    view.inverse_view_projection_matrix, 2, inverse_projection_depth);
  auto view_buffer = CreateUploadBuffer(
    SizeBytes {
      256U,
    },
    BufferUsage::kConstant);
  view_buffer->Update(&view, sizeof(view), 0U);
  const std::array offsets {
    glm::vec4 {
      .5F,
      .5F,
      .5F,
      0,
    },
    glm::vec4 {
      .125F,
      .375F,
      .75F,
      0,
    },
    glm::vec4 {
      .875F,
      .625F,
      .25F,
      0,
    },
  };
  const auto capture = BeginOptionalCapture();
  for (const auto format : {
         Format::kRGBA32Float,
         Format::kRGBA16Float,
       }) {
    const auto volume = MakeSignal(4, 4, samples, 4, format, true);
    for (const bool reuse_history : {
           false,
           true,
         }) {
      for (const float density : {
             .06F,
             0x1p-40F,
           }) {
        for (const float fade_distance : {
               0.0F,
               2.0F,
             }) {
          for (const auto offset : offsets) {
            SCOPED_TRACE(reuse_history);
            SCOPED_TRACE(fade_distance);
            SCOPED_TRACE(offset.z);
            SCOPED_TRACE(density);
            const float emission_scale = density < .001F ? 1e9F : 1.0F;
            auto params
              = vortex::testing::RendererPublicationProbe::FogPassConstants {};
            params.output_header = {
              .output_texture_uav = output_slot.get(),
              .output_width = 4U,
              .output_height = 4U,
              .output_depth = 4U,
            };
            params.grid = {
              .start_distance_m = 1.0F,
              .end_distance_m = 32.0F,
              .near_fade_in_distance_m = fade_distance,
              .global_extinction_scale = 1.0F,
            };
            params.grid_z = {
              .grid_z_params = { 1.0F, 0.0F, 1.0F },
              .directional_shadows_enabled = 0U,
            };
            params.height_fog0.primary_density = density;
            params.height_fog1.match_height_fog_factor = 1.0F;
            params.height_fog1.enabled = 1U;
            params.media1 = {
              .emissive_rgb
              = { 2 * emission_scale, 3 * emission_scale, 4 * emission_scale },
              .static_lighting_scattering_intensity = 1.0F,
            };
            const unsigned history_flags = reuse_history ? 1U : 0U;
            params.temporal_history0 = {
              .previous_light_scattering_srv = volume.srv.get(),
              .enabled = history_flags,
              .history_weight = 1.0F,
              .history_miss_supersample_count = 1U,
            };
            std::ranges::copy(
              std::array {
                offset.x,
                offset.y,
                offset.z,
                offset.w,
              },
              params.temporal_history1.frame_jitter_offsets.front().begin());
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
            const auto emissive = params.media1.emissive_rgb;
            for (unsigned i = 0; i < samples.size(); ++i) {
              // Independent oracle: the slice's own Beer-Lambert opacity over
              // its ray segment [2^s, 2^(s+1)], faded by the distance of its
              // centre past the grid start.
              const auto slice = i / 16;
              const double step = std::exp2(static_cast<double>(slice));
              const double centre
                = std::exp2(static_cast<double>(slice) + .5) - 1;
              const double fade = fade_distance > 0
                ? std::min(centre / static_cast<double>(fade_distance), 1.0)
                : 1.0;
              const double opacity
                = -std::expm1(-static_cast<double>(density) * step) * fade;
              for (unsigned c = 0; c < 4; ++c) {
                double expected = 1.0 - opacity;
                if (reuse_history) {
                  expected = static_cast<double>(samples.at(i).at(c));
                } else if (c != 3) {
                  expected = static_cast<double>(emissive.at(c)) * opacity;
                }
                const double tolerance = reuse_history
                  ? 3e-6
                  : std::min(3e-6, (std::abs(expected) * 2e-5) + 0x1p-120);
                EXPECT_NEAR(actual.at(i).at(c), expected, tolerance)
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

//! The integrated product may narrow to FP16 and its error bounds contain the
//! narrowing; the light-scattering history is FP32, so returning to FP32
//! storage carries no error from the half-precision frames.
NOLINT_TEST_F(
  ExposureGpuTest, FogErrorBoundsContainHalfStoreAndHistoryCarriesNoError)
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
  renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
    HdrPrecisionControl::kQualified);
  oxygen::commands::CommandRegistry command_registry;
  console::Console console { oxygen::observer_ptr { &command_registry } };
  renderer_->RegisterConsoleBindings(observer_ptr {
    &console,
  });
  ASSERT_EQ(
    console.Execute("vtx.volumetric_fog.temporal_reprojection true").status,
    commands::ExecutionStatus::kOk);
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    commands::ExecutionStatus::kOk);
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
  view.viewport = {
    .width = 4.0F,
    .height = 4.0F,
  };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  camera.GetTransform().SetLocalRotation(transforms::LookRotation(
    { .forward = space::move::Down, .up = space::move::Back }));
  auto color = CreateRegisteredTexture({
    .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto target
    = Backend().CreateFramebuffer(FramebufferDesc {}.AddColorAttachment(color));
  // Two producers see the same scene and frame: the observed one stores the
  // product in FP16 or FP32 as the step requires, the reference one always
  // in FP32. Each keeps its own FP32 light-scattering history.
  struct Probe final : IViewExtension {
    observer_ptr<Renderer> renderer;
    EnvironmentLightingService producer;
    EnvironmentLightingService reference_producer;
    bool use_half {
      true,
    };
    bool history_used { false };
    bool discontinuity { false };
    postprocess::ExposurePass::FrameLease frame;
    std::shared_ptr<const Texture> observed;
    std::shared_ptr<const Texture> reference;
    explicit Probe(Renderer& value)
      : renderer(&value)
      , producer(value)
      , reference_producer(value)
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
      std::ignore = producer.PublishEnvironmentBindings(ctx, hook.recorder);
      if (ctx.frame_sequence.get() == 1U) {
        std::ignore = producer.PublishEnvironmentBindings(ctx, hook.recorder);
        EXPECT_FALSE(producer.GetLastViewProductGenerationState()
            .volumetric_fog_temporal_history_reprojection_executed);
      }
      const auto* resources
        = producer.InspectViewRadianceResources(ctx.current_view.view_id);
      CHECK_NOTNULL_F(resources);
      observed = resources->volumetric_fog;
      history_used = producer.GetLastViewProductGenerationState()
                       .volumetric_fog_temporal_history_reprojection_executed;
      discontinuity = ctx.current_view.history_discontinuity;

      ctx.current_view.hdr_color_format = Format::kRGBA32Float;
      reference_producer.OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
      std::ignore
        = reference_producer.PublishEnvironmentBindings(ctx, hook.recorder);
      const auto* reference_resources
        = reference_producer.InspectViewRadianceResources(
          ctx.current_view.view_id);
      CHECK_NOTNULL_F(reference_resources);
      reference = reference_resources->volumetric_fog;
      ctx.current_view.hdr_color_format = old_format;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      hook.recorder.OnSubmission(
        [this, exposure = hook.render_context.current_view.frame_exposure](
          const graphics::SubmissionOutcome outcome) -> void {
          if (outcome == graphics::SubmissionOutcome::kSubmitted) {
            frame = exposure;
          }
        });
    }
  };
  auto probe = std::make_shared<Probe>(*renderer_);
  renderer_->RegisterViewExtension(probe);
  const auto half_to_double = [](const std::uint16_t half) -> double {
    const auto bits = static_cast<std::uint32_t>(half);
    const auto exponent = (bits >> 10U) & 31U;
    const auto mantissa = bits & 1023U;
    const double magnitude = exponent == 0U
      ? std::ldexp(static_cast<double>(mantissa), -24)
      : std::ldexp(static_cast<double>(1024U + mantissa),
          static_cast<int>(exponent) - 25);
    return (bits & 0x8000U) != 0U ? -magnitude : magnitude;
  };
  const auto read_volume
    = [&](const Texture& texture) -> std::vector<std::array<double, 4>> {
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
      static_cast<std::size_t>(desc.width) * desc.height * desc.depth);
    const bool half = desc.format == Format::kRGBA16Float;
    const auto texel_bytes = half ? 8U : 16U;
    const auto mapped_bytes = MappedTextureBytes(*mapped, texel_bytes);
    for (unsigned z = 0U; z < desc.depth; ++z) {
      for (unsigned y = 0U; y < desc.height; ++y) {
        for (unsigned x = 0U; x < desc.width; ++x) {
          const auto bytes
            = mapped_bytes.subspan((z * mapped->Layout().slice_pitch.get())
                + (y * mapped->Layout().row_pitch.get())
                + (static_cast<std::size_t>(x) * texel_bytes),
              texel_bytes);
          auto& value = values.at((((z * desc.height) + y) * desc.width) + x);
          if (half) {
            std::array<std::uint16_t, 4> packed {};
            std::memcpy(packed.data(), bytes.data(), sizeof(packed));
            std::ranges::transform(packed, value.begin(), half_to_double);
          } else {
            Pixel unpacked {};
            std::memcpy(unpacked.data(), bytes.data(), sizeof(unpacked));
            std::ranges::copy(unpacked, value.begin());
          }
        }
      }
    }
    return values;
  };
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr {
    scene.get(),
  });
  double opacity = 1.0;
  double maximum_error = 0.0;
  double last_half_error = 0.0;
  HdrErrorBoundsData last_half_bounds;
  unsigned capture_step = 64U;
  char* capture_step_text = nullptr;
  std::size_t capture_step_size = 0U;
  if (_dupenv_s(&capture_step_text, &capture_step_size,
        "OXYGEN_EXPOSURE_FOG_CAPTURE_STEP")
      == 0
    && (capture_step_text != nullptr)) {
    const auto owned_capture_step
      = std::unique_ptr<char, decltype(&std::free)> {
          capture_step_text,
          &std::free,
        };
    capture_step = static_cast<unsigned>(
      std::strtoul(owned_capture_step.get(), nullptr, 10));
    ASSERT_GE(capture_step, 1U);
    ASSERT_LE(capture_step, 74U);
  }
  for (unsigned step = 1U; step <= 74U; ++step) {
    SCOPED_TRACE(step);
    // Values near 1 that FP16 cannot represent, varying so history and the
    // fresh sample differ.
    double fresh
      = 1.0 + (static_cast<double>(step % 7U) / 4096.0) + (1.0 / 4194304.0);
    if (step == 1U) {
      fresh = 1.0;
    } else if (step == 73U) {
      fresh = std::numeric_limits<double>::infinity();
    } else if (step == 74U) {
      fresh = .25;
    }
    const float emissive = static_cast<float>(std::max(fresh, 0.0) / opacity);
    fog.SetVolumetricFogEmissive({
      emissive,
      emissive,
      emissive,
    });
    probe->use_half = step <= 64U;
    scene->Update();
    const auto slot = frame::Slot {
      (step - 1U) % 3U,
    };
    Backend().BeginFrame(
      frame::SequenceNumber {
        step,
      },
      slot);
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(
      frame::SequenceNumber {
        step,
      },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr {
      &frame,
    });
    auto input = Renderer::OffscreenSceneViewInput::FromCamera("Fog bounds",
      ViewId {
        942U,
      },
      view, camera);
    input.SetViewStateHandle(CompositionView::ViewStateHandle {
      942U,
    });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = slot,
      .frame_sequence = frame::SequenceNumber { step, },
      .delta_time_seconds = 0.0F, });
    facade.SetSceneSource({ .scene = observer_ptr {
                              scene.get(),
                            }, });
    facade.SetViewIntent(input);
    facade.SetOutputTarget({ .framebuffer = observer_ptr {
                               target.get(),
                             }, });
    auto session = facade.Finalize();
    if (!session.has_value()) {
      FAIL() << "Expected session to contain a value";
    }
    observer_ptr<FrameCaptureController> capture;
    if (step == capture_step) {
      capture = BeginOptionalCapture();
    }
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
    const auto& bounds = storage.producer_errors.at(2);
    if (step == 73U) {
      // A non-finite input is a producer failure and leaves the product
      // without a finite error bound.
      EXPECT_TRUE(std::isinf(bounds.rgb_absolute));
      EXPECT_NE(storage.completed.flags & 16U, 0U);
      renderer_->OnFrameEnd(observer_ptr {
        &frame,
      });
      Backend().EndFrame(
        frame::SequenceNumber {
          step,
        },
        slot);
      WaitForQueueIdle();
      continue;
    }
    EXPECT_TRUE(std::isfinite(bounds.rgb_relative));
    EXPECT_TRUE(std::isfinite(bounds.rgb_absolute));
    // Absolute RGB bounds are recorded in scene units; the texels are
    // pre-exposed.
    const auto pre_exposure = static_cast<double>(Read<FrameExposureData>(
      *probe->frame->buffer, ResourceStates::kShaderResource)
        .pre_exposure);
    for (std::size_t i = 0U; i < observed.size(); ++i) {
      for (unsigned c = 0U; c < 4U; ++c) {
        const double error
          = std::abs(observed.at(i).at(c) - reference.at(i).at(c));
        const double allowance = c == 3U
          ? (static_cast<double>(bounds.transmittance_relative)
              * reference.at(i).at(c))
            + bounds.transmittance_absolute
          : (static_cast<double>(bounds.rgb_relative) * reference.at(i).at(c))
            + (static_cast<double>(bounds.rgb_absolute) * pre_exposure);
        EXPECT_LE(error, allowance) << "voxel=" << i << " channel=" << c;
      }
    }
    const double error
      = std::abs(observed.back().at(0) - reference.back().at(0));
    if (reference.back().at(0) > 0.0) {
      maximum_error = std::max(maximum_error, error / reference.back().at(0));
    }
    if (step == 1U) {
      opacity = reference.back().at(0);
    }
    if (step == 64U) {
      last_half_error = error;
      last_half_bounds = bounds;
      EXPECT_EQ(storage.completed.flags & 16U, 0U);
    }
    if (step >= 65U && step <= 72U) {
      // The reused history is FP32, so the FP32 product is exact at once.
      EXPECT_TRUE(probe->history_used);
      EXPECT_FALSE(probe->discontinuity);
      EXPECT_EQ(error, 0.0);
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
      // The non-finite step leaves nothing behind: the fog product itself
      // qualifies. This fixture has no geometry, so the fog is seen only over
      // the sky, which composes the whole volume; the certified error of
      // hardware-filtering it from FP16 storage (HdrFilterArithmetic) keeps
      // the scene image from narrowing, independently of the history.
      EXPECT_EQ(qualification.failure_flags & 16U, 0U);
      EXPECT_EQ(qualification.first_failure_product, 11U);
      EXPECT_EQ(storage.completed.fp16_eligible_streak, 0U);
    }
    renderer_->OnFrameEnd(observer_ptr {
      &frame,
    });
    Backend().EndFrame(
      frame::SequenceNumber {
        step,
      },
      slot);
    WaitForQueueIdle();
  }
  // Half-precision frames show only the narrowing to FP16: at most half an
  // ulp, relative 2^-11.
  EXPECT_GT(maximum_error, 0.0);
  EXPECT_LE(maximum_error, 1.0 / 2048.0);
  RecordProperty("maximum_observed_error", std::to_string(maximum_error));
  RecordProperty("last_half_error", std::to_string(last_half_error));
  RecordProperty(
    "last_half_relative_bound", std::to_string(last_half_bounds.rgb_relative));
  RecordProperty(
    "last_half_absolute_bound", std::to_string(last_half_bounds.rgb_absolute));
}

//! The integration pass accumulates each froxel column front to back: at
//! every slice, the light scattered by all slices up to it, attenuated by the
//! slices in front, and their combined transmittance. A medium confined to
//! the front slice must still reach every texel behind it.
NOLINT_TEST_F(ExposureGpuTest, FogIntegrationAccumulatesSlicesFrontToBack)
{
  namespace root = oxygen::bindless::generated::d3d12;
  constexpr unsigned kWidth = 2U;
  constexpr unsigned kDepth = 4U;
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest {
          .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogIntegrateCS",
        })
        .SetRootBindings(ExposureProbeRootBindings())
        .SetDebugName("Fog integration fixture")
        .Build();
  auto output = CreateRegisteredTexture({
    .width = kWidth,
    .height = 1U,
    .depth = kDepth,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*output, std::move(handle),
    TextureViewDescription {
      .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D,
    });

  // Column 0: a varied medium. Column 1: a medium only in the front slice,
  // clear air behind it (the case a per-cell evaluation loses).
  const std::array<std::array<Pixel, kDepth>, kWidth> columns {
    {
      {
        {
          { 1.0F, 2.0F, 3.0F, .5F },
          { 2.0F, 1.0F, .5F, .25F },
          { 0.0F, 0.0F, 0.0F, 1.0F },
          { 4.0F, 8.0F, 2.0F, .5F },
        },
      },
      {
        {
          { 3.0F, 1.5F, .75F, .2F },
          { 0.0F, 0.0F, 0.0F, 1.0F },
          { 0.0F, 0.0F, 0.0F, 1.0F },
          { 0.0F, 0.0F, 0.0F, 1.0F },
        },
      },
    },
  };
  std::array<Pixel, static_cast<std::size_t>(kWidth) * kDepth> slices {};
  for (unsigned z = 0U; z < kDepth; ++z) {
    for (unsigned x = 0U; x < kWidth; ++x) {
      slices.at((z * kWidth) + x) = columns.at(x).at(z);
    }
  }
  const auto scattering
    = MakeSignal(kWidth, 1U, slices, kDepth, Format::kRGBA32Float, true);

  auto frame_bindings = ViewFrameBindings {};
  frame_bindings.frame_exposure_slot = PublishUnitFrameExposure();
  auto view = ViewConstants::GpuData {};
  view.bindless_view_frame_bindings_slot = BindlessViewFrameBindingsSlot {
    PublishFixtureData(frame_bindings),
  };
  auto view_buffer = CreateUploadBuffer(
    SizeBytes {
      256U,
    },
    BufferUsage::kConstant);
  view_buffer->Update(&view, sizeof(view), 0U);

  auto params = vortex::testing::RendererPublicationProbe::FogPassConstants {};
  params.output_header = {
    .output_texture_uav = output_slot.get(),
    .output_width = kWidth,
    .output_height = 1U,
    .output_depth = kDepth,
  };
  params.light_scattering_srv = scattering.srv.get();
  const auto pass_slot = PublishFixtureData(params);
  {
    auto recorder = AcquireRecorder("Fog integration dispatch");
    recorder->BeginTrackingResourceState(
      *output, ResourceStates::kCommon, false);
    recorder->RequireResourceState(*output, ResourceStates::kUnorderedAccess);
    recorder->FlushBarriers();
    recorder->SetPipelineState(pipeline);
    recorder->SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(root::RootParam::kViewConstants),
      view_buffer->GetGPUVirtualAddress());
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(root::RootParam::kRootConstants), 0U, 0U);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(root::RootParam::kRootConstants),
      pass_slot.get(), 1U);
    recorder->Dispatch(1U, 1U, 1U);
  }

  const auto actual = ReadFloatTexture(*output);
  ASSERT_EQ(actual.size(), slices.size());
  for (unsigned x = 0U; x < kWidth; ++x) {
    std::array<double, 3> luminance {};
    double transmittance = 1.0;
    for (unsigned z = 0U; z < kDepth; ++z) {
      const auto& slice = columns.at(x).at(z);
      for (unsigned c = 0U; c < 3U; ++c) {
        luminance.at(c) += transmittance * static_cast<double>(slice.at(c));
      }
      transmittance *= static_cast<double>(slice.at(3));
      const auto& texel = actual.at((z * kWidth) + x);
      for (unsigned c = 0U; c < 3U; ++c) {
        EXPECT_NEAR(texel.at(c), luminance.at(c), 1e-6 * luminance.at(c))
          << "x=" << x << " z=" << z << " channel=" << c;
      }
      EXPECT_NEAR(texel.at(3), transmittance, 1e-7) << "x=" << x << " z=" << z;
    }
  }
  // The clear slices behind the front medium keep its light.
  EXPECT_FLOAT_EQ(actual.at(((kDepth - 1U) * kWidth) + 1U).at(0), 3.0F);
  WaitForQueueIdle();
}

} // namespace oxygen::vortex::testing::exposure
