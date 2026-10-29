//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

  //! Source mount folders by mount name; names compare exactly.
  using SourceMountTable
    = std::map<std::string, std::filesystem::path, std::less<>>;

  [[nodiscard]] static auto Create() -> Builder;

  [[nodiscard]] static constexpr auto DefaultShaderBuildConfigName() noexcept
    -> std::string_view
  {
#ifndef NDEBUG
    return "Debug";
#else
    return "Release";
#endif
  }

  [[nodiscard]] static constexpr auto DefaultShaderModeName() noexcept
    -> std::string_view
  {
#ifndef NDEBUG
    return "dev";
#else
    return "production";
#endif
  }

  [[nodiscard]] static auto DefaultShaderLibraryPathForCurrentBuild()
    -> std::filesystem::path
  {
    return std::filesystem::path(std::string("bin/Oxygen/")
      + std::string(DefaultShaderBuildConfigName()) + "/"
      + std::string(DefaultShaderModeName()) + "/shaders.bin");
  }

  //! Repo-local path of the engine content pak the engine build produces for
  //! the current configuration; an installed SDK ships it as
  //! `share/oxygen/engine.pak`.
  [[nodiscard]] static auto DefaultEngineContentPakPathForCurrentBuild()
    -> std::filesystem::path
  {
    return std::filesystem::path(std::string("bin/Oxygen/")
      + std::string(DefaultShaderBuildConfigName()) + "/engine.pak");
  }

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

  //! The engine content pak, mounted beneath every other content source.
  [[nodiscard]] auto EngineContentPakPath() const noexcept
    -> const std::filesystem::path&
  {
    return engine_content_pak_path_;
  }

  //! The folder of each source mount, by mount name. External script sources
  //! are mount paths, `/<Mount>/<path in mount>`, resolved through this table;
  //! hot reload watches these folders.
  [[nodiscard]] auto SourceMounts() const noexcept -> const SourceMountTable&
  {
    return source_mounts_;
  }

  //! The path to the persistent bytecode cache file (e.g., scripts.bin).
  [[nodiscard]] auto ScriptBytecodeCachePath() const noexcept
    -> const std::filesystem::path&
  {
    return script_bytecode_cache_path_;
  }

private:
  //! Default CVars archive path for repo-local development.
  static constexpr std::string_view kDefaultCVarsArchivePath
    = "bin/Oxygen/cvars.json";

  //! Default bytecode cache path.
  static constexpr std::string_view kDefaultBytecodeCachePath
    = "bin/Oxygen/scripts.bin";

  std::filesystem::path workspace_root_path_;
  std::filesystem::path shader_library_path_ {
    DefaultShaderLibraryPathForCurrentBuild(),
  };
  std::filesystem::path cvars_archive_path_ {
    std::string(kDefaultCVarsArchivePath),
  };
  std::filesystem::path engine_content_pak_path_ {
    DefaultEngineContentPakPathForCurrentBuild(),
  };
  SourceMountTable source_mounts_;
  std::filesystem::path script_bytecode_cache_path_ {
    std::string(kDefaultBytecodeCachePath),
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

  //! Binds source mount `name` to `folder`, replacing an earlier binding.
  [[nodiscard]] auto WithSourceMount(
    std::string name, std::filesystem::path folder) && -> Builder&&
  {
    config_.source_mounts_.insert_or_assign(std::move(name), std::move(folder));
    return std::move(*this);
  }

  [[nodiscard]] auto WithSourceMounts(SourceMountTable mounts) && -> Builder&&
  {
    config_.source_mounts_ = std::move(mounts);
    return std::move(*this);
  }

  [[nodiscard]] auto WithScriptBytecodeCachePath(
    std::filesystem::path path) && -> Builder&&
  {
    config_.script_bytecode_cache_path_ = std::move(path);
    return std::move(*this);
  }

  [[nodiscard]] auto WithEngineContentPakPath(
    std::filesystem::path engine_content_pak_path) && -> Builder&&
  {
    config_.engine_content_pak_path_ = std::move(engine_content_pak_path);
    return std::move(*this);
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
