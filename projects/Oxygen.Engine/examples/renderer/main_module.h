//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <oxygen/core/module.h>

#include "oxygen/base/macros.h"
#include "oxygen/platform/types.h"
#include "oxygen/renderer/types.h"

class MainModule : public oxygen::core::Module
{
public:
  explicit MainModule(oxygen::PlatformPtr platform);
  ~MainModule() override;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVEABLE(MainModule);

  void Initialize() override;

  void ProcessInput(const oxygen::platform::InputEvent& event) override;
  void Update(oxygen::Duration delta_time) override;
  void FixedUpdate() override;
  void Render() override;

  void Shutdown() noexcept override;

private:
  oxygen::PlatformPtr platform_;
  oxygen::RendererPtr renderer_;
  oxygen::SurfaceId surface_id_;
};
