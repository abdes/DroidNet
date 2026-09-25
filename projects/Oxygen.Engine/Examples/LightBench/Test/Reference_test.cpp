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
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>

#include "LightBench/LightBenchSettings.h"
#include "LightBench/LightScene.h"
#include "LightBench/ReferenceScene.h"
#include <d3d12.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/SceneSync/SceneObserverSyncModule.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::examples::light_bench::testing {
namespace {
  namespace oracle = vortex::testing::reference;
  class LightBenchReferenceTest
    : public vortex::testing::exposure::ExposureLightingGpuTest {
  protected:
    LightScene bench;
    static constexpr std::uint32_t kWidth = 384, kHeight = 216;
    auto SetUp() -> void override
    {
      ExposureLightingGpuTest::SetUp();
      renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
        vortex::HdrPrecisionControl::kProduction);
      renderer_->SetGroundGridConfig({ .enabled = false });
      scene = bench.CreateScene();
      frame.SetScene(observer_ptr { scene.get() });
      bench.SetScene(observer_ptr { scene.get() });
      bench.Update();
      camera = LightScene::CreateReferenceCamera(*scene);
      settings = scene->GetEnvironment()
                   ->TryGetSystem<scene::environment::PostProcessVolume>()
                   ->GetExposureSettings();
      view.viewport.width = kWidth;
      view.viewport.height = kHeight;
      auto output = CreateRegisteredTexture({
        .width = kWidth,
        .height = kHeight,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = graphics::ResourceStates::kCommon,
      });
      framebuffer = Backend().CreateFramebuffer(
        graphics::FramebufferDesc {}.AddColorAttachment(output));
      expected_draws = 3;
      verify_manual_p
        = false; // Production may choose P=1 independently of gain.
      probe->prepare = [](vortex::RenderContext&) -> void { };
    }
    auto ReadDepth(const graphics::Texture& depth) -> std::vector<float>
    {
      // Reuse the native depth-plane readback approach from EX05's queued
      // consumer fixture. Generic texture readback excludes stencil formats.
      auto* source = depth.GetNativeResource()->AsPointer<ID3D12Resource>();
      const auto desc = source->GetDesc();
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
      UINT rows = 0;
      UINT64 row_bytes = 0, bytes = 0;
      Backend().GetCurrentDevice()->GetCopyableFootprints(
        &desc, 0, 1, 0, &footprint, &rows, &row_bytes, &bytes);
      auto readback
        = CreateReadbackBuffer(SizeBytes { bytes }, "LightBench depth plane");
      {
        auto recorder = AcquireRecorder("LightBench depth plane");
        CHECK_F(recorder->AdoptKnownResourceState(depth));
        recorder->RequireResourceState(
          depth, graphics::ResourceStates::kCopySource);
        EnsureTracked(*recorder, readback, graphics::ResourceStates::kCopyDest);
        recorder->FlushBarriers();
        D3D12_TEXTURE_COPY_LOCATION from {};
        from.pResource = source;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to {};
        to.pResource
          = readback->GetNativeResource()->AsPointer<ID3D12Resource>();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = footprint;
        const auto recording = recorder->GetCommandListForInspection();
        // Native fixture backend is D3D12; RTTI is disabled.
        const auto* native
          = static_cast<const graphics::d3d12::CommandList*>(recording.get());
        native->GetCommandList()->CopyTextureRegion(
          &to, 0, 0, 0, &from, nullptr);
        recorder->RequireResourceStateFinal(
          depth, graphics::ResourceStates::kShaderResource);
      }
      WaitForQueueIdle();
      const auto data
        = std::span(static_cast<const std::byte*>(readback->Map()), bytes);
      const auto width = footprint.Footprint.Width;
      const auto stride = row_bytes / width;
      std::vector<float> result(static_cast<std::size_t>(width) * rows);
      for (std::size_t y = 0; y < rows; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const auto pixel = data.subspan(footprint.Offset
              + (y * footprint.Footprint.RowPitch) + (x * stride),
            sizeof(float));
          std::memcpy(&result.at(y * width + x), pixel.data(), sizeof(float));
        }
      readback->UnMap();
      return result;
    }

