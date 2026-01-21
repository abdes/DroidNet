//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Content/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Content/Tools/ImportTool/ImportCommand.h>
#include <Oxygen/Content/Tools/ImportTool/SceneImportSettings.h>

namespace oxygen::content::import::tool {

class GltfCommand final : public ImportCommand {
public:
  explicit GltfCommand(const GlobalOptions* global_options)
    : global_options_ { global_options }
  {
  }

  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run() -> int override;

private:
  const GlobalOptions* global_options_ = nullptr;
  SceneImportSettings options_ {};
  bool no_bake_transforms_ = false;
  bool no_import_textures_ = false;
  bool no_import_materials_ = false;
  bool no_import_geometry_ = false;
  bool no_import_scene_ = false;
};

} // namespace oxygen::content::import::tool
