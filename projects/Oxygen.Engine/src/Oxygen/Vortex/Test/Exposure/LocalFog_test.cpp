//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Environment/Passes/FogPass.h>
#include <Oxygen/Vortex/Environment/Passes/LocalFogVolumeComposePass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Color;

using graphics::Buffer;
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

NOLINT_TEST_F(ExposureGpuTest, LocalFogInjectionMatchesMixedMediumIntegral)
{
  namespace root = oxygen::bindless::generated::d3d12;
  auto fog_scene = std::make_shared<scene::Scene>("Injected medium range", 8U);
  auto node = fog_scene->CreateNode("Local medium");
  auto impl = node.GetImpl();
  if (!impl.has_value()) {
    FAIL() << "Expected impl to contain a value";
  }
  auto& local = impl->get().AddComponent<scene::environment::LocalFogVolume>();
  local.SetEnabled(true);
  local.SetHeightFogFalloff(1);
  local.SetHeightFogOffset(0);
  const float sample_depth = std::numbers::sqrt2_v<float>;
  node.GetTransform().SetLocalPosition({
    0,
    0,
    -sample_depth,
  });
  node.GetTransform().SetLocalScale({
    .2F,
    .2F,
    .2F,
  });
  fog_scene->Update();
  auto resolved = ResolvedView { ResolvedView::Params {} };
  ctx_.scene = observer_ptr {
    fog_scene.get(),
  };
  ctx_.current_view.resolved_view = observer_ptr {
    &resolved,
  };
  ctx_.current_view.with_local_fog = true;
  auto local_state = environment::internal::LocalFogVolumeState(*renderer_);
  auto output = CreateRegisteredTexture({
    .width = 1,
    .height = 1,
    .depth = 1,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto tiles = CreateRegisteredTexture({
    .width = 1,
    .height = 1,
    .array_size = 2,
    .format = Format::kR32UInt,
    .texture_type = TextureType::kTexture2DArray,
    .is_shader_resource = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto tile_upload = CreateUploadBuffer(SizeBytes {
    1024,
  });
  std::array<std::uint32_t, 256> tile_data {};
  tile_data.at(0) = 1;
  tile_upload->Update(tile_data.data(), sizeof(tile_data), 0);
  {
    auto recorder = AcquireRecorder("Injected medium tile upload");
    EnsureTracked(*recorder, tile_upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, tiles, ResourceStates::kCommon);
    recorder->RequireResourceState(*tiles, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*tile_upload,
      {
        .buffer_row_pitch = 256,
        .buffer_slice_pitch = 256,
        .dst_slice = { .width = 1, .height = 1, .depth = 1 },
      },
      *tiles);
    recorder->RequireResourceStateFinal(
      *tiles, ResourceStates::kShaderResource);
  }
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto tile_handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto tile_slot = allocator.GetShaderVisibleIndex(tile_handle);
  Backend().GetResourceRegistry().RegisterView(*tiles, std::move(tile_handle),
    TextureViewDescription {
      .format = Format::kR32UInt,
      .dimension = TextureType::kTexture2DArray,
    });
  auto output_handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(output_handle);
  Backend().GetResourceRegistry().RegisterView(*output,
    std::move(output_handle),
    TextureViewDescription {
      .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D,
    });
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest {
          .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS",
        })
        .SetRootBindings(ExposureProbeRootBindings())
        .SetDebugName("Injected medium analytic fixture")
        .Build();
  struct Case {
    const char* name;
    float global_density;
    float radial;
    float height;
    float global_emission;
    float local_emission;
    float illuminance;
    bool scattering;
    bool unsupported;
  };
  const std::array cases {
    Case {
      .name = "vacuum",
      .global_density = 0,
      .radial = 0,
      .height = 1,
      .global_emission = 1,
      .local_emission = 0x1p32F,
      .illuminance = 0,
      .scattering = false,
      .unsupported = false,
    },
    Case {
      .name = "height only",
      .global_density = .06F,
      .radial = 0,
      .height = 1,
      .global_emission = 2,
      .local_emission = 0,
      .illuminance = 0,
      .scattering = false,
      .unsupported = false,
    },
    Case {
      .name = "thin local emission",
      .global_density = 0,
      .radial = 1,
      .height = 0x1p-40F,
      .global_emission = 0,
      .local_emission = 0x1p32F,
      .illuminance = 0,
      .scattering = false,
      .unsupported = false,
    },
    Case {
      .name = "small local emission",
      .global_density = 0,
      .radial = 1,
      .height = 1,
      .global_emission = 0,
      .local_emission = 0x1p-22F,
      .illuminance = 0,
      .scattering = false,
      .unsupported = false,
    },
    Case {
      .name = "bright local emission",
      .global_density = 0,
      .radial = 1,
      .height = 1,
      .global_emission = 0,
      .local_emission = 0x1p32F,
      .illuminance = 0,
      .scattering = false,
      .unsupported = false,
    },
    Case {
      .name = "mixed emission",
      .global_density = .1F,
      .radial = 1,
      .height = 1,
      .global_emission = 1,
      .local_emission = 3,
      .illuminance = 0,
      .scattering = false,
      .unsupported = false,
    },
    Case {
      .name = "thin local scattering",
      .global_density = 0,
      .radial = 1,
      .height = 0x1p-24F,
      .global_emission = 0,
      .local_emission = 0,
      .illuminance = 0x1p32F,
      .scattering = true,
      .unsupported = false,
    },
    Case {
      .name = "mixed scattering",
      .global_density = .06F,
      .radial = 1,
      .height = 1,
      .global_emission = 0,
      .local_emission = 0,
      .illuminance = 0x1p32F,
      .scattering = true,
      .unsupported = false,
    },
    Case {
      .name = "unsupported",
      .global_density = 0,
      .radial = 1,
      .height = 1,
      .global_emission = 0,
      .local_emission = 0x1p38F,
      .illuminance = 0,
      .scattering = false,
      .unsupported = true,
    },
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  auto context = engine::FrameContext {};
  context.SetScene(observer_ptr {
    fog_scene.get(),
  });
  unsigned sequence = 0;
  for (const auto& test : cases) {
    for (const float ev : {
           -32.0F,
           0.0F,
           32.0F,
         }) {
      SCOPED_TRACE(test.name);
      SCOPED_TRACE(ev);
      const auto slot = frame::Slot {
        sequence % 3,
      };
      const auto seq = frame::SequenceNumber {
        ++sequence,
      };
      Backend().BeginFrame(seq, slot);
      context.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
      context.SetFrameSequenceNumber(
        seq, engine::internal::EngineTagFactory::Get());
      renderer_->OnFrameStart(observer_ptr {
        &context,
      });
      ctx_.frame_slot = slot;
      ctx_.frame_sequence = seq;
      local.SetRadialFogExtinction(test.radial);
      local.SetHeightFogExtinction(test.height);
      local.SetFogEmissive({
        test.local_emission,
        test.local_emission,
        test.local_emission,
      });
      local.SetFogAlbedo(test.scattering ? Vec3 { 1, 1, 1, } : Vec3 { 0, 0, 0, });
      local_state.OnFrameStart(seq, slot);
      const auto products = local_state.Prepare(ctx_);
      ASSERT_TRUE(products.buffer_ready);
      ASSERT_EQ(products.instance_count, 1U);
      settings.manual_ev = ev;
      auto exposure_inputs = postprocess::ExposurePass::FrameInputs {};
      exposure_inputs.use_fp32 = false;
      const auto exposure = SubmitCommands("Vortex Exposure Frame",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return pass_->ResolveFrame(
            ctx_, recorder, SharedConfig(settings), exposure_inputs);
        });
      ASSERT_NE(exposure, nullptr);
      auto bindings = ViewFrameBindings {};
      bindings.frame_exposure_slot = exposure->srv_index;
      bindings.exposure_status_uav = exposure->current_state->status_uav_index;
      auto view = ViewConstants::GpuData {};
      view.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
        PublishFixtureData(bindings),
      };
      auto projection_depth = glm::column(view.projection_matrix, 2);
      projection_depth.z = -1.0F / 32;
      view.projection_matrix
        = glm::column(view.projection_matrix, 2, projection_depth);
      auto inverse_projection_depth
        = glm::column(view.inverse_view_projection_matrix, 2);
      inverse_projection_depth.z = -32;
      view.inverse_view_projection_matrix = glm::column(
        view.inverse_view_projection_matrix, 2, inverse_projection_depth);
      auto view_buffer = CreateUploadBuffer(
        SizeBytes {
          256,
        },
        BufferUsage::kConstant);
      view_buffer->Update(&view, sizeof(view), 0);
      auto params
        = vortex::testing::RendererPublicationProbe::FogPassConstants {};
      params.output_header = {
        .output_texture_uav = output_slot.get(),
        .output_width = 1,
        .output_height = 1,
        .output_depth = 1,
      };
      params.grid = {
        .start_distance_m = 1,
        .end_distance_m = 32,
        .near_fade_in_distance_m = 0,
        .global_extinction_scale = 1,
      };
      params.grid_z = {
        .grid_z_params = { 1, 0, 1 },
        .shadowed_directional_light0_enabled = 0,
      };
      params.height_fog0.primary_density = test.global_density;
      params.height_fog1.match_height_fog_factor = 1;
      params.height_fog1.enabled = 1;
      params.media0
        = { .albedo_rgb = { test.scattering ? 1.0F : 0.0F,
              test.scattering ? 1.0F : 0.0F, test.scattering ? 1.0F : 0.0F, },
            .scattering_distribution = 0, };
      params.media1 = {
        .emissive_rgb
        = { test.global_emission, test.global_emission, test.global_emission },
        .static_lighting_scattering_intensity = 1,
      };
      params.local_fog0 = {
        .instance_buffer_slot = products.instance_buffer_slot.get(),
        .tile_data_texture_slot = tile_slot.get(),
        .instance_count = 1,
        .enabled = 1,
      };
      params.local_fog1 = {
        .tile_resolution_x = 1,
        .tile_resolution_y = 1,
        .max_instances_per_tile = 1,
        .global_start_distance_m = 0,
      };
      params.local_fog2.max_density_into_volumetric_fog = 100;
      params.light0_direction_enabled[2] = 1;
      params.light0_direction_enabled[3] = 1;
      std::ranges::fill(
        std::span {
          params.light0_illuminance_rgb,
        }
          .first<3>(),
        test.illuminance);
      std::ranges::fill(
        std::span {
          params.temporal_history1.frame_jitter_offsets[0],
        }
          .first<3>(),
        .5F);
      params.exposure_status_uav
        = exposure->current_state->status_uav_index.get();
      const auto constants = PublishFixtureData(params);
      // The single froxel samples exactly at the authored local-volume center.
      // Independent double Beer-Lambert solution for mixed source coefficients.
      const double local_opacity
        = -std::expm1(-static_cast<double>(test.radial))
        * -std::expm1(-static_cast<double>(test.height));
      const double local_density = -std::log1p(-local_opacity);
      const double total_density = test.global_density + local_density;
      const double length = std::numbers::sqrt2 - 1;
      const double opacity = -std::expm1(-total_density * length);
      const double light = test.scattering
        ? static_cast<double>(test.illuminance) / (4 * std::acos(-1.0))
          * static_cast<double>(2e-5F)
        : 0;
      const double source = (static_cast<double>(test.global_density)
                              * (test.global_emission + light))
        + (local_density * (test.local_emission + light));
      const double radiance
        = total_density > 0 ? source / total_density * opacity : 0;
      observer_ptr<FrameCaptureController> capture;
      if (ev == 0 && test.height == 0x1p-40F) {
        capture = BeginOptionalCapture();
      }
      {
        auto recorder = AcquireRecorder("Injected medium production dispatch");
        if (!recorder->AdoptKnownResourceState(*output)) {
          recorder->BeginTrackingResourceState(
            *output, ResourceStates::kCommon, false);
        }
        ASSERT_TRUE(recorder->AdoptKnownResourceState(
          *exposure->current_state->status_buffer));
        recorder->RequireResourceState(
          *output, ResourceStates::kUnorderedAccess);
        recorder->RequireResourceState(*exposure->current_state->status_buffer,
          ResourceStates::kUnorderedAccess);
        recorder->FlushBarriers();
        recorder->SetPipelineState(pipeline);
        recorder->SetComputeRootConstantBufferView(
          static_cast<unsigned>(root::RootParam::kViewConstants),
          view_buffer->GetGPUVirtualAddress());
        recorder->SetComputeRoot32BitConstant(
          static_cast<unsigned>(root::RootParam::kRootConstants), 0, 0);
        recorder->SetComputeRoot32BitConstant(
          static_cast<unsigned>(root::RootParam::kRootConstants),
          constants.get(), 1);
        recorder->Dispatch(1, 1, 1);
      }
      if (capture) {
        EXPECT_TRUE(capture->EndCapture());
      }
      const auto pixels = ReadFloatTexture(*output);
      ASSERT_EQ(pixels.size(), 1U);
      const double expected = radiance * std::exp2(-static_cast<double>(ev));
      for (unsigned c = 0; c < 3; ++c) {
        EXPECT_NEAR(
          pixels.at(0).at(c), expected, (std::abs(expected) * 2e-5) + 0x1p-120);
      }
      EXPECT_NEAR(pixels.at(0).at(3), std::exp(-total_density * length), 2e-5);
      const auto status
        = Read<ExposureCompletedStatus>(*exposure->current_state->status_buffer,
          ResourceStates::kUnorderedAccess);
      if (test.unsupported) {
        ASSERT_GT(radiance, 0x1p32);
        EXPECT_EQ(status.first_failure_product, 10U);
        EXPECT_NE(status.first_failure_kind & 32U, 0U);
        EXPECT_EQ(status.flags & 18U, 18U);
      } else {
        EXPECT_EQ(status.flags & 16U, 0U);
      }
      renderer_->OnFrameEnd(observer_ptr {
        &context,
      });
      Backend().EndFrame(seq, slot);
      WaitForQueueIdle();
    }
  }
  ctx_.scene.reset();
  ctx_.current_view.resolved_view.reset();
  RecordProperty("local_injection_cases", sequence);
}

NOLINT_TEST_F(ExposureGpuTest, AuthoredLocalFogPreservesRadiometryThroughUpload)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto renderer_config = RendererConfig {};
  renderer_config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), renderer_config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr {
    &console,
  });
  ASSERT_EQ(console.Execute("vtx.local_fog.global_start_distance_m 0").status,
    console::ExecutionStatus::kOk);
  auto fog_scene
    = std::make_shared<scene::Scene>("Local fog radiometric domain", 8U);
  auto node = fog_scene->CreateNode("Authored local fog");
  auto impl = node.GetImpl();
  if (!impl.has_value()) {
    FAIL() << "Expected impl to contain a value";
  }
  auto& fog = impl->get().AddComponent<scene::environment::LocalFogVolume>();
  fog.SetEnabled(true);
  fog.SetFogAlbedo({
    0,
    0,
    0,
  });
  fog.SetHeightFogOffset(0);
  node.GetTransform().SetLocalScale({
    .2F,
    .2F,
    .2F,
  });
  fog_scene->Update();
  auto resolved_params = ResolvedView::Params {};
  resolved_params.view_config.viewport = {
    .width = 1,
    .height = 1,
  };
  resolved_params.view_config.scissor = {
    .right = 1,
    .bottom = 1,
  };
  auto lens = scene::PerspectiveCamera {};
  lens.SetViewport(resolved_params.view_config.viewport);
  resolved_params.proj_matrix = lens.ProjectionMatrix();
  auto resolved = ResolvedView {
    resolved_params,
  };
  ctx_.scene = observer_ptr {
    fog_scene.get(),
  };
  ctx_.current_view.resolved_view = observer_ptr {
    &resolved,
  };
  ctx_.current_view.with_local_fog = true;
  auto state = environment::internal::LocalFogVolumeState(*renderer_);
  auto compose = environment::LocalFogVolumeComposePass(*renderer_);
  auto textures = SceneTextures(Backend(),
    {
      .extent = { 1U, 1U },
      .enable_velocity = false,
      .scene_color_format = Format::kRGBA32Float,
    });
  auto framebuffer = Backend().CreateFramebuffer(FramebufferDesc {}
      .AddColorAttachment(textures.GetSceneColorResource())
      .SetDepthAttachment({
        .texture = textures.GetSceneDepthResource(),
        .format = textures.GetSceneDepth().GetDescriptor().format,
      }));
  auto tiles = CreateRegisteredTexture({
    .width = 1,
    .height = 1,
    .array_size = 2,
    .format = Format::kR32UInt,
    .texture_type = TextureType::kTexture2DArray,
    .is_shader_resource = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto tile_upload = CreateUploadBuffer(SizeBytes {
    1024U,
  });
  std::array<std::uint32_t, 256> tile_values {};
  tile_values.at(0) = 1U; // One volume, instance index zero in array slice one.
  tile_upload->Update(tile_values.data(), sizeof(tile_values), 0U);
  {
    auto recorder = AcquireRecorder("Local fog tile fixture upload");
    EnsureTracked(*recorder, tile_upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, tiles, ResourceStates::kCommon);
    recorder->RequireResourceState(*tiles, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*tile_upload,
      {
        .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 1, .height = 1, .depth = 1 },
      },
      *tiles);
    recorder->RequireResourceStateFinal(
      *tiles, ResourceStates::kShaderResource);
  }
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  const auto texture_srv = [&](const Texture& texture,
                             TextureType type) -> bindless::ShaderVisibleIndex {
    auto handle = allocator.AllocateRaw(
      ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const auto index = allocator.GetShaderVisibleIndex(handle);
    Backend().GetResourceRegistry().RegisterView(texture, std::move(handle),
      TextureViewDescription {
        .format = texture.GetDescriptor().format,
        .dimension = type,
      });
    return index;
  };
  const auto tiles_slot = texture_srv(*tiles, TextureType::kTexture2DArray);
  auto scene_bindings = SceneTextureBindings {};
  scene_bindings.scene_depth_srv
    = texture_srv(textures.GetSceneDepth(), TextureType::kTexture2D).get();
  const auto scene_slot = PublishFixtureData(scene_bindings);
  const auto occupied_slot = PublishFixtureData(std::uint32_t {
    0U,
  });
  auto args = CreateUploadBuffer(SizeBytes {
    16U,
  });
  const std::array<std::uint32_t, 4> draw_args {
    6U,
    1U,
    0U,
    0U,
  };
  args->Update(draw_args.data(), sizeof(draw_args), 0U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0;
  const auto config = SharedConfig(settings);
  struct Case {
    float radial;
    float height;
    float falloff;
    float emission;
    bool expected_range_failure;
  };
  const std::array cases {
    Case {
      .radial = 1,
      .height = 0,
      .falloff = 100,
      .emission = 0,
      .expected_range_failure = false,
    },
    Case {
      .radial = 1,
      .height = 0x1p-24F,
      .falloff = 100,
      .emission = 0x1p-24F,
      .expected_range_failure = false,
    },
    Case {
      .radial = 1,
      .height = 0x1p-16F,
      .falloff = 100,
      .emission = 0x1p-16F,
      .expected_range_failure = false,
    },
    Case {
      .radial = 1,
      .height = 0x1p-14F,
      .falloff = 100,
      .emission = 0x1p-14F,
      .expected_range_failure = false,
    },
    Case {
      .radial = .45F,
      .height = .25F,
      .falloff = 100,
      .emission = 1,
      .expected_range_failure = false,
    },
    Case {
      .radial = 1,
      .height = 1,
      .falloff = 100,
      .emission = 131072,
      .expected_range_failure = false,
    },
    Case {
      .radial = 0x1p-24F,
      .height = 1,
      .falloff = 100,
      .emission = 0x1p32F,
      .expected_range_failure = false,
    },
    Case {
      .radial = 0x1p-16F,
      .height = 1,
      .falloff = 100,
      .emission = 0x1p32F,
      .expected_range_failure = false,
    },
    Case {
      .radial = 0x1p-14F,
      .height = 1,
      .falloff = 100,
      .emission = 0x1p32F,
      .expected_range_failure = false,
    },
    Case {
      .radial = 0x1p-24F,
      .height = 0x1p-24F,
      .falloff = 100,
      .emission = 0x1p32F,
      .expected_range_failure = false,
    },
    Case {
      .radial = 1,
      .height = 1,
      .falloff = 10000,
      .emission = 1,
      .expected_range_failure = false,
    },
    Case {
      .radial = 1,
      .height = 1,
      .falloff = 100,
      .emission = 0x1p37F,
      .expected_range_failure = true,
    },
  };
  unsigned sequence = 0;
  for (const auto& value : cases) {
    SCOPED_TRACE(sequence);
    fog.SetRadialFogExtinction(value.radial);
    fog.SetHeightFogExtinction(value.height);
    fog.SetHeightFogFalloff(value.falloff);
    fog.SetFogEmissive({
      value.emission,
      value.emission / 2,
      value.emission / 4,
    });
    state.OnFrameStart(
      frame::SequenceNumber {
        ++sequence,
      },
      frame::Slot {
        0U,
      });
    auto products = state.Prepare(ctx_);
    ASSERT_TRUE(products.buffer_ready);
    ASSERT_EQ(products.instance_count, 1U);
    const std::array<std::array<std::uint32_t, 4>, 2> decode_input {
      std::array {
        products.instance_buffer_slot.get(),
        0U,
        0U,
        0U,
      },
      std::array {
        0U,
        0U,
        0U,
        0U,
      },
    };
    const auto decoded = RunToneProbe(std::as_bytes(std::span {
                                        decode_input,
                                      }),
      1U, 4096U, false);
    EXPECT_EQ(decoded.at(0).at(0), value.radial);
    EXPECT_EQ(decoded.at(0).at(1), value.height);
    EXPECT_EQ(decoded.at(0).at(2), value.falloff * .01F);
    EXPECT_EQ(decoded.at(0).at(3), 0.0F);
    EXPECT_EQ(decoded.at(0).at(4), value.emission);
    EXPECT_EQ(decoded.at(0).at(5), value.emission / 2);
    EXPECT_EQ(decoded.at(0).at(6), value.emission / 4);
    EXPECT_EQ(decoded.at(0).at(7), 1.0F);
    // Reversed rays and small positive/negative changes of height exercise
    // the production signed integral, not a duplicate arithmetic helper.
    const std::array rays {
      Pixel {
        1,
        -1,
        1,
        0,
      },
      Pixel {
        0,
        1,
        1,
        0,
      },
      Pixel {
        .5F,
        1,
        1e-6F,
        0,
      },
      Pixel {
        .5F,
        -1,
        1e-6F,
        0,
      },
    };
    std::vector<std::array<std::uint32_t, 4>> inputs;
    for (const auto ray : rays) {
      inputs.push_back({
        products.instance_buffer_slot.get(),
        0U,
        0U,
        0U,
      });
      inputs.push_back(std::bit_cast<std::array<std::uint32_t, 4>>(ray));
    }
    const auto actual = RunToneProbe(std::as_bytes(std::span {
                                       inputs,
                                     }),
      static_cast<std::uint32_t>(rays.size()), 8192U, false);
    for (std::size_t i = 0; i < rays.size(); ++i) {
      SCOPED_TRACE(i);
      const double start = rays.at(i).at(0);
      const double direction = rays.at(i).at(1);
      const double length = rays.at(i).at(2);
      const auto falloff = static_cast<double>(value.falloff * .01F);
      const double radial_depth = static_cast<double>(value.radial) * .75
        * (((1 - (start * start)) * length)
          - (start * direction * length * length)
          - (length * length * length / 3));
      const double height_depth = static_cast<double>(value.height)
        * std::exp(-start * falloff)
        * -std::expm1(-direction * length * falloff) / (falloff * direction);
      const double combined
        = -std::expm1(-radial_depth) * -std::expm1(-height_depth);
      // The complement would lose very thin coverage in double too. At unit
      // scale the product of opacities is the exact unsaturated coverage.
      const double coverage = std::min(combined, 1.0 - 1e-6);
      EXPECT_NEAR(actual.at(i).at(3), coverage, (coverage * 2e-5) + 0x1p-120);
      const double media_opacity
        = -std::expm1(-static_cast<double>(value.radial))
        * -std::expm1(-static_cast<double>(value.height));
      const double media_extinction = media_opacity < 1 - 1e-6
        ? -std::log1p(-media_opacity)
        : -std::log(1e-6);
      EXPECT_NEAR(actual.at(i).at(7), media_extinction,
        (media_extinction * 2e-5) + 0x1p-120);
      for (unsigned c = 0; c < 3; ++c) {
        const double source
          = static_cast<double>(value.emission) / static_cast<double>(1U << c);
        EXPECT_NEAR(actual.at(i).at(c), source * coverage,
          (source * coverage * 2e-5) + 0x1p-120);
        EXPECT_NEAR(actual.at(i).at(c + 4), source * media_extinction,
          (source * media_extinction * 2e-5) + 0x1p-120);
      }
    }
    ctx_.frame_sequence = frame::SequenceNumber {
      sequence,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = SubmitCommands("Vortex Exposure Frame",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->ResolveFrame(ctx_, recorder, config, frame_inputs);
      });
    ASSERT_NE(frame, nullptr);
    ctx_.current_view.frame_exposure = frame;
    auto bindings = ViewFrameBindings {};
    bindings.scene_texture_frame_slot = scene_slot;
    bindings.frame_exposure_slot = frame->srv_index;
    bindings.exposure_status_uav = frame->current_state->status_uav_index;
    auto view = ViewConstants::GpuData {};
    view.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
      PublishFixtureData(bindings),
    };
    view.inverse_view_projection_matrix = glm::mat4 {
      0.0F,
    };
    view.inverse_view_projection_matrix
      = glm::column(view.inverse_view_projection_matrix, 3,
        glm::vec4 {
          0,
          0,
          .5F,
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
      auto recorder = AcquireRecorder("Local fog compose initialization");
      for (const auto& texture : {
             textures.GetSceneColorResource(),
             textures.GetSceneDepthResource(),
           }) {
        if (!recorder->AdoptKnownResourceState(*texture)) {
          recorder->BeginTrackingResourceState(
            *texture, texture->GetDescriptor().initial_state);
        }
      }
      EnsureTracked(*recorder, args, ResourceStates::kGenericRead);
      recorder->RequireResourceState(
        textures.GetSceneColor(), ResourceStates::kRenderTarget);
      recorder->RequireResourceState(
        textures.GetSceneDepth(), ResourceStates::kDepthWrite);
      recorder->FlushBarriers();
      recorder->ClearFramebuffer(*framebuffer,
        std::vector<std::optional<Color>> {
          Color {},
        },
        .5F);
    }
    products.tile_data_ready = true;
    products.tile_data_texture_slot = tiles_slot;
    products.occupied_tile_buffer_slot = occupied_slot;
    products.tile_resolution_x = products.tile_resolution_y = 1;
    products.max_instances_per_tile = 1;
    products.occupied_tile_draw_args_buffer = observer_ptr<const Buffer> {
      args.get(),
    };
    compose.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    observer_ptr<FrameCaptureController> capture;
    if (value.emission == 131072) {
      capture = BeginOptionalCapture();
    }
    ASSERT_TRUE(SubmitCommands(
      "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
        return compose.Record(ctx_, recorder, textures, products);
      }).executed);
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
    const auto pixels = ReadFloatTexture(textures.GetSceneColor());
    const auto falloff = static_cast<double>(value.falloff * .01F);
    const double start = renderer_->GetLocalFogGlobalStartDistanceMeters();
    const double height_depth = static_cast<double>(value.height)
      * std::exp(-start * falloff) * -std::expm1(-(.5 - start) * falloff)
      / falloff;
    const double radial_depth = static_cast<double>(value.radial) * .75
      * ((.5 - (.125 / 3)) - (start - (start * start * start / 3)));
    const double coverage
      = -std::expm1(-radial_depth) * -std::expm1(-height_depth);
    for (unsigned c = 0; c < 3; ++c) {
      const double expected = static_cast<double>(value.emission)
        / static_cast<double>(1U << c) * coverage;
      EXPECT_NEAR(pixels.at(0).at(c), expected, (expected * 2e-5) + 0x1p-120);
    }
    const auto status = Read<ExposureCompletedStatus>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    if (value.expected_range_failure) {
      ASSERT_GT(static_cast<double>(value.emission) * coverage, 0x1p32);
      EXPECT_EQ(status.first_failure_product, 9U);
      EXPECT_EQ(status.first_failure_kind, 32U);
      EXPECT_EQ(status.flags & 18U, 18U);
    } else {
      ASSERT_LE(static_cast<double>(value.emission) * coverage, 0x1p32);
      EXPECT_EQ(status.flags & 16U, 0U);
    }
    ctx_.current_view.frame_exposure.reset();
    ctx_.view_constants.reset();
  }
  RecordProperty("authored_local_fog_cases", cases.size() * 4);
  ctx_.current_view.resolved_view.reset();
  ctx_.scene.reset();
  WaitForQueueIdle();
}

} // namespace oxygen::vortex::testing::exposure
