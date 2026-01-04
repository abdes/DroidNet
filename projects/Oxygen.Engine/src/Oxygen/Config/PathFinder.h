//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/PathFinderConfig.h>

namespace oxygen {

//! Stateless path resolver built from an immutable PathFinderConfig.
/*!
 Resolves workspace-root-relative paths and provides canonical include roots.

 The resolver is intentionally cheap to construct and can be instantiated in
 each module/DLL as needed.
*/
class PathFinder final {
public:
  explicit PathFinder(std::shared_ptr<const PathFinderConfig> config,
    std::filesystem::path working_directory)
    : config_(std::move(config))
    , working_directory_(std::move(working_directory))
  {
  }

  ~PathFinder() = default;

  OXYGEN_DEFAULT_COPYABLE(PathFinder)
  OXYGEN_DEFAULT_MOVABLE(PathFinder)

  [[nodiscard]] auto WorkspaceRoot() const -> std::filesystem::path
  {
    if (!config_->WorkspaceRootPath().empty()) {
      return config_->WorkspaceRootPath();
    }
    return working_directory_;
  }

  [[nodiscard]] auto ResolvePath(const std::filesystem::path& path) const
    -> std::filesystem::path
  {
    if (path.is_absolute()) {
      return path;
    }
    return WorkspaceRoot() / path;
  }

  [[nodiscard]] auto ShaderLibraryPath() const -> std::filesystem::path
  {
    return ResolvePath(config_->ShaderLibraryPath());
  }

  [[nodiscard]] auto ShaderIncludeRoots() const
    -> std::array<std::filesystem::path, 2>
  {
    const auto root = WorkspaceRoot();
    return {
      root / "src/Oxygen",
      root / "src/Oxygen/Graphics/Direct3D12/Shaders",
    };
  }

private:
  std::shared_ptr<const PathFinderConfig> config_;
  std::filesystem::path working_directory_;
};

} // namespace oxygen
