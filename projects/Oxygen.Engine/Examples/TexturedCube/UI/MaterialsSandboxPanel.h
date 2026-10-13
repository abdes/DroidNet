//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include "DemoShell/UI/DemoPanel.h"
#include "TexturedCube/UI/MaterialsSandboxVm.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::textured_cube::ui {

//! Picks what the sphere and the cube are shaded with, imports textures and
//! assigns cooked textures, skyboxes and materials.
/*!
 Every control that changes the scene runs a `cube.*` command. The import
 settings are a draft until Import runs them as one command.
*/
class MaterialsSandboxPanel final : public oxygen::examples::DemoPanel {
public:
  MaterialsSandboxPanel(
    observer_ptr<MaterialsSandboxVm> vm, observer_ptr<PanelCommands> commands);
  ~MaterialsSandboxPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MaterialsSandboxPanel)
  OXYGEN_MAKE_NON_MOVABLE(MaterialsSandboxPanel)

  // DemoPanel interface
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto DrawContents() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;

private:
  auto DrawImportSection() -> void;
  auto DrawMaterialsSection() -> void;
  auto DrawBrowserSection() -> void;
  auto DrawMaterialBrowserSection() -> void;

  observer_ptr<MaterialsSandboxVm> vm_;
  observer_ptr<PanelCommands> commands_;
};

} // namespace oxygen::examples::textured_cube::ui
