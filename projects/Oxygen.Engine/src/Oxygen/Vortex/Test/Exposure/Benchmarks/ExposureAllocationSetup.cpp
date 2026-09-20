//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <charconv>
#include <cstdlib>

#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureAllocationScenario.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;

ExposureAllocationScenario::ExposureAllocationScenario(
  ExposureLightingGpuTest& fixture, bool temporal)
  : fixture_(fixture)
  , temporal_(temporal)
{
}

ExposureAllocationScenario::~ExposureAllocationScenario()
{
  if (cleanup_armed_) {
    CleanUp();
  }
}

auto ExposureAllocationScenario::Run() -> void
{
  SetUp();
  if (::testing::Test::HasFatalFailure()) {
    return;
  }
  RunLifecycle();
  if (::testing::Test::HasFatalFailure()) {
    return;
  }
  WriteReport();
}

auto ExposureAllocationScenario::ReadEnvironment(
  const char* name, std::string_view fallback) -> std::string
{
  char* text = nullptr;
  std::size_t size = 0U;
  if (_dupenv_s(&text, &size, name) != 0 || (text == nullptr)) {
    return std::string {
      fallback,
    };
  }
  const auto owned
    = std::unique_ptr<char, decltype(&std::free)>(text, &std::free);
  return std::string {
    owned.get(),
  };
}

