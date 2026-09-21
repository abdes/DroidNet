//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <tuple>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::TextureDesc;

NOLINT_TEST_F(
  ExposureGpuTest, UnavailableMaskPreventsMeterInitializationButNotLockedSolve)
{
  const auto signal = Uniform(.25F);
  const auto pending = Run(signal, {}, 1.0F, nullptr, 1.0F, false);
  EXPECT_EQ(pending.histogram.at(257), 0U);
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
  auto service = PostProcessService(*renderer_,
    observer_ptr {
      &loader,
    });
  auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  data::pak::render::TexturePayloadHeader header {};
  std::memcpy(&header,
    std::span {
      payload,
    }
      .subspan(sizeof(data::pak::core::TextureResourceDesc), sizeof(header))
      .data(),
    sizeof(header));
  payload.at(sizeof(data::pak::core::TextureResourceDesc)
    + header.data_offset_bytes) = 128U;
  auto settings = scene::ExposureSettings {};
  settings.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  const auto tag = internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(tag,
    frame::Slot {
      0U,
    });
  service.OnFrameStart(
    frame::SequenceNumber {
      1U,
    },
    frame::Slot {
      0U,
    });
  EXPECT_EQ(service
              .ResolveViewExposureSettings(
                ctx_.current_view.view_state_handle, settings)
              .mask_status,
    oxygen::vortex::ExposureMaskStatus::kPending);
  // Each step flushes submitted upload work and observes its completed ticket.
  // This is resource-readiness synchronization, not exposure-settling warmup.
  for (unsigned i = 0U; i < 8U; ++i) {
    WaitForQueueIdle();
    const auto slot = frame::Slot {
      (i + 1U) % 3U,
    };
    renderer_->GetUploadCoordinator().OnFrameStart(tag, slot);
    service.OnFrameStart(
      frame::SequenceNumber {
        i + 2U,
      },
      slot);
    if (service
          .ResolveViewExposureSettings(
            ctx_.current_view.view_state_handle, settings)
          .mask_status
      == oxygen::vortex::ExposureMaskStatus::kReady) {
      break;
    }
  }
  const auto& accepted = service.ResolveViewExposureSettings(
    ctx_.current_view.view_state_handle, settings);
  ASSERT_EQ(accepted.mask_status, oxygen::vortex::ExposureMaskStatus::kReady);
  ASSERT_NE(accepted.mask, nullptr);
  const auto mask = Signal {
    .texture = accepted.mask->texture,
    .srv = accepted.mask->srv,
  };
  const auto result = Run(Uniform(.25F), settings, 0.0F, &mask);
  EXPECT_EQ(result.histogram.at(102), 2056U); // round-half-up(4095*128/255).
  EXPECT_EQ(result.histogram.at(257), 1U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, SetConfigLoadsMasksAndRetainsAcceptedReplacementAtomically)
{
  auto loader = vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_,
    observer_ptr {
      &loader,
    });
  auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  data::pak::render::TexturePayloadHeader header {};
  std::memcpy(&header,
    std::span {
      payload,
    }
      .subspan(sizeof(data::pak::core::TextureResourceDesc), sizeof(header))
      .data(),
    sizeof(header));
  payload.at(sizeof(data::pak::core::TextureResourceDesc)
    + header.data_offset_bytes) = 128U;
  auto config = PostProcessConfig {};
  config.exposure.key = 12.5F;
  config.exposure.metering_mask
    = loader.PreloadCookedTexture(std::span(payload));
  service.SetConfig(config);
  const auto signal = Uniform(.25F);
  const auto render
    = [&] -> std::optional<PostProcessService::PreparedExposure> {
    ++sequence_;
    ctx_.frame_sequence = frame::SequenceNumber {
      sequence_,
    };
    ctx_.frame_slot = frame::Slot {
      static_cast<unsigned>(sequence_ % 3U),
    };
    renderer_->GetUploadCoordinator().OnFrameStart(
      internal::RendererTagFactory::Get(), ctx_.frame_slot);
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    {
      auto scene_inputs = PostProcessService::Inputs {};
      scene_inputs.scene_signal = signal.texture.get();
      scene_inputs.scene_signal_srv = signal.srv;
      return SubmitCommands(
        "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
          return service.PrepareSceneExposure(
            ctx_.current_view.view_id, ctx_, recorder, scene_inputs);
        });
    }
  };
  auto prepared = render();
  if (!prepared.has_value()) {
    FAIL() << "Expected prepared to contain a value";
  }
  EXPECT_EQ(prepared->config.Exposure().authored.metering_mask.get(), 0U);
  EXPECT_EQ(ReadState(prepared->exposure).flags & 4U, 0U);
  for (unsigned i = 0U; i < 8U
    && prepared->config.Exposure().authored.metering_mask
      != config.exposure.metering_mask;
    ++i) {
    WaitForQueueIdle();
    prepared = render();
    if (!prepared.has_value()) {
      FAIL() << "Expected prepared to contain a value";
    }
  }
  ASSERT_EQ(prepared->config.Exposure().authored, config.exposure);
  const auto histogram = Read<std::array<std::uint32_t, 264>>(
    *prepared->exposure.histogram_buffer, ResourceStates::kCommon);
  EXPECT_EQ(histogram.at(102), 2056U);
  EXPECT_EQ(histogram.at(257), 1U);
  const auto accepted = prepared->config;
  const auto accepted_state = ReadState(prepared->exposure);
  EXPECT_NEAR(accepted_state.displayed_scale, .72F, 2e-4F);
  config.exposure.metering_mask = loader.MintSyntheticTextureKey();
  config.exposure.compensation_ev = 1.0F;
  service.SetConfig(config);
  for (unsigned i = 0U; i < 3U; ++i) {
    WaitForQueueIdle();
    prepared = render();
    if (!prepared.has_value()) {
      FAIL() << "Expected prepared to contain a value";
    }
    EXPECT_EQ(
      prepared->config.Exposure().authored, accepted.Exposure().authored);
    EXPECT_EQ(prepared->config.Revision(), accepted.Revision());
    EXPECT_EQ((Read<std::array<std::uint32_t, 264>>(
                *prepared->exposure.histogram_buffer, ResourceStates::kCommon)
                  .at(102)),
      2056U);
    EXPECT_EQ(ReadState(prepared->exposure).displayed_scale,
      accepted_state.displayed_scale);
  }
  // The scene entry point supplies the same authored request through capture.
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    902U,
  };
  ctx_.current_view.view_id = ViewId {
    902U,
  };
  service.OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    ctx_.frame_slot);
  ctx_.frame_sequence = frame::SequenceNumber {
    sequence_,
  };
  std::ignore = service.CaptureViewExposureSettings(ctx_.current_view.view_id,
    ctx_.current_view.view_state_handle, accepted.Exposure().authored);
  auto scene_result_inputs = PostProcessService::Inputs {};
  scene_result_inputs.scene_signal = signal.texture.get();
  scene_result_inputs.scene_signal_srv = signal.srv;
  const auto scene_result = SubmitCommands(
    "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
      return service.PrepareSceneExposure(
        ctx_.current_view.view_id, ctx_, recorder, scene_result_inputs);
    });
  if (!scene_result.has_value()) {
    FAIL() << "Expected scene_result to contain a value";
  }
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
  auto service = PostProcessService(*renderer_,
    observer_ptr {
      &loader,
    });
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
  for (bool previous : {
         false,
         true,
       }) {
    for (bool failure : {
           false,
           true,
         }) {
      const auto handle = CompositionView::ViewStateHandle {
        id++,
      };
      ctx_.current_view.view_state_handle = handle;
      ctx_.frame_sequence = frame::SequenceNumber {
        ++sequence_,
      };
      service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
      auto requested = scene::ExposureSettings {};
      requested.key = 12.5F;
      if (previous) {
        std::ignore = service.ResolveViewExposureSettings(handle, requested);
      }
      requested.metering_mask = loader.MintSyntheticTextureKey();
      EXPECT_EQ(
        service.ResolveViewExposureSettings(handle, requested).mask_status,
        oxygen::vortex::ExposureMaskStatus::kPending);
      if (failure) {
        ctx_.frame_sequence = frame::SequenceNumber {
          ++sequence_,
        };
        service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
        EXPECT_EQ(
          service.ResolveViewExposureSettings(handle, requested).mask_status,
          oxygen::vortex::ExposureMaskStatus::kFailed);
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
      {
        auto exposure_inputs = PostProcessService::Inputs {};
        exposure_inputs.scene_signal = signal.texture.get();
        exposure_inputs.post_target = observer_ptr<const Framebuffer> {
          framebuffer.get(),
        };
        exposure_inputs.scene_signal_srv = signal.srv;
        SubmitCommands("Vortex PostProcess",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return service.Record(
              ctx_.current_view.view_id, ctx_, recorder, exposure_inputs);
          });
      }
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
        if (!ticket.has_value()) {
          FAIL() << "Expected ticket to contain a value";
        }
      }
      const auto mapped = readback->MapNow();
      if (!mapped.has_value()) {
        FAIL() << "Expected mapped to contain a value";
      }
      Pixel pixel {};
      std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(pixel.at(channel), .25F, 2e-5F) << previous << failure;
      }
    }
  }
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, LateMaskRemovalCannotEnableMeteringForCapturedFrame)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.metering_mask = content::ResourceKey {
    123U,
  };
  auto pixel_options = ServicePixelOptions {};
  pixel_options.before_execute = [&] -> void {
    settings.key = 12.5F;
    settings.metering_mask = {};
    const auto& late = service.ResolveViewExposureSettings(
      ctx_.current_view.view_state_handle, settings);
    auto config = service.GetConfig();
    config.exposure = late.resolved.authored;
    service.SetConfig(config);
  };
  const auto pixel = ServicePixel(service, signal, settings, pixel_options);
  // The initial invalid-mask fallback uses the canonical key 10 at EV0.
  EXPECT_NEAR(pixel, .25F * .8F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, signal, settings), .18F, 2e-5F);
}

} // namespace oxygen::vortex::testing::exposure
