//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include <Oxygen/Base/Mixin.h>
#include <Oxygen/Base/MixinInitialize.h>
#include <Oxygen/Base/MixinNamed.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

struct ShaderCompilerConfig {
    //! A map of defines to be used when compiling shaders. The key is the name
    //! of the define and the value is an optional value for the define, the
    //! empty string means no value.
    std::map<std::wstring, std::wstring> global_defines {};
};

class ShaderCompiler
    : public Mixin<ShaderCompiler, Curry<MixinNamed, const char*>::mixin, MixinInitialize> {
public:
    template <typename... Args>
    explicit ShaderCompiler(const char* name, ShaderCompilerConfig config, Args&&... args)
        : Mixin(name, std::forward<Args>(args)...)
        , config_(std::move(config))
    {
    }

    template <typename... Args>
    explicit ShaderCompiler(ShaderCompilerConfig config, Args&&... args)
        : Mixin("Shader Compiler", std::forward<Args>(args)...)
        , config_(std::move(config))
    {
    }

    template <typename... Args>
    explicit ShaderCompiler(const char* name, Args&&... args)
        : Mixin(name, std::forward<Args>(args)...)
    {
    }

    template <typename... Args>
    explicit ShaderCompiler(Args&&... args)
        : Mixin("Shader Compiler", std::forward<Args>(args)...)
    {
    }

    OXYGEN_GFX_API ~ShaderCompiler() override = default;

    OXYGEN_MAKE_NON_COPYABLE(ShaderCompiler);
    OXYGEN_DEFAULT_MOVABLE(ShaderCompiler);

    //! Compiles a shader from a file.
    /*!
     \param shader_full_path The path to the file containing the shader source,
            encoded in UTF-8.
     \param shader_profile The shader profile containing information that
            will drive how the shader will be compiled.
     \return A unique pointer to the shader byte code; `nullptr` if the shader
             could not be compiled.
    */
   [[nodiscard]] OXYGEN_GFX_API virtual auto CompileFromFile(
        const std::filesystem::path& shader_full_path,
        const ShaderProfile& shader_profile) const -> std::unique_ptr<IShaderByteCode>;

    //! Compiles a shader from a string.
    /*!
     \param shader_source The source code of the shader, encoded in UTF-8.
     \param shader_profile The shader profile containing information that
            will drive how the shader will be compiled.
     \return A unique pointer to the shader byte code; `nullptr` if the shader
             could not be compiled.
    */
    [[nodiscard]] virtual auto CompileFromSource(
        const std::u8string& shader_source,
        const ShaderProfile& shader_profile) const -> std::unique_ptr<IShaderByteCode>
        = 0;

protected:
    OXYGEN_GFX_API virtual void OnInitialize();
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; // Allow access to OnInitialize.

    ShaderCompilerConfig config_ {};
};

} // namespace oxygen
