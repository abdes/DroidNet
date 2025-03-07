//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class ShaderCompiler : public Composition {
public:
    struct Config {
        std::string name;

        //! A map of symbol definitions to be used when compiling shaders. The
        //! key is the symbol name. The value is optional, and the empty string
        //! means no value.
        std::map<std::wstring, std::wstring> global_defines;
    };

    explicit ShaderCompiler(Config config)
        : config_(std::move(config))
    {
        if (config_.name.empty()) {
            throw std::invalid_argument("ShaderCompiler name cannot be empty.");
        }

        AddComponent<ObjectMetaData>(config_.name);
    }

    OXYGEN_GFX_API ~ShaderCompiler() override = default;

    OXYGEN_MAKE_NON_COPYABLE(ShaderCompiler);
    OXYGEN_DEFAULT_MOVABLE(ShaderCompiler);

    //! Compiles a shader from a file.
    /*!
     \param shader_full_path The path to the file containing the shader source,
            encoded in UTF-8.
     \param shader_info The shader profile containing information that
            will drive how the shader will be compiled.
     \return A unique pointer to the shader byte code; `nullptr` if the shader
             could not be compiled.
    */
    [[nodiscard]] OXYGEN_GFX_API virtual auto CompileFromFile(
        const std::filesystem::path& shader_full_path,
        const ShaderInfo& shader_info) const -> std::unique_ptr<IShaderByteCode>;

    //! Compiles a shader from a string.
    /*!
     \param shader_source The source code of the shader, encoded in UTF-8.
     \param shader_info The shader profile containing information that
            will drive how the shader will be compiled.
     \return A unique pointer to the shader byte code; `nullptr` if the shader
             could not be compiled.
    */
    [[nodiscard]] virtual auto CompileFromSource(
        const std::u8string& shader_source,
        const ShaderInfo& shader_info) const -> std::unique_ptr<IShaderByteCode>
        = 0;

    [[nodiscard]] auto GetName() const noexcept
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

protected:
    Config config_ {};
};

}  // namespace oxygen::graphics
