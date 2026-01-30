//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"
#include "TexturedCube/UI/TextureBrowserVm.h"

namespace oxygen::examples::textured_cube::ui {

class TextureBrowserPanel final : public oxygen::examples::DemoPanel {
public:
  TextureBrowserPanel() = default;
  ~TextureBrowserPanel() override = default;

  void Initialize(observer_ptr<TextureBrowserVm> vm);

  // DemoPanel interface
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  auto GetIcon() const noexcept -> std::string_view override;
  auto DrawContents() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;

private:
  auto DrawImportSection() -> void;
  auto DrawMaterialsSection() -> void;
  auto DrawBrowserSection() -> void;

  observer_ptr<TextureBrowserVm> vm_;
};

} // namespace oxygen::examples::textured_cube::ui
