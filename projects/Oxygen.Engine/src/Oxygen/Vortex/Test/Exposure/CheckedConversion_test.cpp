//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

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
    bool zero_meter_mask { false };
  };
  const std::array cases {
    Case { "ordinary", ordinary, 0, true, false, 0U, expected },
    Case { "typed half rounding",
      { 1.500732421875F, 1.500244140625F, 1.50048828125F, 1.0F }, 0, true,
      false, 0U, { 0x3e01U, 0x3e00U, 0x3e00U, 0x3c00U } },
    Case { "odd tie and exponent boundary",
      { 1.50146484375F, 8198.0F, 1.99951171875F, 1.0F }, 0, true, false, 0U,
      { 0x3e02U, 0x7001U, 0x4000U, 0x3c00U } },
    Case { "subnormal nearest even",
      { 1.75F * 0x1p-24F, 2.5F * 0x1p-24F, 3.5F * 0x1p-24F, 0.0F }, 0, true,
      false, 0U, { 2U, 2U, 4U, 0U }, false, true },
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
    for (unsigned y = 0U; y < height; ++y) {
      for (unsigned x = 0U; x < width; ++x) {
        std::memcpy(initial.data() + y * 256U + x * sizeof(PackedPixel),
          sentinel.data(), sizeof(PackedPixel));
      }
    }
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
    const auto zero_mask = test_case.zero_meter_mask
      ? std::optional<Signal> { Uniform(0.0F) }
      : std::nullopt;
    ASSERT_TRUE(pass_->ConvertCheckedSceneColor(ctx_, frame, config,
      { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .metering_mask = zero_mask ? zero_mask->texture.get() : nullptr,
        .metering_mask_srv
        = zero_mask ? zero_mask->srv : kInvalidShaderVisibleIndex },
      *destination, uav));
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
    ASSERT_TRUE(mapped.has_value());
    for (unsigned y = 0U; y < height; ++y) {
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
    bool missing_certificate { false };
    bool bloom { false };
  };
  const std::array cases {
    Case { "accepted half", 1.0F / 3.0F, 0, true, false, .333251953125F },
    Case { "overflow fallback", 1.0F / 3.0F, 0, true, true, 1.0F / 3.0F },
    Case { "dark loss fallback", 0x1p-30F, -30, true, false, 1.0F },
    Case { "future candidate cannot approve current conversion", 0x1p20F, 20,
      true, false, 1.0F },
    Case { "nonunit P", 1.0F / 3.0F, -2, false, false, .333251953125F },
    Case { "missing certificate fallback", 1.0F / 3.0F, 0, true, false,
      1.0F / 3.0F, true },
    Case { "accepted half with external bloom", 1.0F / 3.0F, 0, true, false,
      .333251953125F + .0625F, false, true },
    Case { "float fallback with external bloom", 1.0F / 3.0F, 0, true, true,
      1.0F / 3.0F + .0625F, false, true },
    Case { "nonunit P with external bloom", 1.0F / 3.0F, -2, false, false,
      .333251953125F + .0625F, false, true },
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
    config.enable_bloom = test_case.bloom;
    config.bloom_intensity = test_case.bloom ? .5F : 0.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    static_cast<void>(service.SelectPrecisionCandidate(
      ctx_, { .product_layout_revision = 1U, .expected_products = 1024U }));
    const auto frame = service.PrepareFrameExposure(ctx_, test_case.fp32);
    ASSERT_NE(frame, nullptr);
    std::array<Pixel, 16U> pixels;
    pixels.fill(
      Pixel { test_case.value, test_case.value, test_case.value, 1.0F });
    if (test_case.overflow) {
      pixels.back() = Pixel { 0x1p20F, 0x1p20F, 0x1p20F, 1.0F };
    }
    const auto accumulation = MakeSignal(4U, 4U, pixels);
    ctx_.current_view.frame_exposure = frame;
    if (!test_case.missing_certificate) {
      ASSERT_TRUE(service.CapturePreEnvironmentRange(
        ctx_, *accumulation.texture, accumulation.srv));
    }
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
    for (unsigned y = 0U; y < 4U; ++y) {
      for (unsigned x = 0U; x < 4U; ++x) {
        std::memcpy(initial.data() + y * 256U + x * sizeof(sentinel),
          sentinel.data(), sizeof(sentinel));
      }
    }
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
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = accumulation.texture.get(),
      .srv = accumulation.srv,
      .id = 11U,
      .metering = true,
      .composed_error = true } };
    if (!test_case.missing_certificate) {
      ASSERT_TRUE(service.PrepareScenePrecision(ctx_, *prepared, products,
        postprocess::ExposurePass::SceneComposition {}));
    }
    ASSERT_TRUE(service.ConvertSceneColor(ctx_, *prepared,
      { .scene_signal = accumulation.texture.get(),
        .scene_signal_srv = accumulation.srv },
      *destination, uav));
    const auto converted_state = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&before, &converted_state, sizeof(before)), 0);
    const auto conversion_before = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(service.FinalizeScenePrecision(ctx_, *prepared),
      !test_case.missing_certificate);
    const auto finalized = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    const auto bloom = Uniform(
      float(.125 * (test_case.fp32 ? std::exp2(double(test_case.ev)) : 1.0)),
      4U, 4U);
    EXPECT_NEAR(ServicePixel(service, resolved, settings,
                  ServicePixelOptions { .start_new_frame = false,
                    .prepared = &*prepared,
                    .fallback = &accumulation,
                    .checked_resolution = frame,
                    .bloom = test_case.bloom ? &bloom : nullptr,
                    .bloom_intensity = test_case.bloom ? .5F : 0.0F }),
      test_case.expected, 1e-7F);
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
    const auto report = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&conversion_before, &report, sizeof(report)), 0);
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
    EXPECT_EQ(std::memcmp(&finalized, &after, sizeof(finalized)), 0);
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
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  pool.OnFrameStart(ctx_.frame_sequence);
  auto& reclaimer = Backend().GetDeferredReclaimer();
  reclaimer.OnBeginFrame(ctx_.frame_slot);
  const auto desc = TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA16Float,
    .texture_type = TextureType::kTexture2D,
    .debug_name = "RecycledCheckedTonemapHalf",
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon };
  auto destination = pool.Acquire(ctx_.current_view.view_id, desc, true);
  ASSERT_NE(destination, nullptr);
  constexpr std::array<std::uint16_t, 4U> poison { 0x3400U, 0x3400U, 0x3400U,
    0x3c00U };
  std::array<std::byte, 1024U> initial {};
  for (unsigned y = 0U; y < 4U; ++y) {
    for (unsigned x = 0U; x < 4U; ++x) {
      std::memcpy(initial.data() + y * 256U + x * sizeof(poison), poison.data(),
        sizeof(poison));
    }
  }
  auto upload = CreateUploadBuffer(SizeBytes { initial.size() });
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
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 1024U,
        .dst_slice = { .width = 4U, .height = 4U, .depth = 1U } },
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
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
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
  auto config = PostProcessConfig { .exposure = settings };
  config.tone_mapper = engine::ToneMapper::kNone;
  config.gamma = 1.0F;
  config.enable_bloom = false;
  config.bloom_intensity = 0.0F;
  service.SetResolvedConfig(service.BuildPassConfig(
    config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
  static_cast<void>(service.SelectPrecisionCandidate(
    ctx_, { .product_layout_revision = 1U, .expected_products = 1024U }));
  const auto frame = service.PrepareFrameExposure(ctx_, true);
  ASSERT_NE(frame, nullptr);
  ctx_.current_view.frame_exposure = frame;
  std::array<Pixel, 16U> pixels;
  pixels.fill(Pixel { 1.0F / 3.0F, 1.0F / 3.0F, 1.0F / 3.0F, 1.0F });
  pixels.back() = Pixel { 0x1p20F, 0x1p20F, 0x1p20F, 1.0F };
  const auto accumulation = MakeSignal(4U, 4U, pixels);
  ASSERT_TRUE(service.CapturePreEnvironmentRange(
    ctx_, *accumulation.texture, accumulation.srv));
  const auto prepared
    = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = accumulation.texture.get(),
        .scene_signal_srv = accumulation.srv });
  ASSERT_TRUE(prepared.has_value());
  const auto solved = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(solved.displayed_scale, 1.0F);
  const auto domain
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.pre_exposure, 1.0F);
  EXPECT_EQ(domain.one_over_pre_exposure, 1.0F);
  const auto bind = [&](const ResourceViewType type) {
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
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = accumulation.texture.get(),
    .srv = accumulation.srv,
    .id = 11U,
    .metering = true,
    .composed_error = true } };
  ASSERT_TRUE(service.PrepareScenePrecision(
    ctx_, *prepared, products, postprocess::ExposurePass::SceneComposition {}));
  ASSERT_TRUE(service.ConvertSceneColor(ctx_, *prepared,
    { .scene_signal = accumulation.texture.get(),
      .scene_signal_srv = accumulation.srv },
    *destination, uav));
  EXPECT_EQ(GetQueue()->TryGetKnownResourceState(native),
    ResourceStates::kShaderResource);
  const auto converted = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&solved, &converted, sizeof(solved)), 0);
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
  ASSERT_TRUE(service.FinalizeScenePrecision(ctx_, *prepared));
  const auto finalized = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);

  // Rejection must preserve the poisoned contents across the entire image.
  // The visible result below must therefore come from the original FP32 input.
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
    ASSERT_TRUE(mapped.has_value());
    for (unsigned y = 0U; y < 4U; ++y) {
      for (unsigned x = 0U; x < 4U; ++x) {
        std::array<std::uint16_t, 4U> actual {};
        std::memcpy(actual.data(),
          mapped->Data() + y * mapped->Layout().row_pitch.get()
            + x * sizeof(poison),
          sizeof(actual));
        EXPECT_EQ(actual, poison);
      }
    }
  }
  EXPECT_NEAR(ServicePixel(service, resolved, settings,
                ServicePixelOptions { .start_new_frame = false,
                  .prepared = &*prepared,
                  .fallback = &accumulation,
                  .checked_resolution = frame }),
    1.0F / 3.0F, 1e-7F)
    << "Consuming the stale half texture would return 0.25 instead";
  const auto after = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&finalized, &after, sizeof(finalized)), 0);
  const auto report_after = Read<HdrSuitabilityData>(
    *frame->conversion_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&report, &report_after, sizeof(report)), 0);
  const auto domain_after
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&domain, &domain_after, sizeof(domain)), 0);
  const auto* bindings = service.InspectBindings(ctx_.current_view.view_id);
  ASSERT_NE(bindings, nullptr);
  EXPECT_EQ(bindings->scene_fallback_srv, accumulation.srv);
  EXPECT_EQ(bindings->conversion_report_srv, frame->conversion_srv);
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

} // namespace oxygen::vortex::testing::exposure
