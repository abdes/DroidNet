//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Module.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Platform/Types.h"
#include "Oxygen/Renderers/Common/Types.h"

class MainModule final : public oxygen::core::Module
{
 public:
  template <typename... Args>
  explicit MainModule(
    oxygen::PlatformPtr platform,
    oxygen::EngineWeakPtr engine,
    oxygen::platform::WindowPtr window,
    Args&&... ctor_args)
    : Module("MainModule", std::move(engine), std::forward<Args>(ctor_args)...)
    , platform_(std::move(platform))
    , my_window_(std::move(window))
  {
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVEABLE(MainModule);

  void OnInitialize(const oxygen::Renderer* renderer) override;

  void ProcessInput(const oxygen::platform::InputEvent& event) override;
  void Update(oxygen::Duration delta_time) override;
  void FixedUpdate() override;
  void Render(const oxygen::Renderer* renderer) override;

  void OnShutdown() noexcept override;

 private:
  [[nodiscard]] auto RenderGame(
    const oxygen::Renderer* renderer,
    const oxygen::renderer::RenderTarget& render_target) const
    -> oxygen::renderer::CommandLists;

  oxygen::PlatformPtr platform_ {};

  oxygen::renderer::SurfacePtr surface_ {};
  // TODO: hack for ImGui - redesign surfaces
  oxygen::platform::WindowPtr my_window_ {};
};
