//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Common/ShaderCompiler.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Graphics/Common/ShaderByteCode.h"
#include "Oxygen/Graphics/Common/Shaders.h"

using namespace oxygen::graphics;
using namespace std::filesystem;

auto ShaderCompiler::CompileFromFile(
    const path& shader_full_path,
    const ShaderProfile& shader_profile) const
    -> std::unique_ptr<IShaderByteCode>
{
    if (std::error_code ec; !exists(shader_full_path, ec) || ec) {
        LOG_F(ERROR, "Shader file not found: {}", shader_full_path.string());
        return nullptr;
    }

    try {
        // Open file in binary mode to preserve UTF-8 encoding
        std::ifstream file(shader_full_path, std::ios::in | std::ios::binary);
        if (!file) {
            LOG_F(ERROR, "Failed to open shader file: {}", shader_full_path.string());
            return nullptr;
        }

        // Read file content into string
        const std::string buffer(std::istreambuf_iterator(file), {});
        file.close();

        // Convert std::string to std::u8string for UTF-8
        const std::u8string shader_source(reinterpret_cast<const char8_t*>(buffer.c_str()), buffer.length());

        return CompileFromSource(shader_source, shader_profile);
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to compile shader `{}`: {}", MakeShaderIdentifier(shader_profile.type, shader_full_path.string()), e.what());
        return nullptr;
    }
}

void ShaderCompiler::OnInitialize()
{
}
