//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <d3dcommon.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/ShaderCompiler.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCompiler.h>
#include <type_traits>

using Microsoft::WRL::ComPtr;
using oxygen::graphics::ShaderType;
using oxygen::graphics::d3d12::ShaderCompiler;
using oxygen::windows::ThrowOnFailed;

namespace {

void LogCompilationErrors(IDxcBlob* error_blob)
{
    if (error_blob != nullptr) {
        // Get the pointer to the error message and its size
        const auto* const error_message = static_cast<const char*>(error_blob->GetBufferPointer());
        const size_t error_message_size = error_blob->GetBufferSize();

        // Convert the error message to a string
        const std::string error_string(error_message, error_message_size);

        // Display the error message
        LOG_F(ERROR, "Shader compilation error: {}", error_string);
    }
}

constexpr auto GetProfileForShaderType(const ShaderType type) -> const wchar_t*
{
    switch (type) // NOLINT(clang-diagnostic-switch-enum)
    {
    case ShaderType::kVertex:
        return L"vs_6_8";
    case ShaderType::kGeometry:
        return L"gs_6_8";
    case ShaderType::kHull:
        return L"hs_6_8";
    case ShaderType::kDomain:
        return L"ds_6_8";
    case ShaderType::kPixel:
        return L"ps_6_8";
    case ShaderType::kCompute:
        return L"cs_6_8";
    case ShaderType::kMesh:
        return L"ms_6_8";
    case ShaderType::kAmplification:
        return L"as_6_8";
    default:;
    }
    LOG_F(ERROR, "Invalid shader type");
    throw std::invalid_argument("Invalid shader type");
}
} // namespace

ShaderCompiler::ShaderCompiler(Config config)
    : Base(std::forward<Config>(config))
{
    ThrowOnFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_)));
    ThrowOnFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_)));
    ThrowOnFailed(utils_->CreateDefaultIncludeHandler(include_processor_.GetAddressOf()));
}

ShaderCompiler::~ShaderCompiler() = default;

auto ShaderCompiler::CompileFromSource(
    const std::u8string& shader_source,
    const ShaderInfo& shader_info) const -> std::unique_ptr<IShaderByteCode>
{
    DCHECK_F(shader_source.size() < std::numeric_limits<uint32_t>::max());

    if (shader_source.empty()) {
        LOG_F(WARNING, "Attempt to compile a shader from source,");
        return {};
    }

    const auto* profile_name = GetProfileForShaderType(shader_info.type);

    ComPtr<IDxcBlobEncoding> src_blob;
    ThrowOnFailed(utils_->CreateBlob(
        shader_source.data(), static_cast<UINT32>(shader_source.size()),
        CP_UTF8, src_blob.GetAddressOf()));

    std::vector<const wchar_t*> arguments;
    arguments.emplace_back(L"-Ges");
    arguments.emplace_back(L"-T");
    arguments.emplace_back(profile_name);
#if !defined(NDEBUG)
    arguments.emplace_back(L"-Od"); // Disable optimizations
    arguments.emplace_back(L"-Zi"); // Enable debug information
    arguments.emplace_back(L"-Qembed_debug"); // Embed PDB in shader container
#else // NDEBUG
    arguments.emplace_back(L"-O3"); // Optimization level 3
#endif // NDEBUG

    std::wstring entry_point {};
    string_utils::Utf8ToWide(shader_info.entry_point, entry_point);
    arguments.emplace_back(L"-E");
    arguments.emplace_back(entry_point.c_str());

    //// Add defines to arguments
    // for (const auto& define : defines)
    //{
    //   std::wstring define_str = L"-D" + std::wstring(define.Name);
    //   if (define.Value)
    //   {
    //     define_str += L"=" + std::wstring(define.Value);
    //   }
    //   arguments.push_back(define_str.c_str());
    // }

    DxcBuffer source_buffer {
        .Ptr = source_buffer.Ptr = src_blob->GetBufferPointer(),
        .Size = source_buffer.Size = src_blob->GetBufferSize(),
        .Encoding = source_buffer.Encoding = DXC_CP_UTF8,
    };

    ComPtr<IDxcResult> result;
    HRESULT hr = compiler_->Compile(
        &source_buffer,
        arguments.data(),
        static_cast<uint32_t>(arguments.size()),
        include_processor_.Get(),
        IID_PPV_ARGS(&result));
    if (FAILED(hr)) {
        LOG_F(ERROR, "DXC Compile call failed: {:x}", hr);
        return {};
    }
    DCHECK_NOTNULL_F(result);

    hr = result->GetStatus(&hr);
    if (FAILED(hr)) {
        ComPtr<IDxcBlobEncoding> error_blob;
        hr = result->GetErrorBuffer(&error_blob);
        if (error_blob != nullptr) {
            LOG_F(ERROR, "Failed to compile shader from source");
            LogCompilationErrors(error_blob.Get());
        }
        return {};
    }
    LOG_F(INFO, "Shader at `{}` compiled successfully", shader_info.relative_path);

    ComPtr<IDxcBlob> output;
    ThrowOnFailed(result->GetResult(&output));

    if (output == nullptr) {
        LOG_F(ERROR, "GetResult returned null blob");
        return {};
    }

    size_t size = output->GetBufferSize();
    if (size == 0) {
        LOG_F(ERROR, "Shader compilation succeeded but produced empty bytecode");

        // Check if we have any warnings
        ComPtr<IDxcBlobWide> output_name_blob;
        ComPtr<IDxcBlob> warning_blob;
        hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&warning_blob), &output_name_blob);
        if (SUCCEEDED(hr) && (warning_blob != nullptr)) {
            LogCompilationErrors(warning_blob.Get());
        }
        return {};
    }

    LOG_F(1, "Shader bytecode size = {}", output->GetBufferSize());

    return std::make_unique<ShaderByteCode<ComPtr<IDxcBlob>>>(std::move(output));
}
