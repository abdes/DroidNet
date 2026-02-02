//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/EnvironmentVm.h"

namespace oxygen::examples::ui {

//! Configuration for the environment debug panel.
struct EnvironmentDebugConfig {
  //! Environment view model.
  observer_ptr<EnvironmentVm> environment_vm { nullptr };
};

class EnvironmentDebugPanel final : public DemoPanel {
public:
  EnvironmentDebugPanel() = default;
  ~EnvironmentDebugPanel() override = default;

  EnvironmentDebugPanel(const EnvironmentDebugPanel&) = delete;
  auto operator=(const EnvironmentDebugPanel&)
    -> EnvironmentDebugPanel& = delete;
  EnvironmentDebugPanel(EnvironmentDebugPanel&&) = default;
  auto operator=(EnvironmentDebugPanel&&) -> EnvironmentDebugPanel& = default;

  //! Initialize or update the panel configuration.
  void Initialize(const EnvironmentDebugConfig& config);

  //! Update configuration (e.g., when scene changes).
  void UpdateConfig(const EnvironmentDebugConfig& config);

  //! Draws the panel content without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnRegistered() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

  //! Returns true if there are pending changes to apply.
  [[nodiscard]] auto HasPendingChanges() const -> bool;

  //! Apply pending changes to the scene. Call during OnSceneMutation.
  void ApplyPendingChanges();

  //! Request that the panel resync its cached state from the scene.
  /*!
   This is useful when an external system (e.g. an async skybox load) modifies
   the scene environment outside of `ApplyPendingChanges`.
  */
  void RequestResync();

private:
  //=== UI Drawing Methods ===-----------------------------------------------//
  void DrawSkyAtmosphereSection();
  void DrawSkySphereSection();
  void DrawSkyLightSection();
  // NOTE: Fog UI removed - use Aerial Perspective instead. Real fog TBD.
  void DrawSunSection();
  void DrawRendererDebugSection();

  //=== Configuration ===----------------------------------------------------//
  EnvironmentDebugConfig config_ {};
  observer_ptr<EnvironmentVm> environment_vm_ { nullptr };
  bool initialized_ { false };

  // Skybox load UI (disk -> synthetic cubemap)
  std::array<char, 260> skybox_path_ {};
};

} // namespace oxygen::examples::ui