    auto TearDown() -> void override
    {
      bench.ClearScene();
      ExposureLightingGpuTest::TearDown();
    }
  };

  auto Expected(double albedo, double f0) -> double
  {
    // Independent EX07 integrator, not the production energy texture.
    const auto moments = oracle::IntegrateGgxMoments(
      oracle::PerceptualRoughness { 1.0 }, oracle::ViewCosine { 1.0 });
    if (!moments) {
      throw std::runtime_error("Independent rough-card integration failed");
    }
    std::array<oracle::BrdfReflectance, 3> material;
    material.fill({ .f0 = f0, .diffuse = albedo });
    const auto response = oracle::EvaluateRasterGgxBrdf({}, material, *moments);
    if (!response) {
      throw std::runtime_error("Independent rough-card BRDF failed");
    }
    const auto& lobe = response->front();
    return 1000.0
      * (lobe.single_scattering + lobe.multiple_scattering + lobe.diffuse);
  }
} // namespace

NOLINT_TEST(
  LightBenchReference, ExposureIsDerivedFromIndependentMaterialResponse)
{
  for (std::size_t card = 0; card < 3; ++card) {
    EXPECT_NEAR(Expected(reference::kReflectances[card], .04),
      reference::kExpectedLuminance[card],
      2e-5 * reference::kExpectedLuminance[card]);
  }
  EXPECT_NEAR(
    reference::kExposureEv, std::log2(Expected(.18, .04) / .18), 1e-6);
}

