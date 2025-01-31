//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <span>
#include <string>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Base/MixinShutdown.h"
#include "Oxygen/Graphics/Common/Forward.h"
#include "Oxygen/Graphics/Common/ShaderByteCode.h"
#include "Oxygen/Graphics/Common/Shaders.h"
#include "Oxygen/Graphics/Common/api_export.h"

namespace oxygen::graphics {

struct CompiledShaderInfo {
    ShaderType shader_type;
    std::string shader_unique_id;
    std::string source_file_path;
    uint64_t source_hash;
    size_t compiled_bloc_size;
    std::chrono::system_clock::time_point compile_time;

    // Default constructor
    CompiledShaderInfo() = default;

    // Parameterized constructor
    CompiledShaderInfo(
        const ShaderType type,
        std::string unique_id,
        std::string path,
        const uint64_t hash,
        const size_t size,
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

struct ShaderManagerConfig {
    std::string renderer_name { "Default" };
    std::string archive_file_name { "shaders.bin" };

    std::optional<std::string> archive_dir {};
    std::optional<std::string> source_dir {};

    std::span<const ShaderProfile> shaders {};

    ShaderCompilerPtr compiler {};
};

class ShaderManager
    : public Mixin<ShaderManager,
          Curry<MixinNamed, const char*>::mixin,
          MixinShutdown,
          MixinInitialize // Last to consume remaining args
          > {
public:
    template <typename... Args>
    explicit ShaderManager(ShaderManagerConfig config, Args&&... args)
        : Mixin((std::string("ShaderManager for ") + config.renderer_name).c_str(), std::forward<Args>(args)...)
        , config_(std::move(config))
    {
    }

    OXYGEN_GFX_API ~ShaderManager() override = default;

    OXYGEN_MAKE_NON_COPYABLE(ShaderManager);
    OXYGEN_DEFAULT_MOVABLE(ShaderManager);

    // Core archive operations
    //! Loads shader bytecode and metadata from the archive file
    OXYGEN_GFX_API [[nodiscard]] void Load();
    //! Persists shader bytecode and metadata to the archive file
    OXYGEN_GFX_API [[nodiscard]] void Save() const;
    //! Removes all shaders from the archive
    OXYGEN_GFX_API void Clear() noexcept;

    // Shader management
    //! Adds pre-compiled shader bytecode to the archive
    OXYGEN_GFX_API [[nodiscard]] bool AddCompiledShader(CompiledShader shader);
    //! Retrieves compiled shader bytecode by name
    OXYGEN_GFX_API [[nodiscard]] std::shared_ptr<IShaderByteCode> GetShaderBytecode(
        std::string_view unique_id) const;

    // State queries
    //! Checks if a shader exists in the archive
    OXYGEN_GFX_API [[nodiscard]] bool HasShader(std::string_view unique_id) const noexcept;
    //! Returns profiles of all shaders that need recompilation
    OXYGEN_GFX_API [[nodiscard]] std::vector<ShaderProfile> GetOutdatedShaders() const;
    //! Returns the total number of shaders in the archive
    OXYGEN_GFX_API [[nodiscard]] size_t GetShaderCount() const noexcept;

    // Update operations
    //! Compiles all shaders whose source files have changed
    OXYGEN_GFX_API [[nodiscard]] void UpdateOutdatedShaders();
    //! Forces recompilation of all shaders in the archive
    OXYGEN_GFX_API [[nodiscard]] bool RecompileAll();

private:
    //! Initialize tha shader manager by loading the archive file and checking if
    //! it is up-to-date with the source files, compiling them as needed.
    OXYGEN_GFX_API void OnInitialize();
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; // Allow access to OnInitialize.

    OXYGEN_GFX_API void OnShutdown();
    template <typename Base>
    friend class MixinShutdown; // Allow access to OnShutdown.

    //! Checks if a shader source file has been modified since last compilation
    [[nodiscard]] bool IsShaderOutdated(const ShaderProfile& shader) const;

    //! Compiles shader from profile and adds it to the archive
    [[nodiscard]] bool CompileAndAddShader(const ShaderProfile& profile);

    ShaderManagerConfig config_ {};
    std::unordered_map<std::string, CompiledShader> shader_cache_;
    std::vector<ShaderProfile> shader_profiles_;
    std::filesystem::path archive_path_ {};
};

} // namespace oxygen::graphics
