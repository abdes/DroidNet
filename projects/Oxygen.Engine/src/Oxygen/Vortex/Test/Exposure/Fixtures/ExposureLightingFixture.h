//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

class ExposureAllocationScenario;

// A single visible surface isolates light transport from scene composition.
class ExposureLightingGpuTest : public ExposureGpuTest {
  friend class ExposureAllocationScenario;

protected:
  enum class ExpectedViewOutcome : std::uint8_t { kRendered, kRejected };

  virtual auto AdditionalCapabilities() const -> CapabilitySet;

  struct Probe final : IViewExtension {
    explicit Probe(Renderer& value);
    auto OnViewSetup(const ViewSetupContext& hook) -> void override;
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override;

    // The fixture destroys this observer before its owning renderer.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Renderer& renderer;
    std::shared_ptr<const graphics::Texture> color;
    postprocess::ExposurePass::FrameLease exposure;
    unsigned draws = 0;
    std::vector<float> raster_depths;
    bool early_depth_complete = false;
    std::function<void(RenderContext&)> prepare;
    std::function<void(
      const RenderContext&, const SceneTextureExtractRef&, unsigned)>
      inspect;
    std::function<void()> after_submit;
    std::function<void(const ViewRenderGpuContext&)> after_render;
  };

  auto SetUp() -> void override;
  auto TearDown() -> void override;
  auto MakeEmissiveMaterial(float value)
    -> std::shared_ptr<data::MaterialAsset>;
  auto MeasureHdrAllocationAccounting(bool temporal) -> void;
  auto QualifySharedSceneLifecycle(bool forward) -> void;
  auto QualifyMixedPrecisionAuxiliaryHandoff(bool split_targets) -> void;
  auto UniformReferenceGain(float luminance) const -> double;
  auto ExpectSurfaceExposure(float luminance, double expected_gain,
    const graphics::Texture& reference, ExposureStateData& state) -> void;
  auto ReferenceAdaptedGain(
    double previous, double target, double seconds) const -> double;
  auto SetSurface(data::MaterialDomain domain, float emission = 0,
    bool rejected_mask = false) -> void;
  auto RenderSurface(bool forward, float ev, unsigned frames = 5,
    ExpectedViewOutcome outcome = ExpectedViewOutcome::kRendered) -> void;
  auto RenderPublishedSurface(bool forward) -> void;

  std::shared_ptr<scene::Scene> scene;
  scene::SceneNode camera;
  scene::SceneNode mesh_node;
  scene::ExposureSettings settings;
  View view;
  std::shared_ptr<graphics::Framebuffer> framebuffer;
  std::shared_ptr<Probe> probe;
  engine::FrameContext frame;
  unsigned sequence = 0;
  unsigned material_sequence = 0;
  unsigned expected_draws = 1;
  std::size_t initial_scene_capacity { 8U };
  std::uint32_t surface_view_id = 100U;
  ViewId surface_source_id = kInvalidViewId;
  std::optional<scene::ExposureSettings> surface_exposure_override;
  bool verify_manual_p = true;
  bool persistent_surface_state = true;
  float frame_delta_seconds = 0;
  DepthPrePassMode depth_mode = DepthPrePassMode::kOpaqueAndMasked;
  console::Console fixture_console;
};

} // namespace oxygen::vortex::testing::exposure