auto ExposureAllocationScenario::SetUp() -> void
{
  const auto width_text
    = ReadEnvironment("OXYGEN_EXPOSURE_TIMING_WIDTH", "1920");
  width_ = 0U;
  const auto parsed = std::from_chars(
    width_text.data(), std::to_address(width_text.end()), width_);
  ASSERT_EQ(parsed.ec, std::errc {});
  ASSERT_EQ(parsed.ptr, std::to_address(width_text.end()));
  ASSERT_TRUE(width_ == 1920U || width_ == 3840U);
  precision_
    = ReadEnvironment("OXYGEN_EXPOSURE_BASELINE_PRECISION", "production");
  ASSERT_TRUE(precision_ == "production" || precision_ == "fp32"
    || precision_ == "fp32-only" || precision_ == "qualified");
  fp32_reference_ = precision_ == "fp32";
  height_ = width_ * 9U / 16U;
  fixture_.verify_manual_p = false;
  fixture_.renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
    fp32_reference_               ? HdrPrecisionControl::kFp32Reference
      : precision_ == "qualified" ? HdrPrecisionControl::kQualified
      : precision_ == "fp32-only" ? HdrPrecisionControl::kFp32Only
                                  : HdrPrecisionControl::kProduction);
  fixture_.view.viewport = {
    .width = static_cast<float>(width_),
    .height = static_cast<float>(height_),
  };
  const auto camera_lens
    = fixture_.camera.GetCameraAs<scene::PerspectiveCamera>();
  if (!camera_lens.has_value()) {
    FAIL() << "Expected a perspective camera";
  }
  camera_lens->get().SetViewport(fixture_.view.viewport);
  fixture_.SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& sky = fixture_.scene->GetEnvironment()
                ->AddSystem<scene::environment::SkyAtmosphere>();
  sky.SetEnabled(true);
  sky.SetRayleighScatteringRgb({
    0,
    0,
    0,
  });
  sky.SetMieScatteringRgb({
    0,
    0,
    0,
  });
  sky.SetMieAbsorptionRgb({
    0,
    0,
    0,
  });
  sky.SetOzoneAbsorptionRgb({
    0,
    0,
    0,
  });
  auto& fog
    = fixture_.scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(0);
  ASSERT_EQ(
    fixture_.fixture_console
      .Execute(temporal_ ? "vtx.volumetric_fog.temporal_reprojection true"
                         : "vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(
    fixture_.fixture_console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  fixture_.probe->prepare = [](RenderContext& context) -> void {
    context.current_view.with_atmosphere = true;
    context.current_view.with_height_fog = true;
  };
  fixture_.scene->Update();
  fixture_.scene->SyncObservers();

  backend_ = &fixture_.FailureBackend();
  backend_->tracked_textures.clear();
  backend_->tracked_buffers.clear();
  backend_->peak_texture_bytes = backend_->peak_hdr_bytes = 0U;
  backend_->peak_buffer_bytes = backend_->peak_placement_bytes = 0U;
  backend_->peak_engine_placement_bytes = 0U;
  backend_->allocation_peaks = nlohmann::json::object();
  backend_->allocation_peak_history = nlohmann::json::array();
  backend_->accounting_phase = "fixed_fixture_outputs";
  backend_->track_resources = true;
  backend_->account_texture_allocations = true;
  consumer_.emplace(*fixture_.renderer_);

  cleanup_armed_ = true;
  for (unsigned index = 0U; index < 2U; ++index) {
    outputs_.at(index) = fixture_.Backend().CreateTexture({
      .width = width_ >> index,
      .height = height_ >> index,
      .format = Format::kRGBA32Float,
      .debug_name = "LifecycleAccounting.Output" + std::to_string(index),
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    ASSERT_NE(outputs_.at(index), nullptr);
    fixture_.Backend().GetResourceRegistry().Register(outputs_.at(index));
    targets_.at(index) = fixture_.Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(outputs_.at(index)));
    consumer_outputs_.at(index) = fixture_.Backend().CreateTexture({
      .width = 1U,
      .height = 1U,
      .format = Format::kRGBA32Float,
      .debug_name
      = "LifecycleAccounting.DelayedConsumer" + std::to_string(index),
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    ASSERT_NE(consumer_outputs_.at(index), nullptr);
    fixture_.Backend().GetResourceRegistry().Register(
      consumer_outputs_.at(index));
    consumer_targets_.at(index) = fixture_.Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(consumer_outputs_.at(index)));
  }

  fixture_.probe->inspect
    = [&](const RenderContext& context, const SceneTextureExtractRef& color,
        unsigned draws) -> void {
    const auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *fixture_.renderer_);
    CHECK_NOTNULL_F(owner);
    const auto& extracts = owner->GetSceneTextureExtracts();
    current_.insert_or_assign(context.current_view.view_state_handle,
      ViewRecord { .id = context.current_view.view_id,
        .color = color,
        .depths = { extracts.resolved_scene_depth, extracts.prev_scene_depth, },
        .draws = draws, });
    frames_.push_back({
      {
        "sequence",
        context.frame_sequence.get(),
      },
      {
        "phase",
        backend_->accounting_phase,
      },
      {
        "view",
        context.current_view.view_id.get(),
      },
      {
        "handle",
        context.current_view.view_state_handle.get(),
      },
      {
        "format",
        static_cast<unsigned>(color.texture->GetDescriptor().format),
      },
      {
        "width",
        color.texture->GetDescriptor().width,
      },
      {
        "height",
        color.texture->GetDescriptor().height,
      },
      {
        "draws",
        draws,
      },
    });
  };
}

auto ExposureAllocationScenario::CleanUp() noexcept -> void
{
  // These readers were declared after the original scope guard, so release
  // them before executing its deferred-submission and output cleanup.
  retained_ = {};
  current_.clear();
  backend_->defer_tonemap_recorders = false;
  fixture_.Backend().SubmitDeferredCommandLists();
  fixture_.WaitForQueueIdle();
  backend_->deferred_tonemap_recordings.clear();
  backend_->account_texture_allocations = false;
  backend_->track_resources = false;
  fixture_.probe->inspect = {};
  fixture_.probe->color.reset();
  fixture_.probe->exposure.reset();
  targets_ = {};
  consumer_targets_ = {};
  for (auto* collection : {
         &outputs_,
         &consumer_outputs_,
       }) {
    for (auto& texture : *collection) {
      if (texture) {
        fixture_.Backend().GetResourceRegistry().UnRegisterResource(*texture);
        fixture_.Backend().RegisterDeferredRelease(std::move(texture));
      }
    }
  }
  for (auto& readback : depth_readbacks_) {
    if (readback.buffer) {
      fixture_.Backend().GetResourceRegistry().UnRegisterResource(
        *readback.buffer);
      fixture_.Backend().RegisterDeferredRelease(std::move(readback.buffer));
    }
  }
}

} // namespace oxygen::vortex::testing::exposure
