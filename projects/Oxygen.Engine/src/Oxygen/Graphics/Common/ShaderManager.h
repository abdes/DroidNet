//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

struct CompiledShaderInfo {
  ShaderType shader_type { ShaderType::kVertex };
  std::string shader_unique_id;
  std::string source_file_path;
  uint64_t source_hash { 0 };
  size_t compiled_bloc_size { 0 };
  std::chrono::system_clock::time_point compile_time;

  // Default constructor
  CompiledShaderInfo() = default;

  // Parameterized constructor
  CompiledShaderInfo(const ShaderType type, std::string unique_id,
    std::string path, const uint64_t hash, const size_t size,
    const std::chrono::system_clock::time_point time) noexcept
    : shader_type(type)
    , shader_unique_id(std::move(unique_id))
    , source_file_path(std::move(path))
    , source_hash(hash)
    , compiled_bloc_size(size)
    , compile_time(time)
  {
  }
};

struct CompiledShader {
  CompiledShaderInfo info;
  std::shared_ptr<IShaderByteCode> bytecode;
};

class ShaderManager : public Composition {
public:
  struct Config {
    std::string backend_name { "Default" };
    std::string archive_file_name { "shaders.bin" };

    // TODO: make a final solution for shader locations
    // Currently these are relative paths to the project root.
    std::optional<std::string> archive_dir;
    std::optional<std::string> source_dir;

    //! Additional include directories for shader compilation.
    //!
    //! Paths are relative to the project root (same convention as
    //! @ref source_dir). The shader manager will always add @ref source_dir as
    //! an include directory when it is provided.
    std::vector<std::string> include_dirs;

    std::span<const ShaderInfo> shaders;

    std::shared_ptr<ShaderCompiler> compiler;
  };

  OXGN_GFX_API explicit ShaderManager(Config config)
    : config_(std::move(config))
  {
    AddComponent<ObjectMetadata>(
      std::string("ShaderManager for ") + config_.backend_name);
    Initialize();
  }

  OXGN_GFX_API ~ShaderManager() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ShaderManager);
  OXYGEN_DEFAULT_MOVABLE(ShaderManager);

  // Core archive operations
  //! Loads shader bytecode and metadata from the archive file
  OXGN_GFX_API auto Load() -> void;
  //! Persists shader bytecode and metadata to the archive file
  OXGN_GFX_API auto Save() const -> void;
  //! Removes all shaders from the archive
  OXGN_GFX_API auto Clear() noexcept -> void;

  // Shader management
  //! Adds pre-compiled shader bytecode to the archive
  OXGN_GFX_NDAPI auto AddCompiledShader(CompiledShader shader) -> bool;
  //! Retrieves compiled shader bytecode by name
  OXGN_GFX_NDAPI auto GetShaderBytecode(std::string_view unique_id) const
    -> std::shared_ptr<IShaderByteCode>;

  // State queries
  //! Checks if a shader exists in the archive
  OXGN_GFX_NDAPI auto HasShader(std::string_view unique_id) const noexcept
    -> bool;
  //! Returns profiles of all shaders that need recompilation
  OXGN_GFX_NDAPI auto GetOutdatedShaders() const -> std::vector<ShaderInfo>;
  //! Returns the total number of shaders in the archive
  OXGN_GFX_NDAPI auto GetShaderCount() const noexcept -> size_t;

  // Update operations
  //! Compiles all shaders whose source files have changed
  OXGN_GFX_API auto UpdateOutdatedShaders() -> void;
  //! Forces recompilation of all shaders in the archive
  OXGN_GFX_NDAPI auto RecompileAll() -> bool;

  [[nodiscard]] auto GetName() const noexcept
  {
    return GetComponent<ObjectMetadata>().GetName();
  }

private:
  //! Initialize tha shader manager by loading the archive file and checking if
  //! it is up-to-date with the source files, compiling them as needed.
  OXGN_GFX_API auto Initialize() -> void;

  //! Checks if a shader source file has been modified since last compilation
  [[nodiscard]] auto IsShaderOutdated(const ShaderInfo& shader) const -> bool;

  //! Compiles shader from profile and adds it to the archive
  [[nodiscard]] auto CompileAndAddShader(const ShaderInfo& profile) -> bool;

  Config config_ {};
  std::unordered_map<std::string, CompiledShader> shader_cache_;
  std::vector<ShaderInfo> shader_infos_;
  std::filesystem::path archive_path_;
};

} // namespace oxygen::graphics
