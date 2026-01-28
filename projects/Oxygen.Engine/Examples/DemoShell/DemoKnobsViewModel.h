//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::examples {

//! Rendering fill modes for demo UIs.
enum class RenderMode { kSolid, kWireframe };

//! Camera control modes for demo UIs.
enum class CameraMode { kFly, kOrbit };

//! Shared view model for demo-wide knobs.
/*!
 Centralizes the small set of frequently used toggles and selections that are
 expected across many demos.

 ### Key Features

 - **Unified State**: Single source of truth for common knobs.
 - **Reusable**: Shared across demos via the DemoShell module.
*/
struct DemoKnobsViewModel {
  RenderMode render_mode { RenderMode::kSolid };
  CameraMode camera_mode { CameraMode::kFly };
  bool show_axes_widget { true };
  bool show_stats_fps { false };
  bool show_stats_frame_timing_detail { false };
};

} // namespace oxygen::examples