NOLINT_TEST_F(
  LightBenchReferenceTest, CanonicalCardsMatchProductionLightingAndOutput)
{
  for (const bool forward : { false, true }) {
    SCOPED_TRACE(forward ? "forward" : "deferred");
    RenderSurface(forward, reference::kExposureEv);
    ASSERT_NE(probe->color, nullptr);
    const auto hdr = ReadFloatTexture(*probe->color, true);
    const auto output = ReadFloatTexture(
      *framebuffer->GetDescriptor().color_attachments.front().texture);
    const auto domain = Read<vortex::FrameExposureData>(
      *probe->exposure->buffer, graphics::ResourceStates::kShaderResource);
    ASSERT_GT(domain.pre_exposure, 0);
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* post
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    RecordProperty(forward ? "forward_tone" : "deferred_tone",
      static_cast<int>(post->GetConfig().tone_mapper));
    RecordProperty(
      forward ? "forward_gamma" : "deferred_gamma", post->GetConfig().gamma);
    const auto gain_state
      = Read<vortex::ExposureStateData>(*probe->exposure->current_state->buffer,
        graphics::ResourceStates::kShaderResource);
    RecordProperty(
      forward ? "forward_gain" : "deferred_gain", gain_state.displayed_scale);
    const double gain = std::exp2(-double(reference::kExposureEv));
    // Inset 16x16 regions around projected card centres. Entire regions must be
    // foreground; surrounding background cannot count as a dark card.
    for (std::size_t card = 0; card < 3; ++card) {
      const double authored = reference::kReflectances[card];
      double expected = Expected(authored, .04);
      double packing_bound = 0;
      if (!forward) {
        const double encoded = authored <= .0031308
          ? 12.92 * authored
          : (1.055 * std::pow(authored, 1.0 / 2.4)) - .055;
        // The two nearest codes enclose hardware sRGB storage conversion;
        // SRGB sampling permits another half code of encoded-side error
        // (the existing EX07 MaterialDecodeGpu contract). Specular=0.5
        // also lies between two UNorm8 codes. Keep these separate
        // from the unchanged shader arithmetic budget.
        for (double code :
          { std::floor(encoded * 255) - .5, std::ceil(encoded * 255) + .5 }) {
          for (double specular : { 127.0 / 255, 128.0 / 255 }) {
            const auto packed
              = Expected((code / 255.0 <= .04045
                             ? code / 255.0 / 12.92
                             : std::pow(((code / 255.0) + .055) / 1.055, 2.4)),
                .08 * specular);
            packing_bound
              = std::max(packing_bound, std::abs(packed - expected));
          }
        }
      }
      // The card offsets remain +/-1.6 m; framing may change independently.
      const auto center_x = static_cast<std::uint32_t>(kWidth
        * (.5
          + (static_cast<double>(card) - 1.0) * 1.6
            / reference::kMinimumViewWidth));
      std::uint32_t foreground = 0;
      double total = 0;
      double maximum_radiance_error = 0;
      double maximum_output_error = 0;
      bool all_finite = true;
      std::string worst;
      for (std::uint32_t y = (kHeight / 2) - 8; y < (kHeight / 2) + 8; ++y) {
        for (std::uint32_t x = center_x - 8; x < center_x + 8; ++x) {
          const auto offset = (y * kWidth) + x;
          const auto& pixel = hdr.at(offset);
          if (pixel[3] >= .999F) {
            ++foreground;
          }
          for (std::size_t c = 0; c < 3; ++c) {
            const double luminance = pixel[c] / domain.pre_exposure;
            all_finite = all_finite && std::isfinite(luminance);
            maximum_radiance_error = std::max(
              maximum_radiance_error, std::abs(luminance - expected));
            const double mapped
              = std::pow(std::clamp(luminance * gain, 0.0, 1.0),
                1.0 / reference::kDisplayGamma);
            constexpr std::array<int, 16> bayer {
              0,
              8,
              2,
              10,
              12,
              4,
              14,
              6,
              3,
              11,
              1,
              9,
              15,
              7,
              13,
              5,
            };
            const double dither
              = ((bayer[((y % 4) * 4) + (x % 4)] / 16.0) - .5) / 255;
            all_finite = all_finite && std::isfinite(output.at(offset)[c]);
            const auto output_error = std::abs(
              output.at(offset)[c] - std::clamp(mapped + dither, 0.0, 1.0));
            if (output_error > maximum_output_error) {
              maximum_output_error = output_error;
              worst = std::to_string(x) + "," + std::to_string(y) + ","
                + std::to_string(c)
                + " actual=" + std::to_string(output.at(offset)[c])
                + " expected=" + std::to_string(mapped + dither);
            }
          }
          total += pixel[0] / domain.pre_exposure;
        }
      }
      RecordProperty(
        std::string(forward ? "forward_output_" : "deferred_output_")
          + std::to_string(card),
        output.at(((kHeight / 2) * kWidth) + center_x)[0]);
      RecordProperty(std::string(forward ? "forward_worst_" : "deferred_worst_")
          + std::to_string(card),
        worst);
      if (!forward) {
        auto packed = owner->GetSceneTextures().GetGBufferResource(
          vortex::GBufferIndex::kBaseColor);
        auto rb = GetReadbackManager()->CreateTextureReadback(
          "LightBench packed reference");
        {
          auto recorder = AcquireRecorder("LightBench packed reference");
          ASSERT_TRUE(recorder->AdoptKnownResourceState(*packed));
          ASSERT_TRUE(rb->EnqueueCopy(*recorder, *packed,
            { .src_slice = { .x = center_x,
                .y = kHeight / 2,
                .width = 1,
                .height = 1,
                .depth = 1 } }));
        }
        const auto bytes = rb->MapNow();
        ASSERT_TRUE(bytes);
        const auto code = std::to_integer<unsigned>(bytes->Data()[0]);
        RecordProperty("deferred_code_" + std::to_string(card), code);
      }
      const auto depth = owner->GetSceneTextures().GetSceneDepthResource();
      ASSERT_NE(depth, nullptr);
      ASSERT_EQ(depth->GetDescriptor().format, Format::kDepth32Stencil8);
      const auto depth_pixels = ReadDepth(*depth);
      double maximum_depth_error = 0;
      for (std::uint32_t y = kHeight / 2 - 8; y < kHeight / 2 + 8; ++y) {
        for (std::uint32_t x = center_x - 8; x < center_x + 8; ++x) {
          const auto z = depth_pixels.at((y * kWidth) + x);
          ASSERT_TRUE(std::isfinite(z));
          maximum_depth_error = std::max(
            maximum_depth_error, std::abs(z - ((100.0 - 6.0) / (100.0 - .1))));
        }
      }
      EXPECT_LE(maximum_depth_error, 1e-6);
      SCOPED_TRACE(card);
      EXPECT_EQ(foreground, 256U);
      EXPECT_TRUE(all_finite);
      EXPECT_LE(
        maximum_radiance_error, packing_bound + (2e-5 * expected) + 0x1p-120);
      EXPECT_LE(maximum_output_error, 2e-5);
      RecordProperty(
        std::string(forward ? "forward_" : "deferred_") + std::to_string(card),
        total / 256);
    }
  }
}

NOLINT_TEST_F(
  LightBenchReferenceTest, DirectionalEnableTogglePublishesAndChangesRendering)
{
  scenesync::SceneObserverSyncModule sync(
    engine::kSceneObserverSyncModulePriority);
  co::testing::TestEventLoop loop;
  const auto synchronize = [&]() -> void {
    co::Run(loop, [&]() -> co::Co<> {
      co_await sync.OnSceneMutation(observer_ptr { &frame });
    });
  };
  synchronize();
  ASSERT_EQ(
    scene->GetDirectionalLightResolver().ResolveDirectionalLights().size(), 1U);
  const auto centre = (kHeight / 2U) * kWidth + kWidth / 2U;
  for (const bool forward : { false, true }) {
    SCOPED_TRACE(forward ? "forward" : "deferred");
    RenderSurface(forward, reference::kExposureEv);
    const auto lit = ReadFloatTexture(*probe->color, true).at(centre).at(0);
    ASSERT_GT(lit, 0.0F);
    for (const bool enabled : { false, true }) {
      bench.GetDirectionalLightState().enabled = enabled;
      bench.Update();
      synchronize();
      // Check before RenderSurface: its fixture-owned SyncObservers must not
      // hide a broken/missing synchronization step in this app integration.
      EXPECT_EQ(
        scene->GetDirectionalLightResolver().ResolveDirectionalLights().size(),
        enabled ? 1U : 0U);
      RenderSurface(forward, reference::kExposureEv, 1U);
      const auto pixel = ReadFloatTexture(*probe->color, true).at(centre);
      for (std::size_t channel = 0; channel < 3; ++channel) {
        EXPECT_NEAR(pixel.at(channel), enabled ? lit : 0.0F, 2e-5F);
      }
    }
  }
}

