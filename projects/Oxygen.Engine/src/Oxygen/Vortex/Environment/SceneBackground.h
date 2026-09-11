//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/RenderContext.h>

namespace oxygen::vortex::environment {

//! Resolves the presentation background for a normal scene view.
[[nodiscard]] inline auto ResolveSceneBackground(const RenderContext& ctx)
  -> std::optional<Vec3>
{
  if (ctx.current_view.is_reflection_capture
    || !ctx.current_view.feature_mask.Has(
      CompositionView::ViewFeatureMask::kEnvironment)) {
    return std::nullopt;
  }
  const auto scene = ctx.GetScene();
  if (!scene) {
    return std::nullopt;
  }
  const auto environment = scene->GetEnvironment();
  if (!environment) {
    return std::nullopt;
  }
  if (const auto atmosphere
    = environment->TryGetSystem<scene::environment::SkyAtmosphere>();
    atmosphere && atmosphere->IsEnabled() && ctx.current_view.with_atmosphere) {
    return std::nullopt;
  }
  const auto background
    = environment->TryGetSystem<scene::environment::Background>();
  return background && background->IsEnabled()
    ? std::optional<Vec3> { background->GetColorRgb() }
    : std::nullopt;
}

} // namespace oxygen::vortex::environment
