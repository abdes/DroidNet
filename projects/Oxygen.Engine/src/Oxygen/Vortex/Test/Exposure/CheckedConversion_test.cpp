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
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using graphics::DescriptorVisibility;
using graphics::FrameCaptureController;
using graphics::ResourceStates;
using graphics::ResourceViewType;
using graphics::Texture;
using graphics::TextureDesc;
using graphics::TextureViewDescription;

NOLINT_TEST_F(ExposureGpuTest,
  CheckedSceneColorConversionUsesCurrentScaleAndRejectsTheWholeImage)
{
  constexpr std::uint32_t width = 9U;
  constexpr std::uint32_t height = 3U;
  using PackedPixel = std::array<std::uint16_t, 4U>;
  constexpr PackedPixel sentinel {
    0x3400U,
    0x3800U,
    0x3a00U,
    0x3c00U,
  };
  constexpr PackedPixel expected {
    0x3000U,
    0x3555U,
    0x3800U,
    0x3800U,
  };
  const Pixel ordinary {
    .125F,
    1.0F / 3.0F,
    .5F,
    .5F,
  };
  const float sensitive = 256.4375F * 0x1p-24F;
  struct Case {
    const char* name;
    Pixel pixel;
    float ev;
    bool fp32;
    bool background;
    std::uint32_t failure;
    PackedPixel last_pixel;
    bool automatic;
    bool zero_meter_mask;
  };
  const std::array cases {
    Case {
      .name = "ordinary",
      .pixel = ordinary,
      .ev = 0,
      .fp32 = true,
      .background = false,
      .failure = 0U,
      .last_pixel = expected,
      .automatic = false,
      .zero_meter_mask = false,
    },
    Case {
      .name = "typed half rounding",
      .pixel = { 1.500732421875F, 1.500244140625F, 1.50048828125F, 1.0F },
      .ev = 0,
      .fp32 = true,
      .background = false,
      .failure = 0U,
      .last_pixel = { 0x3e01U, 0x3e00U, 0x3e00U, 0x3c00U },
      .automatic = false,
      .zero_meter_mask = false,
    },
    Case {
      .name = "odd tie and exponent boundary",
      .pixel = { 1.50146484375F, 8198.0F, 1.99951171875F, 1.0F },
      .ev = 0,
      .fp32 = true,
      .background = false,
      .failure = 0U,
      .last_pixel = { 0x3e02U, 0x7001U, 0x4000U, 0x3c00U },
      .automatic = false,
      .zero_meter_mask = false,
    },
    Case {
      .name = "subnormal nearest even",
      .pixel = { 1.75F * 0x1p-24F, 2.5F * 0x1p-24F, 3.5F * 0x1p-24F, 0.0F },
      .ev = 0,
      .fp32 = true,
      .background = false,
      .failure = 0U,
      .last_pixel = { 2U, 2U, 4U, 0U },
      .automatic = false,
      .zero_meter_mask = true,
    },
    Case {
      .name = "overflow",
      .pixel = { 0x1p20F, 0x1p20F, 0x1p20F, 1 },
      .ev = 0,
      .fp32 = true,
      .background = false,
      .failure = 2U,
      .last_pixel = sentinel,
      .automatic = false,
      .zero_meter_mask = false,
    },
    Case {
      .name = "nonfinite",
      .pixel = { std::numeric_limits<float>::quiet_NaN(), 0, 0, 1 },
      .ev = 0,
      .fp32 = true,
      .background = false,
      .failure = 1U,
      .last_pixel = sentinel,
      .automatic = false,
      .zero_meter_mask = false,
    },
    Case {
      .name = "displayed dark loss",
      .pixel = { 0x1p-30F, 0x1p-30F, 0x1p-30F, 1 },
      .ev = -30,
      .fp32 = true,
      .background = false,
      .failure = 4U,
      .last_pixel = sentinel,
      .automatic = false,
      .zero_meter_mask = false,
    },
    Case {
      .name = "nonunit P",
      .pixel = ordinary,
      .ev = -2,
      .fp32 = false,
      .background = false,
      .failure = 0U,
      .last_pixel = expected,
      .automatic = false,
      .zero_meter_mask = false,
    },
    Case {
      .name = "opaque zero alpha",
      .pixel = { sensitive, sensitive, sensitive, 0 },
      .ev = 0,
      .fp32 = true,
      .background = false,
      .failure = 8U,
      .last_pixel = sentinel,
      .automatic = true,
      .zero_meter_mask = false,
    },
    Case {
      .name = "background zero alpha",
      .pixel = { sensitive, sensitive, sensitive, 0 },
      .ev = 0,
      .fp32 = true,
      .background = true,
      .failure = 0U,
      .last_pixel = { 0x0100U, 0x0100U, 0x0100U, 0U },
      .automatic = true,
      .zero_meter_mask = false,
    },
    Case {
      .name = "opaque partial alpha",
      .pixel = { sensitive, sensitive, sensitive, .5F },
      .ev = 0,
      .fp32 = true,
      .background = false,
      .failure = 8U,
      .last_pixel = sentinel,
      .automatic = true,
      .zero_meter_mask = false,
    },
    Case {
      .name = "background partial alpha",
      .pixel = { sensitive, sensitive, sensitive, .5F },
      .ev = 0,
      .fp32 = true,
      .background = true,
      .failure = 8U,
      .last_pixel = sentinel,
      .automatic = true,
      .zero_meter_mask = false,
    },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto scene = scene::Scene("CheckedResolveCoverage", 1U);
    scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    scene.GetEnvironment()
      ->AddSystem<scene::environment::Background>()
      .SetEnabled(test_case.background);
    ctx_.scene = observer_ptr {
      &scene,
    };
    auto settings = scene::ExposureSettings {};
    settings.mode = test_case.automatic ? engine::ExposureMode::kAuto
                                        : engine::ExposureMode::kManual;
    settings.manual_ev = test_case.ev;
    settings.min_log_luminance = -24.0F;
    auto config = SharedConfig(settings);
    std::vector<Pixel> pixels(
      static_cast<std::size_t>(width) * height, ordinary);
    pixels.back() = test_case.pixel;
    const auto signal = MakeSignal(width, height, pixels);
    auto destination = CreateRegisteredTexture(TextureDesc {
      .width = width,
      .height = height,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "CheckedSceneColorDestination",
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon,
    });
    std::array<std::byte, static_cast<std::size_t>(height) * 256U> initial {};
    for (unsigned y = 0U; y < height; ++y) {
      for (unsigned x = 0U; x < width; ++x) {
        std::memcpy(
          std::span {
            initial,
          }
            .subspan(
              (static_cast<std::size_t>(y) * 256U) + (x * sizeof(PackedPixel)),
              sizeof(PackedPixel))
            .data(),
          sentinel.data(), sizeof(PackedPixel));
      }
    }
    auto upload = CreateUploadBuffer(SizeBytes {
      initial.size(),
    });
    upload->Update(initial.data(), initial.size(), 0U);
    {
      auto recorder = AcquireRecorder("Initialize checked resolve sentinel");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, destination, ResourceStates::kCommon);
      recorder->RequireResourceState(*destination, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        {
          .buffer_offset = 0U,
          .buffer_row_pitch = 256U,
          .buffer_slice_pitch = static_cast<std::uint64_t>(height) * 256U,
          .dst_slice = { .width = width, .height = height, .depth = 1U },
        },
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
          TextureViewDescription {
            .view_type = ResourceViewType::kTexture_UAV,
            .format = Format::kRGBA16Float,
            .dimension = TextureType::kTexture2D,
          })
        ->IsValid());
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = test_case.fp32;
    const auto frame = SubmitCommands("Vortex Exposure Frame",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->ResolveFrame(ctx_, recorder, config, frame_inputs);
      });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    observer_ptr<FrameCaptureController> capture;
    if (&test_case == &cases.front()) {
      capture = BeginOptionalCapture();
    }
    std::optional<Signal> zero_mask;
    if (test_case.zero_meter_mask) {
      zero_mask = Uniform(0.0F);
    }
    {
      auto exposure_inputs = postprocess::ExposurePass::Inputs {};
      exposure_inputs.scene_signal = signal.texture.get();
      exposure_inputs.scene_signal_srv = signal.srv;
      exposure_inputs.metering_mask
        = zero_mask ? zero_mask->texture.get() : nullptr;
      exposure_inputs.metering_mask_srv
        = zero_mask ? zero_mask->srv : kInvalidShaderVisibleIndex;
      ASSERT_TRUE(SubmitCommands("Vortex Checked SceneColor Conversion",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return pass_->ConvertCheckedSceneColor(
            ctx_, recorder, frame, config, exposure_inputs, *destination, uav);
        }));
    }
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
    const auto result = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(result.candidate_pre_exposure, test_case.fp32 ? 1.0F : 4.0F);
    EXPECT_EQ(result.expected_products, 1U << 10U);
    EXPECT_EQ(result.checked_products, result.expected_products);
    const bool accepted = test_case.failure == 0U;
    if (accepted) {
      EXPECT_EQ(result.failure_flags, 0U);
    } else {
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
    if (!mapped.has_value()) {
      FAIL() << "Expected mapped to contain a value";
    }
    const auto mapped_bytes = MappedTextureBytes(*mapped, sizeof(PackedPixel));
    for (unsigned y = 0U; y < height; ++y) {
      for (unsigned x = 0U; x < width; ++x) {
        PackedPixel actual {};
        std::memcpy(actual.data(),
          mapped_bytes
            .subspan((y * mapped->Layout().row_pitch.get())
                + (x * sizeof(PackedPixel)),
              sizeof(PackedPixel))
            .data(),
          sizeof(PackedPixel));
        auto expected_pixel = expected;
        if (!accepted) {
          expected_pixel = sentinel;
        } else if (y == height - 1U && x == width - 1U) {
          expected_pixel = test_case.last_pixel;
        }
        EXPECT_EQ(actual, expected_pixel);
      }
    }
    ctx_.scene = {};
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
    bool missing_certificate;
    bool bloom;
  };
  const std::array cases {
    Case {
      .name = "accepted half",
      .value = 1.0F / 3.0F,
      .ev = 0,
      .fp32 = true,
      .overflow = false,
      .expected = .333251953125F,
      .missing_certificate = false,
      .bloom = false,
    },
    Case {
      .name = "overflow fallback",
      .value = 1.0F / 3.0F,
      .ev = 0,
      .fp32 = true,
      .overflow = true,
      .expected = 1.0F / 3.0F,
      .missing_certificate = false,
      .bloom = false,
    },
    Case {
      .name = "dark loss fallback",
      .value = 0x1p-30F,
      .ev = -30,
      .fp32 = true,
      .overflow = false,
      .expected = 1.0F,
      .missing_certificate = false,
      .bloom = false,
    },
    Case {
      .name = "future candidate cannot approve current conversion",
      .value = 0x1p20F,
      .ev = 20,
      .fp32 = true,
      .overflow = false,
      .expected = 1.0F,
      .missing_certificate = false,
      .bloom = false,
    },
    Case {
      .name = "nonunit P",
      .value = 1.0F / 3.0F,
      .ev = -2,
      .fp32 = false,
      .overflow = false,
      .expected = .333251953125F,
      .missing_certificate = false,
      .bloom = false,
    },
    Case {
      .name = "missing certificate fallback",
      .value = 1.0F / 3.0F,
      .ev = 0,
      .fp32 = true,
      .overflow = false,
      .expected = 1.0F / 3.0F,
      .missing_certificate = true,
      .bloom = false,
    },
    Case {
      .name = "accepted half with external bloom",
      .value = 1.0F / 3.0F,
      .ev = 0,
      .fp32 = true,
      .overflow = false,
      .expected = .333251953125F + .0625F,
      .missing_certificate = false,
      .bloom = true,
    },
    Case {
      .name = "float fallback with external bloom",
      .value = 1.0F / 3.0F,
      .ev = 0,
      .fp32 = true,
      .overflow = true,
      .expected = (1.0F / 3.0F) + .0625F,
      .missing_certificate = false,
      .bloom = true,
    },
    Case {
      .name = "nonunit P with external bloom",
      .value = 1.0F / 3.0F,
      .ev = -2,
      .fp32 = false,
      .overflow = false,
      .expected = .333251953125F + .0625F,
      .missing_certificate = false,
      .bloom = true,
    },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto service = PostProcessService(*renderer_);
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = test_case.ev;
    settings.key = 12.5F;
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
    auto config = PostProcessConfig {
      .exposure = settings,
    };
    config.tone_mapper = engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    config.enable_bloom = test_case.bloom;
    config.bloom_intensity = test_case.bloom ? .5F : 0.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    std::ignore = service.SelectPrecisionCandidate(ctx_,
      {
        .product_layout_revision = 1U,
        .expected_products = 1024U,
      });
    const auto frame = SubmitCommands("Vortex Exposure Frame",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareFrameExposure(ctx_, recorder, test_case.fp32);
      });
    ASSERT_NE(frame, nullptr);
    std::array<Pixel, 16U> pixels {};
    pixels.fill(Pixel {
      test_case.value,
      test_case.value,
      test_case.value,
      1.0F,
    });
    if (test_case.overflow) {
      pixels.back() = Pixel {
        0x1p20F,
        0x1p20F,
        0x1p20F,
        1.0F,
      };
    }
    const auto accumulation = MakeSignal(4U, 4U, pixels);
    ctx_.current_view.frame_exposure = frame;
    if (!test_case.missing_certificate) {
      ASSERT_TRUE(SubmitCommands("Vortex Exposure PreEnvironment Range",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return service.CapturePreEnvironmentRange(
            ctx_, recorder, *accumulation.texture, accumulation.srv);
        }));
    }
    auto scene_inputs = PostProcessService::Inputs {};
    scene_inputs.scene_signal = accumulation.texture.get();
    scene_inputs.scene_signal_srv = accumulation.srv;
    const auto prepared = SubmitCommands(
      "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, scene_inputs);
      });
    if (!prepared.has_value()) {
      FAIL() << "Expected prepared to contain a value";
    }
    const auto before = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    auto destination = CreateRegisteredTexture(TextureDesc {
      .width = 4U,
      .height = 4U,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "CheckedTonemapHalf",
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon,
    });
    constexpr std::array<std::uint16_t, 4U> sentinel {
      0x3400U,
      0x3400U,
      0x3400U,
      0x3c00U,
    };
    std::array<std::byte, 1024U> initial {};
    for (unsigned y = 0U; y < 4U; ++y) {
      for (unsigned x = 0U; x < 4U; ++x) {
        std::memcpy(
          std::span {
            initial,
          }
            .subspan(
              (static_cast<std::size_t>(y) * 256U) + (x * sizeof(sentinel)),
              sizeof(sentinel))
            .data(),
          sentinel.data(), sizeof(sentinel));
      }
    }
    auto upload = CreateUploadBuffer(SizeBytes {
      initial.size(),
    });
    upload->Update(initial.data(), initial.size(), 0U);
    {
      auto recorder = AcquireRecorder("Initialize checked tonemap sentinel");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, destination, ResourceStates::kCommon);
      recorder->RequireResourceState(*destination, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        {
          .buffer_offset = 0U,
          .buffer_row_pitch = 256U,
          .buffer_slice_pitch = 1024U,
          .dst_slice = { .width = 4U, .height = 4U, .depth = 1U },
        },
        *destination);
      recorder->RequireResourceStateFinal(
        *destination, ResourceStates::kShaderResource);
    }
    const auto bind
      = [&](ResourceViewType type) -> bindless::ShaderVisibleIndex {
      auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
      auto allocation
        = allocator.AllocateRaw(type, DescriptorVisibility::kShaderVisible);
      const auto index = allocator.GetShaderVisibleIndex(allocation);
      CHECK_F(Backend()
          .GetResourceRegistry()
          .RegisterView(*destination, std::move(allocation),
            TextureViewDescription {
              .view_type = type,
              .format = Format::kRGBA16Float,
              .dimension = TextureType::kTexture2D,
            })
          ->IsValid());
      return index;
    };
    const auto uav = bind(ResourceViewType::kTexture_UAV);
    const Signal resolved {
      .texture = destination,
      .srv = bind(ResourceViewType::kTexture_SRV),
    };
    observer_ptr<FrameCaptureController> capture;
    if (test_case.overflow) {
      capture = BeginOptionalCapture();
    }
    const std::array products {
      postprocess::ExposurePass::HdrProduct {
        .texture = accumulation.texture.get(),
        .srv = accumulation.srv,
        .id = 11U,
        .metering = true,
        .composed_error = true,
      },
    };
    if (!test_case.missing_certificate) {
      ASSERT_TRUE(SubmitCommands("Vortex Exposure Suitability",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return service.PrepareScenePrecision(ctx_, recorder, *prepared,
            products, postprocess::ExposurePass::SceneComposition {});
        }));
    }
    {
      auto conversion_inputs = PostProcessService::Inputs {};
      conversion_inputs.scene_signal = accumulation.texture.get();
      conversion_inputs.scene_signal_srv = accumulation.srv;
      ASSERT_TRUE(SubmitCommands("Vortex Checked SceneColor Conversion",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return service.ConvertSceneColor(
            ctx_, recorder, *prepared, conversion_inputs, *destination, uav);
        }));
    }
    const auto converted_state = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                     &before,
                                     1,
                                   }),
      std::as_bytes(std::span {
        &converted_state,
        1,
      })));
    const auto conversion_before = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(SubmitCommands("Vortex FP16 Eligibility",
                [&](graphics::CommandRecorder& recorder) -> auto {
                  return service.FinalizeScenePrecision(
                    ctx_, recorder, *prepared);
                }),
      !test_case.missing_certificate);
    const auto finalized = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    const auto bloom = Uniform(
      static_cast<float>(.125
        * (test_case.fp32 ? std::exp2(static_cast<double>(test_case.ev))
                          : 1.0)),
      4U, 4U);
    {
      auto pixel_options = ServicePixelOptions {};
      pixel_options.start_new_frame = false;
      pixel_options.prepared = &*prepared;
      pixel_options.fallback = &accumulation;
      pixel_options.checked_resolution = frame;
      pixel_options.bloom = test_case.bloom ? &bloom : nullptr;
      pixel_options.bloom_intensity = test_case.bloom ? .5F : 0.0F;
      EXPECT_NEAR(ServicePixel(service, resolved, settings, pixel_options),
        test_case.expected, 1e-7F);
    }
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
    const auto report = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                     &conversion_before,
                                     1,
                                   }),
      std::as_bytes(std::span {
        &report,
        1,
      })));
    const bool rejected = test_case.overflow || test_case.value == 0x1p-30F
      || test_case.value == 0x1p20F || test_case.missing_certificate;
    EXPECT_EQ(report.failure_flags != 0U, rejected);
    if (!test_case.fp32 && rejected) {
      EXPECT_EQ(finalized.fp16_eligible_streak, 0U);
    }
    if (test_case.fp32 && test_case.overflow) {
      EXPECT_EQ(finalized.fp16_eligible_streak, 1U);
    }
    const auto after = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                     &finalized,
                                     1,
                                   }),
      std::as_bytes(std::span {
        &after,
        1,
      })));
    const auto* bindings = service.InspectBindings(ctx_.current_view.view_id);
    ASSERT_NE(bindings, nullptr);
    EXPECT_EQ(bindings->scene_fallback_srv, accumulation.srv);
    EXPECT_EQ(bindings->conversion_report_srv, frame->conversion_srv);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, RecycledPoisonedHalfRejectsConversionAndUsesOriginalFloat)
{
  auto pool = vortex::internal::RetainedTexturePool(GetGraphicsShared());
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  pool.OnFrameStart(ctx_.frame_sequence);
  auto& reclaimer = Backend().GetDeferredReclaimer();
  reclaimer.OnBeginFrame(ctx_.frame_slot);
  const auto desc = TextureDesc {
    .width = 4U,
    .height = 4U,
    .format = Format::kRGBA16Float,
    .texture_type = TextureType::kTexture2D,
    .debug_name = "RecycledCheckedTonemapHalf",
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon,
  };
  auto destination = pool.Acquire(ctx_.current_view.view_id, desc, true);
  ASSERT_NE(destination, nullptr);
  constexpr std::array<std::uint16_t, 4U> poison {
    0x3400U,
    0x3400U,
    0x3400U,
    0x3c00U,
  };
  std::array<std::byte, 1024U> initial {};
  for (unsigned y = 0U; y < 4U; ++y) {
    for (unsigned x = 0U; x < 4U; ++x) {
      std::memcpy(
        std::span {
          initial,
        }
          .subspan((static_cast<std::size_t>(y) * 256U) + (x * sizeof(poison)),
            sizeof(poison))
          .data(),
        poison.data(), sizeof(poison));
    }
  }
  auto upload = CreateUploadBuffer(SizeBytes {
    initial.size(),
  });
  upload->Update(initial.data(), initial.size(), 0U);
  {
    auto recorder = AcquireRecorder("Poison retained half before recycling");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    // The pool owns registration. The fixture's EnsureTracked(texture) would
    // retain an additional reader and prevent this deliberate reuse.
    recorder->BeginTrackingResourceState(*destination, ResourceStates::kCommon);
    recorder->RequireResourceState(*destination, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      {
        .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 1024U,
        .dst_slice = { .width = 4U, .height = 4U, .depth = 1U },
      },
      *destination);
    recorder->RequireResourceStateFinal(
      *destination, ResourceStates::kCopySource);
  }
  WaitForQueueIdle();
  const auto native = destination->GetNativeResource();
  auto* const physical_identity = destination.get();
  const std::weak_ptr<Texture> physical = destination->shared_from_this();
  ASSERT_EQ(
    Backend().TryGetKnownResourceState(native), ResourceStates::kCopySource);
  destination.reset();
  reclaimer.OnBeginFrame(ctx_.frame_slot);
  ASSERT_FALSE(physical.expired());
  ASSERT_FALSE(Backend().GetResourceRegistry().Contains(*physical.lock()));
  ASSERT_FALSE(GetQueue()->TryGetKnownResourceState(native).has_value());
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  pool.OnFrameStart(ctx_.frame_sequence);
  destination = pool.Acquire(ctx_.current_view.view_id, desc, true);
  ASSERT_EQ(destination.get(), physical_identity);
  ASSERT_EQ(
    GetQueue()->TryGetKnownResourceState(native), ResourceStates::kCopySource);

  auto service = PostProcessService(*renderer_);
  service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  settings.key = 12.5F;
  [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
  auto config = PostProcessConfig {
    .exposure = settings,
  };
  config.tone_mapper = engine::ToneMapper::kNone;
  config.gamma = 1.0F;
  config.enable_bloom = false;
  config.bloom_intensity = 0.0F;
  service.SetResolvedConfig(service.BuildPassConfig(
    config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
  std::ignore = service.SelectPrecisionCandidate(ctx_,
    {
      .product_layout_revision = 1U,
      .expected_products = 1024U,
    });
  const auto frame = SubmitCommands(
    "Vortex Exposure Frame", [&](graphics::CommandRecorder& recorder) -> auto {
      return service.PrepareFrameExposure(ctx_, recorder, true);
    });
  ASSERT_NE(frame, nullptr);
  ctx_.current_view.frame_exposure = frame;
  std::array<Pixel, 16U> pixels {};
  pixels.fill(Pixel {
    1.0F / 3.0F,
    1.0F / 3.0F,
    1.0F / 3.0F,
    1.0F,
  });
  pixels.back() = Pixel {
    0x1p20F,
    0x1p20F,
    0x1p20F,
    1.0F,
  };
  const auto accumulation = MakeSignal(4U, 4U, pixels);
  ASSERT_TRUE(SubmitCommands("Vortex Exposure PreEnvironment Range",
    [&](graphics::CommandRecorder& recorder) -> auto {
      return service.CapturePreEnvironmentRange(
        ctx_, recorder, *accumulation.texture, accumulation.srv);
    }));
  auto prepared_inputs = PostProcessService::Inputs {};
  prepared_inputs.scene_signal = accumulation.texture.get();
  prepared_inputs.scene_signal_srv = accumulation.srv;
  const auto prepared = SubmitCommands(
    "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
      return service.PrepareSceneExposure(
        ctx_.current_view.view_id, ctx_, recorder, prepared_inputs);
    });
  if (!prepared.has_value()) {
    FAIL() << "Expected prepared to contain a value";
  }
  const auto solved = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(solved.displayed_scale, 1.0F);
  const auto domain
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.pre_exposure, 1.0F);
  EXPECT_EQ(domain.one_over_pre_exposure, 1.0F);
  const auto bind
    = [&](const ResourceViewType type) -> bindless::ShaderVisibleIndex {
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto allocation
      = allocator.AllocateRaw(type, DescriptorVisibility::kShaderVisible);
    const auto index = allocator.GetShaderVisibleIndex(allocation);
    CHECK_F(Backend()
        .GetResourceRegistry()
        .RegisterView(*destination, std::move(allocation),
          TextureViewDescription {
            .view_type = type,
            .format = Format::kRGBA16Float,
            .dimension = TextureType::kTexture2D,
          })
        ->IsValid());
    return index;
  };
  const auto uav = bind(ResourceViewType::kTexture_UAV);
  const Signal resolved {
    .texture = destination,
    .srv = bind(ResourceViewType::kTexture_SRV),
  };
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = accumulation.texture.get(),
      .srv = accumulation.srv,
      .id = 11U,
      .metering = true,
      .composed_error = true,
    },
  };
  ASSERT_TRUE(SubmitCommands("Vortex Exposure Suitability",
    [&](graphics::CommandRecorder& recorder) -> auto {
      return service.PrepareScenePrecision(ctx_, recorder, *prepared, products,
        postprocess::ExposurePass::SceneComposition {});
    }));
  {
    auto scene_inputs = PostProcessService::Inputs {};
    scene_inputs.scene_signal = accumulation.texture.get();
    scene_inputs.scene_signal_srv = accumulation.srv;
    ASSERT_TRUE(SubmitCommands("Vortex Checked SceneColor Conversion",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return service.ConvertSceneColor(
          ctx_, recorder, *prepared, scene_inputs, *destination, uav);
      }));
  }
  EXPECT_EQ(GetQueue()->TryGetKnownResourceState(native),
    ResourceStates::kShaderResource);
  const auto converted = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                   &solved,
                                   1,
                                 }),
    std::as_bytes(std::span {
      &converted,
      1,
    })));
  const auto report = Read<HdrSuitabilityData>(
    *frame->conversion_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
  EXPECT_EQ(report.expected_products, 1024U);
  EXPECT_EQ(report.checked_products, 1024U);
  // Composed-scene qualification rejects an unrepresentable interval before
  // the point-sample overflow path. Only the deliberately oversized texel
  // must fail; rejection of every texel would hide an invalid certificate.
  EXPECT_EQ(report.failure_flags, 16U);
  EXPECT_EQ(report.image_failures, 1U);
  EXPECT_EQ(report.checked_samples, 16U);
  EXPECT_EQ(report.first_failure_product, 11U);
  ASSERT_TRUE(SubmitCommands("Vortex FP16 Eligibility",
    [&](graphics::CommandRecorder& recorder) -> auto {
      return service.FinalizeScenePrecision(ctx_, recorder, *prepared);
    }));
  const auto finalized = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);

  {
    // Rejection must preserve the poisoned contents across the entire image.
    // The visible result below must therefore come from the original FP32
    // input.
    {
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Recycled half poison");
      {
        auto recorder = AcquireRecorder("Read rejected recycled half");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*destination));
        ASSERT_TRUE(
          readback->EnqueueCopy(*recorder, *destination, {}).has_value());
      }
      const auto mapped = readback->MapNow();
      if (!mapped.has_value()) {
        FAIL() << "Expected mapped to contain a value";
      }
      const auto mapped_bytes = MappedTextureBytes(*mapped, sizeof(poison));
      for (unsigned y = 0U; y < 4U; ++y) {
        for (unsigned x = 0U; x < 4U; ++x) {
          std::array<std::uint16_t, 4U> actual {};
          std::memcpy(actual.data(),
            mapped_bytes
              .subspan(
                (y * mapped->Layout().row_pitch.get()) + (x * sizeof(poison)),
                sizeof(actual))
              .data(),
            sizeof(actual));
          EXPECT_EQ(actual, poison);
        }
      }
    }
    auto pixel_options = ServicePixelOptions {};
    pixel_options.start_new_frame = false;
    pixel_options.prepared = &*prepared;
    pixel_options.fallback = &accumulation;
    pixel_options.checked_resolution = frame;
    EXPECT_NEAR(ServicePixel(service, resolved, settings, pixel_options),
      1.0F / 3.0F, 1e-7F)
      << "Consuming the stale half texture would return 0.25 instead";
  }
  const auto after = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                   &finalized,
                                   1,
                                 }),
    std::as_bytes(std::span {
      &after,
      1,
    })));
  const auto report_after = Read<HdrSuitabilityData>(
    *frame->conversion_buffer, ResourceStates::kShaderResource);
  EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                   &report,
                                   1,
                                 }),
    std::as_bytes(std::span {
      &report_after,
      1,
    })));
  const auto domain_after
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                   &domain,
                                   1,
                                 }),
    std::as_bytes(std::span {
      &domain_after,
      1,
    })));
  const auto* bindings = service.InspectBindings(ctx_.current_view.view_id);
  ASSERT_NE(bindings, nullptr);
  EXPECT_EQ(bindings->scene_fallback_srv, accumulation.srv);
  EXPECT_EQ(bindings->conversion_report_srv, frame->conversion_srv);
}