namespace {
  class LightBenchAutoExposureTest : public LightBenchReferenceTest {
  protected:
    auto AdditionalCapabilities() const -> vortex::CapabilitySet override
    {
      return vortex::RendererCapabilityFamily::kShadowing;
    }
    auto StagePreset(LightBenchPreset preset, std::uint32_t width,
      std::uint32_t height, bool forward = false) -> void
    {
      const auto authored = PresetSettings(preset);
      ApplySettings(authored, bench, camera);
      LightScene::FitReferenceCamera(camera, float(width) / float(height));
      settings = authored.exposure;
      view.viewport.width = static_cast<float>(width);
      view.viewport.height = static_cast<float>(height);
      auto output = CreateRegisteredTexture({ .width = width,
        .height = height,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = graphics::ResourceStates::kCommon });
      framebuffer = Backend().CreateFramebuffer(
        graphics::FramebufferDesc {}.AddColorAttachment(output));
      ++surface_view_id;
      if (settings.mode == engine::ExposureMode::kAuto) {
        ASSERT_TRUE(renderer_->QueueExposureTransition(
          vortex::CompositionView::ViewStateHandle { surface_view_id },
          vortex::ExposureTransitionPolicy::kSeedFromEv100,
          authored.initial_auto_ev));
      }
      const auto draws
        = static_cast<unsigned>(std::count_if(authored.objects.begin(),
          authored.objects.end(), [](const auto& o) { return o.enabled; }));
      bool ready = false;
      probe->inspect = [&](const auto&, const auto&, unsigned count) {
        expected_draws = count;
        ready = count == draws;
      };
      for (unsigned attempt = 0; attempt < 30 && !ready; ++attempt)
        RenderSurface(forward, settings.manual_ev, 1U);
      probe->inspect = {};
      ASSERT_TRUE(ready) << "Preset geometry did not become ready";
      expected_draws = draws;
    }
  };

  // Independent log-histogram integration of this case's Average metering:
  // no texture mask, zero black influence and a unit-P production HDR product.
  auto ReferenceMeter(
    const std::vector<vortex::testing::exposure::Pixel>& pixels,
    const scene::ExposureSettings& settings, std::uint32_t width = 0,
    std::uint32_t height = 0) -> double
  {
    std::array<double, 256> histogram {};
    for (std::size_t index = 0; index < pixels.size(); ++index) {
      const auto& pixel = pixels.at(index);
      double profile = 1;
      if (settings.metering_mode == engine::MeteringMode::kCenterWeighted) {
        const auto x = double(index % width), y = double(index / width);
        profile = std::max(0.0,
          1.0
            - std::hypot(
              ((2 * x + 1) - width) / width, ((2 * y + 1) - height) / height));
      }
      const auto quantized = std::floor(profile * 4095.0 + .5);
      if (quantized == 0)
        continue;
      const double luminance
        = .2126 * pixel.at(0) + .7152 * pixel.at(1) + .0722 * pixel.at(2);
      if (!std::isfinite(luminance)
        || luminance <= std::exp2(settings.min_log_luminance))
        continue;
      const double position
        = std::clamp((std::log2(luminance) - settings.min_log_luminance)
              / settings.log_luminance_range,
            0.0, 1.0)
        * 255.0;
      const auto lower = static_cast<std::size_t>(position);
      const double upper = std::floor(quantized * (position - lower) + .5);
      histogram.at(lower) += quantized - upper;
      histogram.at(std::min(lower + 1, std::size_t { 255 })) += upper;
    }
    double total = 0;
    for (const auto weight : histogram)
      total += weight;
    const double low = total * settings.low_percentile,
                 high = total * settings.high_percentile;
    double cursor = 0, moment = 0, retained = 0;
    for (std::size_t bin = 0; bin < histogram.size(); ++bin) {
      const double end = cursor + histogram.at(bin);
      const double weight
        = std::max(0.0, std::min(end, high) - std::max(cursor, low));
      moment += weight
        * (settings.min_log_luminance
          + double(bin) * settings.log_luminance_range / 255.0);
      retained += weight;
      cursor = end;
    }
    if (retained <= 0)
      throw std::runtime_error("Saved Auto scene has no valid meter samples");
    return std::exp2(moment / retained);
  }
}

