//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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
using oxygen::ShaderType;
using oxygen::graphics::d3d12::ShaderCompiler;
using oxygen::windows::ThrowOnFailed;

namespace {

void LogCompilationErrors(IDxcBlob* error_blob);

struct DxcCompileArgs {
  std::wstring entry_point;
  std::vector<std::wstring> include_dirs;
  std::vector<LPCWSTR> argv;
};

auto MakeDxcArguments(const wchar_t* profile_name,
  const std::string& entry_point_utf8,
  const std::vector<std::filesystem::path>& include_dirs) -> DxcCompileArgs
{
  DxcCompileArgs args {};

  oxygen::string_utils::Utf8ToWide(entry_point_utf8, args.entry_point);

  args.argv.emplace_back(L"-Ges");
  args.argv.emplace_back(L"-T");
  args.argv.emplace_back(profile_name);

  args.include_dirs.reserve(include_dirs.size());
  for (const auto& include_dir : include_dirs) {
    if (include_dir.empty()) {
      continue;
    }
    args.include_dirs.emplace_back(include_dir.wstring());
    args.argv.emplace_back(L"-I");
    args.argv.emplace_back(args.include_dirs.back().c_str());
  }

#if !defined(NDEBUG)
  args.argv.emplace_back(L"-Od"); // Disable optimizations
  args.argv.emplace_back(L"-Zi"); // Enable debug information
  args.argv.emplace_back(L"-Qembed_debug"); // Embed PDB in shader container
#else // NDEBUG
  args.argv.emplace_back(L"-O3"); // Optimization level 3
#endif // NDEBUG

  args.argv.emplace_back(L"-E");
  args.argv.emplace_back(args.entry_point.c_str());

  return args;
}

auto CompileDxc(IDxcCompiler3& compiler, IDxcIncludeHandler& include_handler,
  const DxcBuffer& source_buffer, DxcCompileArgs& args,
  const std::string& shader_identifier)
  -> std::unique_ptr<oxygen::graphics::IShaderByteCode>
{
  ComPtr<IDxcResult> result;
  HRESULT hr = compiler.Compile(&source_buffer, args.argv.data(),
    static_cast<uint32_t>(args.argv.size()), &include_handler,
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
      LOG_F(ERROR, "Failed to compile shader {}", shader_identifier);
      LogCompilationErrors(error_blob.Get());
    }
    return {};
  }

  ComPtr<IDxcBlob> output;
  ThrowOnFailed(result->GetResult(&output));
  if (output == nullptr) {
    LOG_F(ERROR, "GetResult returned null blob");
    return {};
  }

  const size_t size = output->GetBufferSize();
  if (size == 0) {
    LOG_F(ERROR, "Shader compilation succeeded but produced empty bytecode");

    ComPtr<IDxcBlobWide> output_name_blob;
    ComPtr<IDxcBlob> warning_blob;
    hr = result->GetOutput(
      DXC_OUT_ERRORS, IID_PPV_ARGS(&warning_blob), &output_name_blob);
    if (SUCCEEDED(hr) && (warning_blob != nullptr)) {
      LogCompilationErrors(warning_blob.Get());
    }
    return {};
  }

  LOG_F(1, "Shader bytecode size = {}", output->GetBufferSize());
  return std::make_unique<oxygen::graphics::ShaderByteCode<ComPtr<IDxcBlob>>>(
    std::move(output));
}

void LogCompilationErrors(IDxcBlob* error_blob)
{
  if (error_blob != nullptr) {
    // Get the pointer to the error message and its size
    const auto* const error_message
      = static_cast<const char*>(error_blob->GetBufferPointer());
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
    return L"vs_6_6";
  case ShaderType::kGeometry:
    return L"gs_6_6";
  case ShaderType::kHull:
    return L"hs_6_6";
  case ShaderType::kDomain:
    return L"ds_6_6";
  case ShaderType::kPixel:
    return L"ps_6_6";
  case ShaderType::kCompute:
    return L"cs_6_6";
  case ShaderType::kMesh:
    return L"ms_6_6";
  case ShaderType::kAmplification:
    return L"as_6_6";
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
  ThrowOnFailed(
    utils_->CreateDefaultIncludeHandler(include_processor_.GetAddressOf()));
}

ShaderCompiler::~ShaderCompiler() = default;

auto ShaderCompiler::CompileFromSource(const std::u8string& shader_source,
  const ShaderInfo& shader_info, const ShaderCompileOptions& options) const
  -> std::unique_ptr<IShaderByteCode>
{
  DCHECK_F(shader_source.size() < (std::numeric_limits<uint32_t>::max)());

  if (shader_source.empty()) {
    LOG_F(WARNING, "Attempt to compile a shader from source,");
    return {};
  }

  const auto* profile_name = GetProfileForShaderType(shader_info.type);

  ComPtr<IDxcBlobEncoding> src_blob;
  ThrowOnFailed(utils_->CreateBlob(shader_source.data(),
    static_cast<UINT32>(shader_source.size()), CP_UTF8,
    src_blob.GetAddressOf()));

  auto args = MakeDxcArguments(
    profile_name, shader_info.entry_point, options.include_dirs);

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
    .Ptr = src_blob->GetBufferPointer(),
    .Size = src_blob->GetBufferSize(),
    .Encoding = DXC_CP_UTF8,
  };

  auto* const compiler = compiler_.Get();
  auto* const include_handler = include_processor_.Get();
  DCHECK_NOTNULL_F(compiler);
  DCHECK_NOTNULL_F(include_handler);

  auto bytecode = CompileDxc(*compiler, *include_handler, source_buffer, args,
    shader_info.relative_path);
  if (!bytecode) {
    return {};
  }

  LOG_F(
    INFO, "Shader at `{}` compiled successfully", shader_info.relative_path);
  return bytecode;
}
