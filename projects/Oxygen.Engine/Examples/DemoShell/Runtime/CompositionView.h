//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::examples {

//! Defines the rendering and composition intent for a single display layer.
/*!
  This descriptor is the primary public interface for requesting work from the
  RenderingPipeline. It decouples the user's intent from the pipeline's
  internal resource management (pooling, HDR intermediate buffers, etc.).
*/
struct CompositionView {

  //! Strongly typed Z-order value for view composition.
  using ZOrder = NamedType<int32_t, struct ZOrderTag,
    // clang-format off
    DefaultInitialized,
    Arithmetic>; // clang-format on

  //! The absolute background layer (e.g., Skybox, Backdrop).
  static constexpr ZOrder kZOrderBackground { 0 };
  //! The primary shaded world content, strictly above the backdrop.
  static constexpr ZOrder kZOrderScene { 1 };
  //! The standard Game UI layer (main menus, inventory, primary HUD).
  static constexpr ZOrder kZOrderGameUI { 1000 };

  //! The absolute highest layer (e.g., Engine ImGui tools).
  static constexpr ZOrder kZOrderTools {
    (std::numeric_limits<ZOrder::UnderlyingType>::max)() - 1
  };
  //! Debug gizmos and overlays, strictly below the engine tools.
  static constexpr ZOrder kZOrderDebugOverlay {
    (std::numeric_limits<ZOrder::UnderlyingType>::max)() - 2
  };

  //! Human-readable identifier for diagnostics and telemetry.
  std::string_view name;

  //! Unique identifier for the view session. Used by the pipeline to
  //! track and reuse persistent GPU resources (textures, framebuffers)
  //! across multiple frames.
  ViewId id { kInvalidViewId };

  //! Core view configuration (Viewport, Scissors, Jitter, etc.).
  View view;

  //! Composition stacking order.
  //! - Lower values are rendered further back (closer to the background).
  //! - Use symbolic constants for fixed-order layers (Background, Scene,
  //! Tools).
  //! - If multiple views have the same Z-order, the submission order (the order
  //!   in the input span provided to the pipeline) acts as the tie-breaker,
  //!   with later views appearing on top.
  ZOrder z_order { kZOrderScene };

  //! Layer transparency (0.0 to 1.0) applied during final blend.
  float opacity { 1.0F };

  //! Determines if the view's intermediate buffer is wiped.
  //! Set to false for layers intended to 'paint over' the existing content
  //! of the intermediate buffer.
  bool should_clear { true };

  //! Optional 3D source. If set, the RenderingPipeline executes the full
  //! PBR scene rendering sequence for this view.
  std::optional<scene::SceneNode> camera;

  //! The color value applied during the GPU clear operation.
  graphics::Color clear_color { 0.0F, 0.0F, 0.0F, 1.0F };

  //! HDR Policy:
  //! - true: High precision intermediate (PBR Shading, HDR passes,
  //! Tonemapping).
  //! - false: Standard precision intermediate (Fast 2D/UI, Overlays).
  bool enable_hdr { true };

  //! Override to force wireframe rendering for this specific view.
  bool force_wireframe { false };

  //! Callback for recording view-specific SDR commands (HUD, Gizmos, ImGui).
  //! Executed in the correct hardware phase (Post-Tonemap for HDR views).
  std::function<void(graphics::CommandRecorder&)> on_overlay;

  /*
    FUTURE EXTENSIONS:
    - Temporal History: Persistent ViewId allows the pipeline to cache motion
    vectors and history buffers across frames for TAA, Motion Blur, and
    Upscaling.
    - Pipeline Overrides: Support for per-view shader permutations or quality
    settings.
    - View Dependencies: Explicit graph-based dependencies (e.g., Reflection
    view must complete before Main Scene view).
  */

  // --- Static Factory Helpers ---

