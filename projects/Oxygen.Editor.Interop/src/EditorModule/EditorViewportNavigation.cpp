//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportNavigation.h"
#include "EditorModule/EditorViewportDollyFeature.h"
#include "EditorModule/EditorViewportFlyFeature.h"
#include "EditorModule/EditorViewportOrbitFeature.h"
#include "EditorModule/EditorViewportPanFeature.h"
#include "EditorModule/EditorViewportWheelZoomFeature.h"

namespace oxygen::interop::module {

  EditorViewportNavigation::EditorViewportNavigation() {
    features_.push_back(std::make_unique<EditorViewportOrbitFeature>());
    features_.push_back(std::make_unique<EditorViewportPanFeature>());
    features_.push_back(std::make_unique<EditorViewportDollyFeature>());
    features_.push_back(std::make_unique<EditorViewportFlyFeature>());
    features_.push_back(std::make_unique<EditorViewportWheelZoomFeature>());
  }

  auto EditorViewportNavigation::InitializeBindings(
    oxygen::engine::InputSystem& input_system) noexcept -> bool {
    if (ctx_) {
      return true;
    }

    ctx_ = std::make_shared<oxygen::input::InputMappingContext>(
      "IMC_Editor_Viewport");

    for (auto& feature : features_) {
      feature->RegisterBindings(input_system, ctx_);
    }

    // Priority matches the design doc (IMC_Editor_Viewport = 50).
    input_system.AddMappingContext(ctx_, 50);
    input_system.ActivateMappingContext(ctx_);

    LOG_F(INFO, "Initialized editor viewport navigation input mapping context");
    return true;
  }

  auto EditorViewportNavigation::Apply(oxygen::scene::SceneNode camera_node,
    const oxygen::input::InputSnapshot& input_snapshot,
    glm::vec3& focus_point,
    float dt_seconds) noexcept -> void {
    for (auto& feature : features_) {
      feature->Apply(camera_node, input_snapshot, focus_point, dt_seconds);
    }
  }

} // namespace oxygen::interop::module
