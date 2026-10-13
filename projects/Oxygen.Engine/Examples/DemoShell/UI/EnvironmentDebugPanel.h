//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <string_view>

#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/EnvironmentVm.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

//! Edits the scene environment: sun, fog, local fog volumes, sky atmosphere,
//! sky sphere and sky light.
/*!
 Every control runs a `demo.env.*` command (see `EnvironmentPanelCommands`).
 Text fields keep a local draft until they lose focus.
*/
class EnvironmentDebugPanel final : public DemoPanel {
public:
  EnvironmentDebugPanel(
    observer_ptr<EnvironmentVm> vm, observer_ptr<PanelCommands> commands);
  ~EnvironmentDebugPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(EnvironmentDebugPanel)
  OXYGEN_MAKE_NON_MOVABLE(EnvironmentDebugPanel)

  //! Draws the panel content without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnRegistered() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  //=== UI Drawing Methods ===-----------------------------------------------//
  void DrawSkyAtmosphereSection();
  void DrawSkySphereSection();
  void DrawSkyLightSection();
  void DrawSkyLightFeedback();
  void DrawFog();
  void DrawLocalFogVolumes();
  void DrawSunSection();
  void DrawRuntimeStateSection();

  observer_ptr<EnvironmentVm> environment_vm_;
  observer_ptr<PanelCommands> commands_;

  // Skybox load UI (disk -> cooked runtime cubemap)
  std::array<char, 260> skybox_path_ {};
  bool collapse_state_loaded_ { false };
  bool sun_section_open_ { true };
  bool sky_atmo_section_open_ { true };
  bool sky_sphere_section_open_ { false };
  bool sky_light_section_open_ { false };
};

} // namespace oxygen::examples::ui
