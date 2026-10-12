//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/SurfaceLayerSource.h>
#include <Oxygen/Vortex/Types/CompositingTask.h>

namespace oxygen::graphics {
class Framebuffer;
class Surface;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {
class Renderer;
} // namespace oxygen::vortex

namespace oxygen::vortex::internal {

//! Registered surface layer sources and the layer textures they draw into.
class SurfaceLayers {
public:
  //! A surface composited this frame.
  struct Target {
    CompositionView::SurfaceRouteId surface_id {
      CompositionView::kDefaultSurfaceRoute
    };
    std::shared_ptr<graphics::Framebuffer> composite_target;
    std::shared_ptr<graphics::Surface> target_surface;
  };

  //! A recorded layer, ready to blend over its surface.
  struct Composition {
    CompositionSubmission submission;
    std::shared_ptr<graphics::Surface> target_surface;
  };

  SurfaceLayers() = default;
  ~SurfaceLayers() = default;

  OXYGEN_MAKE_NON_COPYABLE(SurfaceLayers)
  OXYGEN_MAKE_NON_MOVABLE(SurfaceLayers)

  [[nodiscard]] auto Register(CompositionView::SurfaceRouteId surface,
    observer_ptr<SurfaceLayerSource> source) -> SurfaceLayerToken;
  auto Unregister(SurfaceLayerToken token) -> void;

  //! Records every active source whose surface is among the targets.
  [[nodiscard]] auto Record(Renderer& renderer, std::span<const Target> targets)
    -> std::vector<Composition>;

  //! Drops every registration and layer texture.
  auto Clear() noexcept -> void;

private:
  struct Layer {
    SurfaceLayerToken token { kInvalidSurfaceLayerToken };
    CompositionView::SurfaceRouteId surface_id {
      CompositionView::kDefaultSurfaceRoute
    };
    observer_ptr<SurfaceLayerSource> source;
    std::shared_ptr<graphics::Texture> texture;
    std::shared_ptr<graphics::Framebuffer> framebuffer;
  };

  [[nodiscard]] static auto EnsureLayerTarget(Renderer& renderer, Layer& layer,
    std::uint32_t width, std::uint32_t height) -> bool;
  [[nodiscard]] static auto RecordLayer(Renderer& renderer, const Layer& layer)
    -> bool;

  std::mutex mutex_;
  std::vector<Layer> layers_;
  std::uint64_t next_token_ { 1U };
};

} // namespace oxygen::vortex::internal