  //! Creates an HDR scene view at its mandatory Z-order.
  static auto ForScene(ViewId id, View view, scene::SceneNode camera)
    -> CompositionView
  {
    return CompositionView {
      .name = "Scene",
      .id = id,
      .view = view,
      .z_order = kZOrderScene,
      .camera = std::move(camera),
      .enable_hdr = true,
      .on_overlay = {},
    };
  }

  //! Creates an HDR Picture-in-Picture or Minimap.
  //! @param z_order Must be > kZOrderScene and < kZOrderDebugOverlay.
  static auto ForPip(ViewId id, ZOrder z_order, View view,
    scene::SceneNode camera) -> CompositionView
  {
    if (z_order <= kZOrderScene || z_order >= kZOrderDebugOverlay) {
      LOG_F(ERROR, "PiP Z-order must be between Scene and Overlays");
    }
    return CompositionView {
      .name = "PiP",
      .id = id,
      .view = view,
      .z_order = z_order,
      .camera = std::move(camera),
      .enable_hdr = true,
      .on_overlay = {},
    };
  }

  //! Creates a standard Game UI layer (menus, inventory).
  static auto ForGameUI(ViewId id, View view,
    std::function<void(graphics::CommandRecorder&)> on_overlay)
    -> CompositionView
  {
    return CompositionView {
      .name = "GameUI",
      .id = id,
      .view = view,
      .z_order = kZOrderGameUI,
      .camera = {},
      .clear_color = { 0.0F, 0.0F, 0.0F, 0.0F },
      .enable_hdr = false,
      .on_overlay = std::move(on_overlay),
    };
  }

  //! Creates a transparent SDR HUD (Heads-Up Display) layer.
  //! @param z_order Must be > kZOrderScene and < kZOrderDebugOverlay.
  static auto ForHud(ViewId id, ZOrder z_order, View view,
    std::function<void(graphics::CommandRecorder&)> on_overlay)
    -> CompositionView
  {
    if (z_order <= kZOrderScene || z_order >= kZOrderDebugOverlay) {
      LOG_F(ERROR, "HUD Z-order must be between Scene and Overlays");
    }
    return CompositionView {
      .name = "HUD",
      .id = id,
      .view = view,
      .z_order = z_order,
      .camera = {},
      .clear_color = { 0.0F, 0.0F, 0.0F, 0.0F },
      .enable_hdr = false,
      .on_overlay = std::move(on_overlay),
    };
  }

  //! Creates a designated ImGui layer at its mandatory highest Z-order.
  static auto ForImGui(ViewId id, View view,
    std::function<void(graphics::CommandRecorder&)> on_overlay)
    -> CompositionView
  {
    return CompositionView {
      .name = "ImGui",
      .id = id,
      .view = view,
      .z_order = kZOrderTools,
      .camera = {},
      .clear_color = { 0.0F, 0.0F, 0.0F, 0.0F },
      .enable_hdr = false,
      .on_overlay = std::move(on_overlay),
    };
  }

  //! Creates a non-clearing SDR overlay for gizmos at its mandatory Z-order.
  static auto ForOverlay(ViewId id, View view,
    std::function<void(graphics::CommandRecorder&)> on_overlay)
    -> CompositionView
  {
    return CompositionView {
      .name = "Overlay",
      .id = id,
      .view = view,
      .z_order = kZOrderDebugOverlay,
      .should_clear = false,
      .camera = {},
      .enable_hdr = false,
      .on_overlay = std::move(on_overlay),
    };
  }
};

//! Convert a ZOrder to a human-readable string.
inline auto to_string(const CompositionView::ZOrder& v) -> std::string
{
  switch (v.get()) {
  case CompositionView::kZOrderBackground.get():
    return "Z-Background";
  case CompositionView::kZOrderScene.get():
    return "Z-Scene";
  case CompositionView::kZOrderGameUI.get():
    return "Z-GameUI";
  case CompositionView::kZOrderTools.get():
    return "Z-Tools";
  case CompositionView::kZOrderDebugOverlay.get():
    return "Z-DebugOverlay";
  default:
    return "Z-" + std::to_string(v.get());
  }
}

} // namespace oxygen::examples
