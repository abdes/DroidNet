//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>

namespace oxygen {

//! Immutable configuration for path resolution.
/*!
 Provides the only serialized/shared input required to resolve engine paths.

 This is intentionally immutable after construction:
 - No setters
 - Private data members
 - Copying is allowed (value semantics)

 @note Prefer sharing a single instance via `std::shared_ptr<const
 PathFinderConfig>` for long-lived subsystem wiring.
*/
class PathFinderConfig final {
public:
  class Builder;

  [[nodiscard]] static auto Create() -> Builder;

  //! Constructs a default configuration.
  PathFinderConfig() = default;

  ~PathFinderConfig() = default;

  OXYGEN_DEFAULT_COPYABLE(PathFinderConfig)
  OXYGEN_DEFAULT_MOVABLE(PathFinderConfig)

  [[nodiscard]] auto WorkspaceRootPath() const noexcept
    -> const std::filesystem::path&
  {
    return workspace_root_path_;
  }

  [[nodiscard]] auto ShaderLibraryPath() const noexcept
    -> const std::filesystem::path&
  {
    return shader_library_path_;
  }

  [[nodiscard]] auto CVarsArchivePath() const noexcept
    -> const std::filesystem::path&
  {
    return cvars_archive_path_;
  }

private:
  //! Default shader library path for repo-local development.
  static constexpr std::string_view kDefaultShaderLibraryPath
    = "bin/Oxygen/shaders.bin";

  //! Default CVars archive path for repo-local development.
  static constexpr std::string_view kDefaultCVarsArchivePath
    = "bin/Oxygen/cvars.json";

  std::filesystem::path workspace_root_path_;
  std::filesystem::path shader_library_path_ {
    std::string(kDefaultShaderLibraryPath),
  };
  std::filesystem::path cvars_archive_path_ {
    std::string(kDefaultCVarsArchivePath),
  };
};

class PathFinderConfig::Builder final {
public:
  Builder() = default;
  ~Builder() = default;

  OXYGEN_DEFAULT_COPYABLE(Builder)
  OXYGEN_DEFAULT_MOVABLE(Builder)

  [[nodiscard]] auto WithWorkspaceRoot(
    std::filesystem::path workspace_root) && -> Builder&&
  {
    config_.workspace_root_path_ = std::move(workspace_root);
    return std::move(*this);
  }

  [[nodiscard]] auto WithShaderLibraryPath(
    std::filesystem::path shader_library_path) && -> Builder&&
  {
    config_.shader_library_path_ = std::move(shader_library_path);
    return std::move(*this);
  }

  [[nodiscard]] auto Build() && -> PathFinderConfig
  {
    return std::move(config_);
  }

  [[nodiscard]] auto WithCVarsArchivePath(
    std::filesystem::path cvars_archive_path) && -> Builder&&
  {
    config_.cvars_archive_path_ = std::move(cvars_archive_path);
    return std::move(*this);
  }

  [[nodiscard]] auto BuildShared() && -> std::shared_ptr<const PathFinderConfig>
  {
    return std::make_shared<const PathFinderConfig>(std::move(config_));
  }

private:
  PathFinderConfig config_;
};

[[nodiscard]] inline auto PathFinderConfig::Create() -> Builder
{
  return Builder {};
}

} // namespace oxygen
