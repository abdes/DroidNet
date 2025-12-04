//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "DemoView.h"

namespace oxygen::examples::multiview {

class PipView final : public DemoView {
public:
  PipView();
  ~PipView() override = default;

  void Initialize(scene::Scene& scene) override;

  void OnSceneMutation() override;

  auto OnPreRender(oxygen::engine::Renderer& renderer) -> co::Co<void> override;

  auto RenderToFramebuffer(const engine::RenderContext& render_ctx,
    graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> co::Co<void> override;

  void Composite(graphics::CommandRecorder& recorder,
    graphics::Texture& backbuffer) override;

  void ReleaseResources() override;

private:
  void EnsurePipRenderTargets(uint32_t width, uint32_t height);

  auto ComputePipExtent(int surface_width, int surface_height)
    -> std::pair<uint32_t, uint32_t>;

  auto ComputePipViewport(int surface_width, int surface_height,
    uint32_t pip_width, uint32_t pip_height) -> ViewPort;

  uint32_t target_width_ { 0 };
  uint32_t target_height_ { 0 };
  std::optional<ViewPort> destination_viewport_;
};

} // namespace oxygen::examples::multiview