NOLINT_TEST_F(
  LightBenchAutoExposureTest, SavedFullSceneAutoTargetRemainsStableAndConverges)
{
  const auto path
    = std::filesystem::path(OXYGEN_LIGHTBENCH_INDOOR_SETTINGS).parent_path()
    / "Test/Data/auto-full-scene.json";
  std::ifstream input(path);
  ASSERT_TRUE(input);
  const auto text = std::string(std::istreambuf_iterator<char> { input }, {});
  const auto saved = DecodeSettings(text);
  ASSERT_TRUE(saved) << saved.error().message;
  // 5:4 approximates the scene region in the supplied image; copied authored
  // inputs are exact. Both dimensions are below the 512-cell meter grid cap.
  constexpr std::uint32_t width = 320, height = 256;
  view.viewport.width = static_cast<float>(width);
  view.viewport.height = static_cast<float>(height);
  auto output = CreateRegisteredTexture({ .width = width,
    .height = height,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = graphics::ResourceStates::kCommon });
  framebuffer = Backend().CreateFramebuffer(
    graphics::FramebufferDesc {}.AddColorAttachment(output));
  LightScene::FitReferenceCamera(camera, float(width) / float(height));
  settings.mode = engine::ExposureMode::kManual;
  frame_delta_seconds = 1.0F / 30.0F;
  RenderSurface(false, reference::kExposureEv);
  auto initial
    = Read<vortex::ExposureStateData>(*probe->exposure->current_state->buffer,
      graphics::ResourceStates::kShaderResource);
  ApplySettings(*saved, bench, camera);
  LightScene::FitReferenceCamera(camera, float(width) / float(height));
  settings = saved->exposure;
  // Newly enabled geometry uploads asynchronously. Gate this numerical
  // observation on actual published draw readiness, not a fixed warmup count.
  bool ready = false;
  probe->inspect = [&](const auto&, const auto&, unsigned draws) {
    expected_draws = draws;
    ready = draws == 6;
  };
  for (unsigned attempt = 0; attempt < 30 && !ready; ++attempt) {
    RenderSurface(false, reference::kExposureEv, 1U);
  }
  ASSERT_TRUE(ready) << "Full scene did not publish all six draw records";
  probe->inspect = {};
  expected_draws = 6;
  double target = 0, meter = 0;
  unsigned frames = 0;
  for (const unsigned checkpoint : { 1U, 30U, 60U, 120U, 240U, 480U, 900U }) {
    RenderSurface(false, reference::kExposureEv, checkpoint - frames);
    frames = checkpoint;
    const auto state
      = Read<vortex::ExposureStateData>(*probe->exposure->current_state->buffer,
        graphics::ResourceStates::kShaderResource);
    const auto domain = Read<vortex::FrameExposureData>(
      *probe->exposure->buffer, graphics::ResourceStates::kShaderResource);
    EXPECT_FLOAT_EQ(domain.pre_exposure, 1.0F);
    if (checkpoint == 1) {
      const auto pixels = ReadFloatTexture(*probe->color);
      meter = ReferenceMeter(pixels, settings);
      const auto ev = std::clamp(std::log2(meter / .18),
        double(settings.min_ev), double(settings.max_ev));
      target = std::exp2(-ev);
      RecordProperty("independent_meter_luminance", meter);
      RecordProperty("independent_target_gain", target);
      RecordProperty("initial_gain", initial.displayed_scale);
    }
    const auto key = std::string("frame_") + std::to_string(checkpoint);
    RecordProperty(key + "_meter", state.raw_metered_luminance);
    RecordProperty(key + "_target", state.target_scale);
    RecordProperty(key + "_applied", state.displayed_scale);
    RecordProperty(key + "_flags", state.flags);
    EXPECT_NEAR(std::log2(state.raw_metered_luminance), std::log2(meter), 2e-4);
    EXPECT_NEAR(std::log2(state.target_scale), std::log2(target), 2e-4);
    EXPECT_TRUE(std::isfinite(state.displayed_scale));
    if (checkpoint == 900) {
      EXPECT_NEAR(std::log2(state.displayed_scale), std::log2(target), 5e-4);
      const auto pixels = ReadFloatTexture(*probe->color);
      std::size_t clipped = 0, positive = 0;
      for (const auto& pixel : pixels) {
        const auto peak = std::max({ pixel.at(0), pixel.at(1), pixel.at(2) });
        if (peak > 0)
          ++positive;
        if (peak * state.displayed_scale > 1)
          ++clipped;
      }
      RecordProperty("clipped_pixels", clipped);
      RecordProperty("positive_pixels", positive);
    }
  }
}

