//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>

#include "DemoView.h"

namespace oxygen::examples::multiview {

class MainView final : public DemoView {
public:
  MainView();
  ~MainView() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainView)
  OXYGEN_DEFAULT_MOVABLE(MainView)

  void Initialize(scene::Scene& scene) override;

  void OnSceneMutation() override;

  auto OnPreRender(engine::Renderer& renderer) -> co::Co<> override;

  auto RenderFrame(const engine::RenderContext& render_ctx,
    graphics::CommandRecorder& recorder) -> co::Co<> override;

  void Composite(graphics::CommandRecorder& recorder,
    graphics::Texture& backbuffer) override;

protected:
  void OnReleaseResources() override;

private:
  void EnsureMainRenderTargets();
};

} // namespace oxygen::examples::multiview
