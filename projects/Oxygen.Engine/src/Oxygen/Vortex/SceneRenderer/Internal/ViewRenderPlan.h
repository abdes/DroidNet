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
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace oxygen::vortex::internal {

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
  struct Spec {
    ViewRenderIntent intent { ViewRenderIntent::kCompositeOnly };
    RenderMode effective_render_mode { RenderMode::kSolid };
    ShaderDebugMode effective_shader_debug_mode { ShaderDebugMode::kDisabled };
    ToneMapPolicy tone_map_policy { ToneMapPolicy::kConfigured };
    DepthPrePassMode depth_prepass_mode { DepthPrePassMode::kOpaqueAndMasked };
    bool run_overlay_wireframe { false };
    bool run_sky_pass { false };
    bool run_sky_lut_update { false };
  };

  explicit ViewRenderPlan(const Spec& spec)
    : intent_(spec.intent)
    , effective_render_mode_(spec.effective_render_mode)
    , effective_shader_debug_mode_(spec.effective_shader_debug_mode)
    , tone_map_policy_(spec.tone_map_policy)
    , depth_prepass_mode_(spec.depth_prepass_mode)
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
    CHECK_F(depth_prepass_mode_ == DepthPrePassMode::kDisabled
        || intent_ == ViewRenderIntent::kSceneAndComposite,
      "DepthPrePass requires scene+composite intent");
    CHECK_F(depth_prepass_mode_ == DepthPrePassMode::kDisabled
        || effective_render_mode_ != RenderMode::kWireframe,
      "DepthPrePass is not valid for wireframe-only plans");
    CHECK_F(!run_sky_pass_ || intent_ == ViewRenderIntent::kSceneAndComposite,
      "Sky visuals require scene+composite intent");
    CHECK_F(
      !run_sky_lut_update_ || intent_ == ViewRenderIntent::kSceneAndComposite,
      "Sky LUT requires scene+composite intent");
  }
  ~ViewRenderPlan() = default;
  OXYGEN_DEFAULT_COPYABLE(ViewRenderPlan)
  OXYGEN_DEFAULT_MOVABLE(ViewRenderPlan)

  [[nodiscard]] auto Intent() const noexcept -> ViewRenderIntent
  {
    return intent_;
  }
  [[nodiscard]] auto HasSceneLinearPath() const noexcept -> bool
  {
    return intent_ == ViewRenderIntent::kSceneAndComposite;
  }
  [[nodiscard]] auto HasCompositePath() const noexcept -> bool { return true; }
  [[nodiscard]] auto EffectiveRenderMode() const noexcept -> RenderMode
  {
    return effective_render_mode_;
  }
  [[nodiscard]] auto EffectiveShaderDebugMode() const noexcept
    -> ShaderDebugMode
  {
    return effective_shader_debug_mode_;
  }
  [[nodiscard]] auto GetToneMapPolicy() const noexcept -> ToneMapPolicy
  {
    return tone_map_policy_;
  }
  [[nodiscard]] auto GetDepthPrePassMode() const noexcept -> DepthPrePassMode
  {
    return depth_prepass_mode_;
  }
  [[nodiscard]] auto WantsDepthPrePass() const noexcept -> bool
  {
    return depth_prepass_mode_ != DepthPrePassMode::kDisabled;
  }
  [[nodiscard]] auto RunOverlayWireframe() const noexcept -> bool
  {
    return run_overlay_wireframe_;
  }
  [[nodiscard]] auto RunSkyPass() const noexcept -> bool
  {
    return run_sky_pass_;
  }
  [[nodiscard]] auto RunSkyLutUpdate() const noexcept -> bool
  {
    return run_sky_lut_update_;
  }
  [[nodiscard]] auto HasSkyWork() const noexcept -> bool
  {
    return run_sky_pass_ || run_sky_lut_update_;
  }

private:
  ViewRenderIntent intent_ { ViewRenderIntent::kCompositeOnly };
  RenderMode effective_render_mode_ { RenderMode::kSolid };
  ShaderDebugMode effective_shader_debug_mode_ { ShaderDebugMode::kDisabled };
  ToneMapPolicy tone_map_policy_ { ToneMapPolicy::kConfigured };
  DepthPrePassMode depth_prepass_mode_ { DepthPrePassMode::kDisabled };
  bool run_overlay_wireframe_ { false };
  bool run_sky_pass_ { false };
  bool run_sky_lut_update_ { false };
};

} // namespace oxygen::vortex::internal
