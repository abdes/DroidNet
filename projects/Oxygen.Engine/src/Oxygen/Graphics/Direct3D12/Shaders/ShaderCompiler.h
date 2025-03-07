//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <wrl/client.h>

#include <Oxygen/Graphics/Common/ShaderCompiler.h>
#include <Oxygen/Graphics/Common/Shaders.h>

struct IDxcCompiler3;
struct IDxcUtils;
struct IDxcIncludeHandler;

namespace oxygen::graphics {

class IShaderByteCode;

namespace d3d12 {

    class ShaderCompiler : public graphics::ShaderCompiler {
        using Base = graphics::ShaderCompiler;

    public:
        explicit ShaderCompiler(Config config);

        ~ShaderCompiler() override;

        OXYGEN_MAKE_NON_COPYABLE(ShaderCompiler);
        OXYGEN_DEFAULT_MOVABLE(ShaderCompiler);

        [[nodiscard]] auto CompileFromSource(
            const std::u8string& shader_source,
            const ShaderInfo& shader_info) const -> std::unique_ptr<IShaderByteCode> override;

    private:
        // Compiler and Utils
        Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
        Microsoft::WRL::ComPtr<IDxcUtils> utils_;

        // Default include handler
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> include_processor_;
    };

} // namespace d3d12

} // namespace oxygen::graphics
