//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/DxcShaderCompiler.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include <unknwn.h>

#include <d3dcommon.h>
#include <dxcapi.h>
#include <fmt/format.h>
#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>

using Microsoft::WRL::ComPtr;
using oxygen::ShaderType;
using oxygen::graphics::d3d12::tools::shader_bake::DxcShaderCompiler;

namespace {

void LogDxcDiagnostics(IDxcBlob* diagnostics_blob);

struct DxcCompileArgs {
  std::vector<std::wstring> argv_storage;
};

auto WideToUtf8String(std::wstring_view in) -> std::string
{
  std::string out;
  oxygen::string_utils::WideToUtf8(in, out);
  return out;
}

auto JoinDxcArgsForLog(const DxcCompileArgs& args) -> std::string
{
  std::string joined;
  for (const auto& a : args.argv_storage) {
    if (!joined.empty()) {
      joined.push_back(' ');
    }
    joined += WideToUtf8String(a);
  }
  return joined;
}

auto ContainsEntryPointToken(std::string_view source, std::string_view entry)
  -> bool
{
  if (entry.empty()) {
    return false;
  }
  if (source.find(entry) == std::string_view::npos) {
    return false;
  }
  std::string needle(entry);
  needle += "(";
  return source.find(needle) != std::string_view::npos;
}

struct DxcFailureContext {
  const std::string* shader_identifier {};
  const wchar_t* profile_name {};
  std::string_view entry_point_utf8;
  std::span<const std::filesystem::path> include_dirs;
  std::string_view args_for_log;
  std::string_view shader_source_utf8;
};

auto UniqueIncludeDirsForLog(std::span<const std::filesystem::path> dirs)
  -> std::vector<std::string>
{
  std::vector<std::string> out;
  out.reserve(dirs.size());

  std::string prev;
  for (const auto& dir : dirs) {
    if (dir.empty()) {
      continue;
    }
    auto current = WideToUtf8String(dir.wstring());
    if (!out.empty() && (current == prev)) {
      continue;
    }
    prev = current;
    out.emplace_back(std::move(current));
  }

  return out;
}

void LogDxcFailureReport(const DxcFailureContext& ctx, std::string_view reason,
  IDxcBlob* diagnostics_blob)
{
  LOG_SCOPE_F(ERROR, "DXC shader compilation failed");
  LOG_F(ERROR, "reason: {}", std::string(reason));

  if (ctx.shader_identifier != nullptr) {
    LOG_F(ERROR, "shader: {}", *ctx.shader_identifier);
  }
  if (ctx.profile_name != nullptr) {
    LOG_F(ERROR, "profile: {}", WideToUtf8String(ctx.profile_name));
  }
  LOG_F(ERROR, "entry point: {}", std::string(ctx.entry_point_utf8));
  LOG_F(ERROR, "args: {}", std::string(ctx.args_for_log));

  const auto include_dirs_for_log = UniqueIncludeDirsForLog(ctx.include_dirs);
  {
    LOG_SCOPE_F(ERROR,
      fmt::format("include dirs ({}):", include_dirs_for_log.size()).c_str());
    for (const auto& dir : include_dirs_for_log) {
      LOG_F(ERROR, "  - {}", dir);
    }
  }

  LOG_F(ERROR, "source contains entry token: {}",
    ContainsEntryPointToken(ctx.shader_source_utf8, ctx.entry_point_utf8));

  LogDxcDiagnostics(diagnostics_blob);
}

auto MakeDxcArguments(const wchar_t* profile_name,
  const std::string& entry_point_utf8,
  std::span<const std::filesystem::path> include_dirs,
  const std::map<std::wstring, std::wstring>& global_defines,
  std::span<const oxygen::graphics::ShaderDefine> request_defines)
  -> DxcCompileArgs
{
  DxcCompileArgs args {};

  std::wstring entry_point;
  oxygen::string_utils::Utf8ToWide(entry_point_utf8, entry_point);

  args.argv_storage.reserve(16U + (include_dirs.size() * 2U)
    + global_defines.size() + request_defines.size());

  args.argv_storage.emplace_back(L"-Ges");
  args.argv_storage.emplace_back(L"-enable-16bit-types");
  args.argv_storage.emplace_back(L"-HV");
  args.argv_storage.emplace_back(L"2021");
  args.argv_storage.emplace_back(L"-T");
  args.argv_storage.emplace_back(profile_name);
  for (const auto& include_dir : include_dirs) {
    if (include_dir.empty()) {
      continue;
    }
    args.argv_storage.emplace_back(L"-I");
    args.argv_storage.emplace_back(include_dir.wstring());
  }

  for (const auto& [name, value] : global_defines) {
    if (name.empty()) {
      continue;
    }

    std::wstring def_arg = L"-D";
    def_arg += name;
    if (!value.empty()) {
      def_arg += L"=";
      def_arg += value;
    }
    args.argv_storage.emplace_back(std::move(def_arg));
  }

  for (const auto& def : request_defines) {
    if (def.name.empty()) {
      continue;
    }

    std::wstring name_w;
    oxygen::string_utils::Utf8ToWide(def.name, name_w);

    std::wstring def_arg = L"-D";
    def_arg += name_w;

    if (def.value.has_value()) {
      std::wstring value_w;
      oxygen::string_utils::Utf8ToWide(*def.value, value_w);
      def_arg += L"=";
      def_arg += value_w;
    }

    args.argv_storage.emplace_back(std::move(def_arg));
  }

#if !defined(NDEBUG)
  args.argv_storage.emplace_back(L"-Od");
  args.argv_storage.emplace_back(L"-Zi");
  args.argv_storage.emplace_back(L"-Qembed_debug");
#else
  args.argv_storage.emplace_back(L"-O3");
#endif

  args.argv_storage.emplace_back(L"-E");
  args.argv_storage.emplace_back(std::move(entry_point));

  return args;
}

auto CompileDxc(IDxcCompiler3& compiler, IDxcIncludeHandler& include_handler,
  const DxcBuffer& source_buffer, DxcCompileArgs& args,
  const std::string& shader_identifier, const wchar_t* profile_name,
  std::string_view entry_point_utf8, std::string_view shader_source_utf8,
  std::span<const std::filesystem::path> include_dirs)
  -> std::unique_ptr<oxygen::graphics::IShaderByteCode>
{
  using oxygen::windows::ThrowOnFailed;

  const auto args_for_log = JoinDxcArgsForLog(args);
  const DxcFailureContext ctx {
    .shader_identifier = &shader_identifier,
    .profile_name = profile_name,
    .entry_point_utf8 = entry_point_utf8,
    .include_dirs = include_dirs,
    .args_for_log = args_for_log,
    .shader_source_utf8 = shader_source_utf8,
  };

  ComPtr<IDxcResult> result;
  std::vector<LPCWSTR> argv;
  argv.reserve(args.argv_storage.size());
  for (const auto& a : args.argv_storage) {
    argv.emplace_back(a.c_str());
  }

  HRESULT hr = compiler.Compile(&source_buffer, argv.data(),
    static_cast<uint32_t>(argv.size()), &include_handler,
    IID_PPV_ARGS(&result));
  if (FAILED(hr)) {
    LOG_F(ERROR, "DXC Compile call failed: {:x}", hr);
    LogDxcFailureReport(ctx, "Compile call failed", nullptr);
    return {};
  }
  DCHECK_NOTNULL_F(result);

  HRESULT status = S_OK;
  ThrowOnFailed(result->GetStatus(&status));
  if (FAILED(status)) {
    ComPtr<IDxcBlobEncoding> error_blob;
    hr = result->GetErrorBuffer(&error_blob);
    LogDxcFailureReport(ctx, "Compilation failed", error_blob.Get());
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
    ComPtr<IDxcBlobWide> output_name_blob;
    ComPtr<IDxcBlob> warning_blob;
    hr = result->GetOutput(
      DXC_OUT_ERRORS, IID_PPV_ARGS(&warning_blob), &output_name_blob);

    auto* diagnostics_blob = (SUCCEEDED(hr) ? warning_blob.Get() : nullptr);
    LogDxcFailureReport(ctx, "Empty bytecode", diagnostics_blob);
    return {};
  }

  return std::make_unique<oxygen::graphics::ShaderByteCode<ComPtr<IDxcBlob>>>(
    std::move(output));
}

void LogDxcDiagnostics(IDxcBlob* diagnostics_blob)
{
  if (diagnostics_blob == nullptr) {
    return;
  }

  LOG_SCOPE_F(ERROR, "DXC Diagnostics Report");

  const auto* const message
    = static_cast<const char*>(diagnostics_blob->GetBufferPointer());
  const size_t message_size = diagnostics_blob->GetBufferSize();
  if ((message == nullptr) || (message_size == 0U)) {
    return;
  }

  std::string_view diagnostics(message, message_size);
  while (!diagnostics.empty()) {
    const auto newline_pos = diagnostics.find('\n');
    std::string_view line = diagnostics.substr(0, newline_pos);
    if (!line.empty() && (line.back() == '\r')) {
      line.remove_suffix(1);
    }
    if (!line.empty()) {
      LOG_F(ERROR, "{}", std::string(line));
    }

    if (newline_pos == std::string_view::npos) {
      break;
    }
    diagnostics.remove_prefix(newline_pos + 1);
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
  throw std::invalid_argument("Invalid shader type");
}

} // namespace

DxcShaderCompiler::DxcShaderCompiler(Config config)
  : config_(std::move(config))
{
  using oxygen::windows::ThrowOnFailed;

  ThrowOnFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_)));
  ThrowOnFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_)));
  ThrowOnFailed(
    utils_->CreateDefaultIncludeHandler(include_processor_.GetAddressOf()));
}