NOLINT_TEST_F(LightBenchAutoExposureTest,
  PointPresetPreservesFixedExposureAndDistanceFalloff)
{
  ASSERT_NO_FATAL_FAILURE(
    StagePreset(LightBenchPreset::kPointFalloff, 1, 1, true));
  const double exposure_ev = settings.manual_ev;
  for (const float distance : { 1.0F, 2.0F, 4.0F }) {
    bench.GetPointLightState().position = { 0, distance, 1 };
    bench.Update();
    RenderSurface(true, static_cast<float>(exposure_ev), 2U);
    const auto pixel = ReadFloatTexture(*probe->color).front();
    const double fade = std::pow(1.0 - std::pow(distance / 20.0, 4.0), 2.0);
    const double lux
      = 1000.0 / (4 * std::numbers::pi * distance * distance) * fade;
    const double expected = Expected(.18, .04) * lux / 1000.0;
    for (std::size_t channel = 0; channel < 3; ++channel)
      EXPECT_NEAR(pixel.at(channel), expected, expected * 2e-5);
    RecordProperty(
      "distance_" + std::to_string(static_cast<int>(distance)) + "m_luminance",
      pixel.at(0));
    EXPECT_DOUBLE_EQ(settings.manual_ev, exposure_ev);
  }
}

NOLINT_TEST_F(
  LightBenchAutoExposureTest, SpotPresetHasBrightCenterAndFiniteConeFootprint)
{
  constexpr unsigned width = 321, height = 257;
  ASSERT_NO_FATAL_FAILURE(
    StagePreset(LightBenchPreset::kSpotCone, width, height, true));
  const auto pixels = ReadFloatTexture(*probe->color);
  const auto center = pixels.at((height / 2) * width + width / 2);
  const double ci = std::cos(15.0 * std::numbers::pi / 180.0),
               co = std::cos(30.0 * std::numbers::pi / 180.0);
  const double omega = 2 * std::numbers::pi * ((1 - ci) + (ci - co) / 3);
  const double lux = 1000 / omega / 9 * std::pow(1 - std::pow(3.0 / 20, 4), 2);
  const double expected = Expected(.18, .04) * lux / 1000;
  EXPECT_NEAR(center.at(0), expected, expected * 2e-5);
  // Probe 2 m from the center: outside the 1.732 m cone radius but still on
  // the 5 m receiver, independently of the camera's default framing.
  const auto outside_x
    = static_cast<unsigned>(width * (.5 + 2.0 / reference::kMinimumViewWidth));
  const auto outside = pixels.at((height / 2) * width + outside_x);
  EXPECT_GT(outside.at(3), .99F);
  EXPECT_FLOAT_EQ(outside.at(0), 0.0F);
  RecordProperty("spot_center_luminance", center.at(0));
}

