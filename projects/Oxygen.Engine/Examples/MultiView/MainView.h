//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "DemoView.h"

namespace oxygen::examples::multiview {

class MainView final : public DemoView {
public:
  MainView();
  ~MainView() override = default;

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
  void EnsureMainRenderTargets();
};

} // namespace oxygen::examples::multiview