DxcShaderCompiler::~DxcShaderCompiler() = default;

auto DxcShaderCompiler::CompileFromSource(const std::u8string& shader_source,
  const ShaderInfo& shader_info, const CompileOptions& options) const
  -> std::unique_ptr<IShaderByteCode>
{
  using oxygen::windows::ThrowOnFailed;

  if (shader_source.empty()) {
    LOG_F(WARNING, "Attempt to compile a shader from empty source");
    return {};
  }

  DCHECK_F(shader_source.size() < (std::numeric_limits<uint32_t>::max)());

  const auto* profile_name = GetProfileForShaderType(shader_info.type);

  ComPtr<IDxcBlobEncoding> src_blob;
  ThrowOnFailed(utils_->CreateBlob(shader_source.data(),
    static_cast<UINT32>(shader_source.size()), CP_UTF8,
    src_blob.GetAddressOf()));

  auto args = MakeDxcArguments(profile_name, shader_info.entry_point,
    options.include_dirs, config_.global_defines, options.defines);

  DxcBuffer source_buffer {
    .Ptr = src_blob->GetBufferPointer(),
    .Size = src_blob->GetBufferSize(),
    .Encoding = DXC_CP_UTF8,
  };

  auto* const compiler = compiler_.Get();
  auto* const include_handler = include_processor_.Get();
  DCHECK_NOTNULL_F(compiler);
  DCHECK_NOTNULL_F(include_handler);

  const std::string shader_identifier
    = oxygen::graphics::FormatShaderLogKey(shader_info);

  return CompileDxc(*compiler, *include_handler, source_buffer, args,
    shader_identifier, profile_name, shader_info.entry_point,
    std::string_view(reinterpret_cast<const char*>(shader_source.data()),
      shader_source.size()),
    options.include_dirs);
}
