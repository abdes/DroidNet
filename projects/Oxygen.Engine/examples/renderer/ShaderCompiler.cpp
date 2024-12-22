//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ShaderCompiler.h"

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Windows/ComError.h"

using Microsoft::WRL::ComPtr;
using oxygen::renderer::d3d12::ShaderType;
using oxygen::windows::ThrowOnFailed;

namespace {

  bool Utf8ToUtf16(const std::string_view in, std::wstring& out)
  {
    if (in.empty())
    {
      out.clear();
      return true;
    }

    const int num_wide_chars = ::MultiByteToWideChar(CP_UTF8, 0, in.data(), static_cast<int>(in.length()), nullptr, 0);
    if (num_wide_chars <= 0)
    {
      // conversion failed
      return false;
    }

    std::vector<wchar_t> buffer;
    buffer.resize(num_wide_chars + 1);
    auto const ret = ::MultiByteToWideChar(CP_UTF8, 0, in.data(), static_cast<int>(in.length()), buffer.data(), num_wide_chars);
    if (ret <= 0)
    {
      // conversion failed
      return false;
    }

    buffer[num_wide_chars] = static_cast<wchar_t>(0);
    out = std::wstring(buffer.data());
    return true;
  }

  void LogCompilationErrors(IDxcBlobEncoding* error_blob) {
    if (error_blob) {
      // Get the pointer to the error message and its size
      const auto error_message = static_cast<const char*>(error_blob->GetBufferPointer());
      const size_t error_message_size = error_blob->GetBufferSize();

      // Convert the error message to a string
      const std::string error_string(error_message, error_message_size);

      // Display the error message
      LOG_F(ERROR, "Shader compilation error: {}", error_string);
    }
  }
} // namespace

namespace oxygen {

  bool ShaderCompiler::Init()
  {
    ThrowOnFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_)));
    ThrowOnFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_)));
    ThrowOnFailed(utils_->CreateDefaultIncludeHandler(include_processor_.GetAddressOf()));

    return true;
  }

  auto ShaderCompiler::Compile(
    const char* source,
    const uint32_t source_size,
    const char* source_name,
    const ShaderType type,
    const std::vector<DxcDefine>& defines,
    ComPtr<IDxcBlob>& output) const -> bool
  {
    HRESULT hr = S_OK;

    const wchar_t* profile_name;
    switch (type)
    {
    case ShaderType::kVertex:
      profile_name = L"vs_6_8";
      break;
    case ShaderType::kGeometry:
      profile_name = L"gs_6_8";
      break;
    case ShaderType::kHull:
      profile_name = L"hs_6_8";
      break;
    case ShaderType::kDomain:
      profile_name = L"ds_6_8";
      break;
    case ShaderType::kPixel:
      profile_name = L"ps_6_8";
      break;
    case ShaderType::kCompute:
      profile_name = L"cs_6_8";
      break;
    case ShaderType::kMesh:
      profile_name = L"ms_6_8";
      break;
    case ShaderType::kAmplification:
      profile_name = L"as_6_8";
      break;

    case ShaderType::kCount:
      LOG_F(ERROR, "Invalid shader type");
      return false;
    }

    ComPtr<IDxcBlobEncoding> src_blob;
    ThrowOnFailed(utils_->CreateBlob(source, source_size, CP_UTF8, src_blob.GetAddressOf()));

    std::vector<const wchar_t*> arguments;
    arguments.emplace_back(L"-Ges");
    arguments.emplace_back(L"-T");
    arguments.emplace_back(profile_name);
#ifdef _DEBUG
    arguments.emplace_back(L"-Od");
    arguments.emplace_back(L"-Zi");
#else
    arguments.emplace_back(L"-O3");
#endif

    // Add defines to arguments
    for (const auto& define : defines)
    {
      std::wstring define_str = L"-D" + std::wstring(define.Name);
      if (define.Value)
      {
        define_str += L"=" + std::wstring(define.Value);
      }
      arguments.push_back(define_str.c_str());
    }

    std::wstring wide_source_name;
    Utf8ToUtf16(source_name, wide_source_name);

    DxcBuffer source_buffer;
    source_buffer.Ptr = src_blob->GetBufferPointer();
    source_buffer.Size = src_blob->GetBufferSize();
    source_buffer.Encoding = DXC_CP_UTF8;

    ComPtr<IDxcResult> result;
    hr = compiler_->Compile(
      &source_buffer,
      arguments.data(),
      static_cast<uint32_t>(arguments.size()),
      include_processor_.Get(),
      IID_PPV_ARGS(&result));
    if (FAILED(hr))
    {
      LOG_F(ERROR, "DXC Compile call failed: {:x}", hr);
      return false;
    }

    hr = result->GetStatus(&hr);
    if (FAILED(hr))
    {
      ComPtr<IDxcBlobEncoding> error_blob;
      hr = result->GetErrorBuffer(&error_blob);
      if (error_blob)
      {
        LOG_F(ERROR, "Failed to compile shader {}", source_name);
        LogCompilationErrors(error_blob.Get());
      }
      return false;
    }

    ThrowOnFailed(result->GetResult(&output));
    return true;
  }

  auto ShaderCompiler::Disassemble(const ComPtr<IDxcBlob>& bytecode, ComPtr<IDxcBlob>& disassembly) const -> bool
  {
    DxcBuffer bytecode_buffer;
    bytecode_buffer.Ptr = bytecode->GetBufferPointer();
    bytecode_buffer.Size = bytecode->GetBufferSize();
    bytecode_buffer.Encoding = DXC_CP_ACP;

    ComPtr<IDxcResult> result;
    ThrowOnFailed(compiler_->Disassemble(&bytecode_buffer, IID_PPV_ARGS(&result)));

    ThrowOnFailed(result->GetResult(&disassembly));
    return true;
  }

  bool ShaderCompiler::Reflect(const ComPtr<IDxcBlob>& bytecode, ComPtr<ID3D12ShaderReflection>& reflection)
  {
    IDxcBlob* bytecode_ptr = bytecode.Get();
    if (bytecode_ptr == nullptr)
    {
      LOG_F(ERROR, "Shader is not compiled");
      return false;
    }

    ComPtr<IDxcContainerReflection> container_reflection;
    HRESULT hr = DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&container_reflection));
    if (FAILED(hr))
    {
      LOG_F(ERROR, "Failed to create DXC Container Reflection: {:x}", hr);
      return false;
    }

    hr = container_reflection->Load(bytecode_ptr);
    if (FAILED(hr))
    {
      LOG_F(ERROR, "Failed to load shader to DXC Container Reflection: {:x}", hr);
      return false;
    }

    constexpr uint32_t dxil_kind = DXC_PART_DXIL;

    uint32_t shader_idx = 0;
    hr = container_reflection->FindFirstPartKind(dxil_kind, &shader_idx);
    if (FAILED(hr))
    {
      LOG_F(ERROR, "Failed to find DXIL code in blob: {:x}", hr);
      return false;
    }

    hr = container_reflection->GetPartReflection(shader_idx, IID_PPV_ARGS(&reflection));
    if (FAILED(hr))
    {
      LOG_F(ERROR, "Failed to acquire shader reflection: {:x}", hr);
      return false;
    }

    return true;
  }

} // namespace oxygen
