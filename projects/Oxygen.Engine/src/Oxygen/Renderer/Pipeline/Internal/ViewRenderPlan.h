//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/Pipeline/RenderMode.h>

namespace oxygen::renderer::internal {

//! Declares which pipeline domains a view is allowed to execute for this frame.
/*!
 `ViewRenderIntent` is a frame-scoped execution contract produced by planning.
 It is consumed by render callback code to gate pass scheduling.

 - `kSceneAndComposite`: run scene-domain rendering (HDR linear) and then
   produce composite-domain output.
 - `kCompositeOnly`: skip scene-domain rendering and execute only
   composite-domain work (overlays/tools/compositor inputs).
*/
enum class ViewRenderIntent : std::uint8_t {
  kSceneAndComposite,
  kCompositeOnly,
};

[[nodiscard]] inline auto to_string(ViewRenderIntent intent) -> std::string_view
{
  using enum ViewRenderIntent;
  switch (intent) {
    // clang-format off
  case kSceneAndComposite: return "SceneAndComposite";
  case kCompositeOnly: return "CompositeOnly";
  default: return "__Unknown__";
    // clang-format on
  }
}

//! Tone-map behavior policy when scene-domain output is converted for
//! compositing.
/*!
 `ToneMapPolicy` is independent from view intent:
 - Intent decides whether the scene->composite stage exists.
 - Tone-map policy decides how that stage behaves when it exists.

 Current values:
 - `kConfigured`: use configured exposure and selected tone mapper.
 - `kNeutral`: force neutral transfer for debug/readback consistency.

 Reserved future extensions:
 - `kBypass`: explicit pass-through path where valid.
 - `kDebugFalseColor`: diagnostic visualization policy.
*/
enum class ToneMapPolicy : std::uint8_t {
  kConfigured,
  kNeutral,
};

[[nodiscard]] inline auto to_string(ToneMapPolicy policy) -> std::string_view
{
  using enum ToneMapPolicy;
  switch (policy) {
  case kConfigured:
    return "Configured";
  case kNeutral:
    return "Neutral";
  default:
    return "__Unknown__";
  }
}

class ViewRenderPlan {
public:
  //! Builder input used only at plan construction time.
  /*!
   `Spec` is mutable by design while assembling a plan. Once passed to
   `ViewRenderPlan`, values become immutable and are invariant-checked.
  */
  struct Spec {
    //! Domain execution contract for the view this frame.
    ViewRenderIntent intent { ViewRenderIntent::kCompositeOnly };
    //! Effective render mode after view-level overrides are applied.
    RenderMode effective_render_mode { RenderMode::kSolid };
    //! Tone-map policy for scene->composite conversion when applicable.
    ToneMapPolicy tone_map_policy { ToneMapPolicy::kConfigured };
    //! Executes wireframe overlay pass on composite-domain output.
    bool run_overlay_wireframe { false };
    //! Executes sky rendering pass.
    //! Valid scenarios:
    //! - true / false LUT: sky sphere/cubemap visual without atmosphere LUT.
    //! - true / true LUT: atmosphere sky visual with LUT update.
    //! - false / false LUT: no sky work this frame.
    bool run_sky_pass { false };
    //! Executes sky LUT preparation/usage.
    //! Current policy: when true, `run_sky_pass` should also be true.
    //! Future extension may relax this if LUT is consumed by non-sky passes.
    bool run_sky_lut_update { false };
  };

  //! Constructs an immutable plan and validates cross-field invariants.
  explicit ViewRenderPlan(const Spec& spec)
    : intent_(spec.intent)
    , effective_render_mode_(spec.effective_render_mode)
    , tone_map_policy_(spec.tone_map_policy)
    , run_overlay_wireframe_(spec.run_overlay_wireframe)
    , run_sky_pass_(spec.run_sky_pass)
    , run_sky_lut_update_(spec.run_sky_lut_update)
  {
    CHECK_F(!run_overlay_wireframe_
        || intent_ == ViewRenderIntent::kSceneAndComposite,
      "Overlay wireframe requires scene+composite intent");
    CHECK_F(!(tone_map_policy_ == ToneMapPolicy::kNeutral)
        || intent_ == ViewRenderIntent::kSceneAndComposite,
      "Neutral tonemap requires HDR->SDR path");
    CHECK_F(!run_sky_pass_ || intent_ == ViewRenderIntent::kSceneAndComposite,
      "Sky visuals require scene+composite intent");
    CHECK_F(
      !run_sky_lut_update_ || intent_ == ViewRenderIntent::kSceneAndComposite,
      "Sky LUT requires scene+composite intent");
  }
  ~ViewRenderPlan() = default;
  OXYGEN_DEFAULT_COPYABLE(ViewRenderPlan)
  OXYGEN_DEFAULT_MOVABLE(ViewRenderPlan)

  //! Returns the explicit execution intent for this view/frame.
  [[nodiscard]] auto Intent() const noexcept -> ViewRenderIntent
  {
    return intent_;
  }
  //! True when scene-domain rendering is allowed for this view/frame.
  [[nodiscard]] auto HasSceneLinearPath() const noexcept -> bool
  {
    return intent_ == ViewRenderIntent::kSceneAndComposite;
  }
  //! Composite-domain output path is always present for valid plans.
  [[nodiscard]] auto HasCompositePath() const noexcept -> bool { return true; }
  //! Returns effective render mode after planner resolution.
  [[nodiscard]] auto EffectiveRenderMode() const noexcept -> RenderMode
  {
    return effective_render_mode_;
  }
  //! Returns tone-map behavior policy for scene->composite conversion.
  [[nodiscard]] auto GetToneMapPolicy() const noexcept -> ToneMapPolicy
  {
    return tone_map_policy_;
  }
  //! Returns whether wireframe overlay pass should run.
  [[nodiscard]] auto RunOverlayWireframe() const noexcept -> bool
  {
    return run_overlay_wireframe_;
  }
  //! Returns whether sky shading is allowed for this plan.
  [[nodiscard]] auto RunSkyPass() const noexcept -> bool
  {
    return run_sky_pass_;
  }
  //! Returns whether sky LUT path is enabled for this plan.
  [[nodiscard]] auto RunSkyLutUpdate() const noexcept -> bool
  {
    return run_sky_lut_update_;
  }
  //! Returns true when any sky-related workload is scheduled.
  [[nodiscard]] auto HasSkyWork() const noexcept -> bool
  {
    return run_sky_pass_ || run_sky_lut_update_;
  }

private:
  ViewRenderIntent intent_ { ViewRenderIntent::kCompositeOnly };
  RenderMode effective_render_mode_ { RenderMode::kSolid };
  ToneMapPolicy tone_map_policy_ { ToneMapPolicy::kConfigured };
  bool run_overlay_wireframe_ { false };
  bool run_sky_pass_ { false };
  bool run_sky_lut_update_ { false };
};

} // namespace oxygen::renderer::internal
