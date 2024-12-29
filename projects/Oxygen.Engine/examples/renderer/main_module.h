//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <oxygen/core/module.h>

#include "oxygen/base/Macros.h"
#include "oxygen/platform/Types.h"
#include "Oxygen/Renderers/Common/Types.h"

class MainModule : public oxygen::core::Module
{
public:
  explicit MainModule(oxygen::PlatformPtr platform);
  ~MainModule() override;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVEABLE(MainModule);

  void Initialize(const oxygen::Renderer& renderer) override;

  void ProcessInput(const oxygen::platform::InputEvent& event) override;
  void Update(oxygen::Duration delta_time) override;
  void FixedUpdate() override;
  void Render(const oxygen::Renderer& renderer) override;

  void Shutdown() noexcept override;

private:
  [[nodiscard]] auto RenderGame(
    const oxygen::Renderer& renderer,
    const oxygen::renderer::RenderTarget& render_target) const
    ->oxygen::renderer::CommandListPtr;

  oxygen::PlatformPtr platform_{};
  oxygen::renderer::SurfacePtr surface_{};
};