NOLINT_TEST_F(
  LightBenchAutoExposureTest, SpotPresetFullFootprintMatchesIndependentFalloff)
{
  constexpr unsigned width = 321, height = 257;
  constexpr double view_width = reference::kMinimumViewWidth;
  constexpr double light_distance = 3.0;
  const double inner = std::cos(15.0 * std::numbers::pi / 180.0);
  const double outer = std::cos(30.0 * std::numbers::pi / 180.0);
  // Integrate the authored squared angular distribution over solid angle.
  const double intensity
    = 1000.0 / (2 * std::numbers::pi * (1 - inner + (inner - outer) / 3));
  const auto moments = oracle::IntegrateGgxMoments(
    oracle::PerceptualRoughness { 1.0 }, oracle::ViewCosine { 1.0 });
  ASSERT_TRUE(moments);
  const auto radiance = [&](double radius, double albedo, double f0) {
    const double distance = std::hypot(light_distance, radius);
    const double cosine = light_distance / distance;
    const double angular
      = std::pow(std::clamp((cosine - outer) / (inner - outer), 0.0, 1.0), 2);
    std::array<oracle::BrdfReflectance, 3> material;
    material.fill({ .f0 = f0, .diffuse = albedo });
    const auto brdf = oracle::EvaluateRasterGgxBrdf(
      { .light = oracle::LightCosine { cosine } }, material, *moments);
    if (!brdf) {
      throw std::runtime_error("Independent spot BRDF failed");
    }
    const auto& lobe = brdf->front();
    const double window = std::pow(1 - std::pow(distance / 20.0, 4), 2);
    return intensity * angular * window / (distance * distance) * cosine
      * (lobe.diffuse + lobe.single_scattering + lobe.multiple_scattering);
  };
  const double peak = radiance(0, .18, .04);
  const double encoded = 1.055 * std::pow(.18, 1.0 / 2.4) - .055;
  const auto decode
    = [](double code) { return std::pow((code / 255.0 + .055) / 1.055, 2.4); };
  for (const bool forward : { true, false }) {
    SCOPED_TRACE(forward ? "forward" : "deferred");
    ASSERT_NO_FATAL_FAILURE(
      StagePreset(LightBenchPreset::kSpotCone, width, height, forward));
    const auto pixels = ReadFloatTexture(*probe->color);
    const auto output = ReadFloatTexture(
      *framebuffer->GetDescriptor().color_attachments.front().texture);
    const auto domain = Read<vortex::FrameExposureData>(
      *probe->exposure->buffer, graphics::ResourceStates::kShaderResource);
    ASSERT_FLOAT_EQ(domain.pre_exposure, 1.0F);
    double maximum_error = 0, maximum_excess = 0;
    unsigned samples = 0, dark_samples = 0;
    std::ostringstream profile;
    profile << std::setprecision(12)
            << "radius_m,expected_hdr,actual_hdr,display_output\n";
    for (unsigned y = 0; y < height; ++y) {
      for (unsigned x = 0; x < width; ++x) {
        const double dx
          = (double(x) + .5 - double(width) / 2) * view_width / width;
        const double dy
          = (double(y) + .5 - double(height) / 2) * view_width / width;
        const double radius = std::hypot(dx, dy);
        // Includes the complete cone and a foreground annulus outside it.
        if (radius > 2.0) {
          continue;
        }
        const auto& pixel = pixels.at(y * width + x);
        ASSERT_GT(pixel.at(3), .99F);
        const double expected = radiance(radius, .18, .04);
        double packing_bound = 0;
        if (!forward) {
          // Same storage/sampling uncertainty as the calibrated card test.
          for (const double code :
            { std::floor(encoded * 255) - .5, std::ceil(encoded * 255) + .5 }) {
            for (const double specular : { 127.0 / 255, 128.0 / 255 }) {
              packing_bound = std::max(packing_bound,
                std::abs(
                  radiance(radius, decode(code), .08 * specular) - expected));
            }
          }
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
          ASSERT_TRUE(std::isfinite(pixel.at(channel)));
          const double error = std::abs(pixel.at(channel) - expected);
          maximum_error = std::max(maximum_error, error);
          maximum_excess = std::max(maximum_excess, error - packing_bound);
          if (expected == 0) {
            EXPECT_FLOAT_EQ(pixel.at(channel), 0.0F);
          }
        }
        ++samples;
        dark_samples += expected == 0 ? 1 : 0;
        if (y == height / 2 && x >= width / 2) {
          profile << radius << ',' << expected << ',' << pixel.at(0) << ','
                  << output.at(y * width + x).at(0) << '\n';
        }
      }
    }
    // Peak-relative profile error stays meaningful at the zero-valued boundary.
    // This supplements, rather than replaces, the strict center photometry
    // test.
    EXPECT_LE(maximum_excess, peak * 2e-5);
    EXPECT_GT(dark_samples, 0U);
    const std::string prefix = forward ? "forward_" : "deferred_";
    RecordProperty(prefix + "samples", samples);
    RecordProperty(prefix + "dark_samples", dark_samples);
    RecordProperty(prefix + "maximum_hdr_error", maximum_error);
    RecordProperty(prefix + "maximum_excess_over_packing", maximum_excess);
    RecordProperty(prefix + "profile_csv", profile.str());
  }
  RecordProperty("peak_hdr", peak);
  RecordProperty("inner_radius_m",
    light_distance * std::tan(15.0 * std::numbers::pi / 180.0));
  RecordProperty("outer_radius_m",
    light_distance * std::tan(30.0 * std::numbers::pi / 180.0));
}