NOLINT_TEST_F(
  ExposureGpuTest, CheckedConversionWaitsForInitialMeteringMaskPolicy)
{
  for (const bool pending : {
         true,
         false,
       }) {
    SCOPED_TRACE(pending);
    auto loader = vortex::testing::FakeAssetLoader {};
    auto service = PostProcessService(*renderer_,
      pending ? observer_ptr { &loader, }
              : observer_ptr<vortex::testing::FakeAssetLoader> {});
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    auto requested = scene::ExposureSettings {};
    requested.metering_mask = pending ? loader.MintSyntheticTextureKey()
                                      : content::ResourceKey {
                                          123U,
                                        };
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
    ASSERT_NE(SubmitCommands("Vortex Exposure Frame",
                [&](graphics::CommandRecorder& recorder) -> auto {
                  return service.PrepareFrameExposure(ctx_, recorder, true);
                }),
      nullptr);
    const auto source = Uniform(.25F, 4U, 4U);
    auto inputs = PostProcessService::Inputs {};
    inputs.scene_signal = source.texture.get();
    inputs.scene_signal_srv = source.srv;
    const auto prepared = SubmitCommands(
      "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, inputs);
      });
    if (!prepared.has_value()) {
      FAIL() << "Expected prepared to contain a value";
    }
    auto destination = CreateRegisteredTexture(TextureDesc {
      .width = 4U,
      .height = 4U,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon,
    });
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto allocation = allocator.AllocateRaw(
      ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
    const auto index = allocator.GetShaderVisibleIndex(allocation);
    ASSERT_TRUE(Backend()
        .GetResourceRegistry()
        .RegisterView(*destination, std::move(allocation),
          TextureViewDescription {
            .view_type = ResourceViewType::kTexture_UAV,
            .format = Format::kRGBA16Float,
            .dimension = TextureType::kTexture2D,
          })
        ->IsValid());
    auto& backend = FailureBackend();
    auto recording = AcquireRecorder("Rejected conversion",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    backend.recorder_names.clear();
    EXPECT_FALSE(service.ConvertSceneColor(
      ctx_, *recording, *prepared, inputs, *destination, index));
    EXPECT_FALSE(recording->IsResourceTracked(*destination));
    EXPECT_TRUE(backend.recorder_names.empty());
  }
}

} // namespace oxygen::vortex::testing::exposure
