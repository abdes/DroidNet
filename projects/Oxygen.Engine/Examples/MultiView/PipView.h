//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/OxCo/Co.h>

#include "MultiView/DemoView.h"

namespace oxygen {
namespace engine {
  class Renderer;
  struct RenderContext;
} // namespace engine
namespace graphics {
  class CommandRecorder;
  class Texture;
} // namespace graphics
namespace scene {
  class Scene;
} // namespace scene
} // namespace oxygen

namespace oxygen::examples::multiview {

class PipView final : public DemoView {
public:
  PipView();
  ~PipView() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PipView)
  OXYGEN_DEFAULT_MOVABLE(PipView)

  void Initialize(scene::Scene& scene) override;

  void OnSceneMutation() override;

  auto OnPreRender(oxygen::engine::Renderer& renderer) -> co::Co<> override;

  auto RenderFrame(const engine::RenderContext& render_ctx,
    graphics::CommandRecorder& recorder) -> co::Co<> override;

  void Composite(graphics::CommandRecorder& recorder,
    graphics::Texture& backbuffer) override;

protected:
  void OnReleaseResources() override;

private:
  void EnsurePipRenderTargets(const SubPixelExtent& viewport_extent);

  static auto ComputePipExtent(const PixelExtent& surface_extent)
    -> PixelExtent;

  static auto ComputePipViewport(const PixelExtent& surface_extent) -> ViewPort;

  std::optional<ViewPort> viewport_;
};

} // namespace oxygen::examples::multiview