NOLINT_TEST_F(LightBenchAutoExposureTest,
  IndoorOutdoorAndAdaptationPresetsConvergeWithoutWhiteSubjects)
{
  constexpr unsigned width = 320, height = 256;
  frame_delta_seconds = 1.0F / 30.0F;
  for (const auto preset : { LightBenchPreset::kAutoAdaptation,
         LightBenchPreset::kIndoor, LightBenchPreset::kOutdoorDaylight }) {
    SCOPED_TRACE(GetPresetInfo(preset).id);
    ASSERT_NO_FATAL_FAILURE(StagePreset(preset, width, height));
    const auto initial
      = Read<vortex::ExposureStateData>(*probe->exposure->current_state->buffer,
        graphics::ResourceStates::kShaderResource);
    const auto hdr = ReadFloatTexture(*probe->color);
    const auto expected_meter = ReferenceMeter(hdr, settings, width, height);
    const auto expected_gain = .18 / expected_meter;
    RenderSurface(false, settings.manual_ev, 600U);
    const auto state
      = Read<vortex::ExposureStateData>(*probe->exposure->current_state->buffer,
        graphics::ResourceStates::kShaderResource);
    EXPECT_NEAR(
      std::log2(state.raw_metered_luminance), std::log2(expected_meter), 2e-4);
    EXPECT_NEAR(std::log2(state.target_scale), std::log2(expected_gain), 2e-4);
    EXPECT_NEAR(
      std::log2(state.displayed_scale), std::log2(expected_gain), 5e-4);
    const auto resolver = vortex::SceneCameraViewResolver(
      [&](const ViewId&) { return camera; }, view.viewport);
    const auto resolved = resolver(ViewId { surface_view_id });
    const auto position = bench.GetGrayCardState().position;
    const auto clip = resolved.ProjectionMatrix() * resolved.ViewMatrix()
      * Vec4 { position, 1.0F };
    const auto x = static_cast<unsigned>((clip.x / clip.w * .5F + .5F) * width);
    const auto y
      = static_cast<unsigned>((.5F - clip.y / clip.w * .5F) * height);
    ASSERT_LT(x, width);
    ASSERT_LT(y, height);
    const auto output = ReadFloatTexture(
      *framebuffer->GetDescriptor().color_attachments.front().texture);
    const auto gray = output.at(y * width + x);
    EXPECT_GT(gray.at(0), .05F);
    EXPECT_LT(std::max({ gray.at(0), gray.at(1), gray.at(2) }), .98F);
    const auto label = std::string(GetPresetInfo(preset).id);
    RecordProperty(label + "_initial_gain", initial.displayed_scale);
    RecordProperty(label + "_meter", state.raw_metered_luminance);
    RecordProperty(label + "_settled_gain", state.displayed_scale);
    RecordProperty(label + "_gray_output", gray.at(0));
  }
}

NOLINT_TEST_F(LightBenchAutoExposureTest,
  AutoPresetLightStepsPreserveHistoryAndAdaptBothWays)
{
  frame_delta_seconds = 1.0F / 30.0F;
  ASSERT_NO_FATAL_FAILURE(
    StagePreset(LightBenchPreset::kAutoAdaptation, 320, 256));
  RenderSurface(false, settings.manual_ev, 600U);
  auto previous
    = Read<vortex::ExposureStateData>(*probe->exposure->current_state->buffer,
      graphics::ResourceStates::kShaderResource);
  const auto generation = previous.applied_generation;
  for (const float lux : { 10000.0F, 100.0F, 1000.0F }) {
    bench.GetDirectionalLightState().illuminance_lux = lux;
    bench.Update();
    RenderSurface(false, settings.manual_ev, 1U);
    const auto first
      = Read<vortex::ExposureStateData>(*probe->exposure->current_state->buffer,
        graphics::ResourceStates::kShaderResource);
    // Histogram percentile boundaries move when the signal shifts across bins.
    // Reintegrate the current input rather than assuming exact scale invariance
    // of the quantized, percentile-trimmed meter.
    const double target = .18
      / ReferenceMeter(ReadFloatTexture(*probe->color), settings, 320, 256);
    const auto delta
      = std::log2(first.displayed_scale / previous.displayed_scale);
    EXPECT_LE(std::abs(delta), settings.speed_up * frame_delta_seconds + 1e-4);
    EXPECT_EQ(first.applied_generation, generation);
    EXPECT_NEAR(std::log2(first.target_scale), std::log2(target), 2e-4);
    RenderSurface(false, settings.manual_ev, 600U);
    const auto final
      = Read<vortex::ExposureStateData>(*probe->exposure->current_state->buffer,
        graphics::ResourceStates::kShaderResource);
    EXPECT_NEAR(std::log2(final.displayed_scale), std::log2(target), 5e-4);
    EXPECT_EQ(final.applied_generation, generation);
    RecordProperty(
      "gain_at_" + std::to_string(static_cast<unsigned>(lux)) + "_lux",
      final.displayed_scale);
    previous = final;
  }
}

} // namespace oxygen::examples::light_bench::testing
