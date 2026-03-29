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
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/TrackingIncludeHandler.h>

using Microsoft::WRL::ComPtr;
using oxygen::ShaderType;
using oxygen::graphics::d3d12::tools::shader_bake::DxcShaderCompiler;

namespace {

struct DxcCompileArgs {
  std::vector<std::wstring> option_storage;
  std::vector<std::wstring> define_name_storage;
  std::vector<std::wstring> define_value_storage;
  std::vector<DxcDefine> defines;
  ComPtr<IDxcCompilerArgs> compiler_args;
};

auto WideToUtf8String(std::wstring_view in) -> std::string
{
  std::string out;
  oxygen::string_utils::WideToUtf8(in, out);
  return out;
}

auto JoinDxcArgsForLog(IDxcCompilerArgs& args) -> std::string
{
  std::string joined;
  const auto argv = args.GetArguments();
  const auto argc = args.GetCount();
  for (uint32_t index = 0; index < argc; ++index) {
    if (!joined.empty()) {
      joined.push_back(' ');
    }
    joined += WideToUtf8String(argv[index]);
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

auto LogDxcFailureReport(const DxcFailureContext& ctx, std::string_view reason,
  IDxcBlob* diagnostics_blob) -> std::string;

auto MakeDxcArguments(IDxcUtils& utils, const std::wstring& source_name_w,
  const wchar_t* profile_name, const std::string& entry_point_utf8,
  std::span<const std::filesystem::path> include_dirs,
  const std::map<std::wstring, std::wstring>& global_defines,
  std::span<const oxygen::graphics::ShaderDefine> request_defines)
  -> DxcCompileArgs
{
  using oxygen::windows::ThrowOnFailed;

  DxcCompileArgs args {};

  std::wstring entry_point;
  oxygen::string_utils::Utf8ToWide(entry_point_utf8, entry_point);

  args.option_storage.reserve(12U + (include_dirs.size() * 2U));

  args.option_storage.emplace_back(L"-Ges");
  args.option_storage.emplace_back(L"-enable-16bit-types");
  args.option_storage.emplace_back(L"-HV");
  args.option_storage.emplace_back(L"2021");
  for (const auto& include_dir : include_dirs) {
    if (include_dir.empty()) {
      continue;
    }
    args.option_storage.emplace_back(L"-I");
    args.option_storage.emplace_back(include_dir.wstring());
  }

  args.define_name_storage.reserve(
    global_defines.size() + request_defines.size());
  args.define_value_storage.reserve(request_defines.size());
  args.defines.reserve(global_defines.size() + request_defines.size());

  for (const auto& [name, value] : global_defines) {
    if (name.empty()) {
      continue;
    }
    args.define_name_storage.push_back(name);
    if (!value.empty()) {
      args.define_value_storage.push_back(value);
      args.defines.push_back(DxcDefine {
        .Name = args.define_name_storage.back().c_str(),
        .Value = args.define_value_storage.back().c_str(),
      });
    } else {
      args.defines.push_back(DxcDefine {
        .Name = args.define_name_storage.back().c_str(),
        .Value = nullptr,
      });
    }
  }

  for (const auto& def : request_defines) {
    if (def.name.empty()) {
      continue;
    }

    std::wstring name_w;
    oxygen::string_utils::Utf8ToWide(def.name, name_w);
    args.define_name_storage.push_back(std::move(name_w));
    if (def.value.has_value()) {
      std::wstring value_w;
      oxygen::string_utils::Utf8ToWide(*def.value, value_w);
      args.define_value_storage.push_back(std::move(value_w));
      args.defines.push_back(DxcDefine {
        .Name = args.define_name_storage.back().c_str(),
        .Value = args.define_value_storage.back().c_str(),
      });
    } else {
      args.defines.push_back(DxcDefine {
        .Name = args.define_name_storage.back().c_str(),
        .Value = nullptr,
      });
    }
  }

#if !defined(NDEBUG)
  args.option_storage.emplace_back(L"-Od");
  args.option_storage.emplace_back(L"-Zi");
  args.option_storage.emplace_back(L"-Qembed_debug");
#else
  args.option_storage.emplace_back(L"-O3");
#endif

  std::vector<LPCWSTR> option_ptrs;
  option_ptrs.reserve(args.option_storage.size());
  for (const auto& option : args.option_storage) {
    option_ptrs.push_back(option.c_str());
  }

  ThrowOnFailed(utils.BuildArguments(source_name_w.c_str(), entry_point.c_str(),
    profile_name, option_ptrs.data(), static_cast<uint32_t>(option_ptrs.size()),
    args.defines.data(), static_cast<uint32_t>(args.defines.size()),
    args.compiler_args.GetAddressOf()));

  return args;
}

auto CompileDxc(IDxcCompiler3& compiler, IDxcIncludeHandler& include_handler,
  const DxcBuffer& source_buffer, DxcCompileArgs& args,
  const std::string& shader_identifier, const wchar_t* profile_name,
  std::string_view entry_point_utf8, std::string_view shader_source_utf8,
  std::span<const std::filesystem::path> include_dirs)
  -> DxcShaderCompiler::CompileResult
{
  using oxygen::windows::ThrowOnFailed;

  auto* const compiler_args = args.compiler_args.Get();
  DCHECK_NOTNULL_F(compiler_args);
  const auto args_for_log = JoinDxcArgsForLog(*compiler_args);
  const DxcFailureContext ctx {
    .shader_identifier = &shader_identifier,
    .profile_name = profile_name,
    .entry_point_utf8 = entry_point_utf8,
    .include_dirs = include_dirs,
    .args_for_log = args_for_log,
    .shader_source_utf8 = shader_source_utf8,
  };

  ComPtr<IDxcResult> result;
  HRESULT hr = compiler.Compile(&source_buffer, compiler_args->GetArguments(),
    compiler_args->GetCount(), &include_handler, IID_PPV_ARGS(&result));
  if (FAILED(hr)) {
    LOG_F(ERROR, "DXC Compile call failed: {:x}", hr);
    return DxcShaderCompiler::CompileResult {
      .diagnostics = LogDxcFailureReport(ctx, "Compile call failed", nullptr),
    };
  }
  DCHECK_NOTNULL_F(result);

  HRESULT status = S_OK;
  ThrowOnFailed(result->GetStatus(&status));
  if (FAILED(status)) {
    ComPtr<IDxcBlobEncoding> error_blob;
    hr = result->GetErrorBuffer(&error_blob);
    return DxcShaderCompiler::CompileResult {
      .diagnostics
      = LogDxcFailureReport(ctx, "Compilation failed", error_blob.Get()),
    };
  }

  ComPtr<IDxcBlob> output;
  ThrowOnFailed(result->GetResult(&output));
  if (output == nullptr) {
    LOG_F(ERROR, "GetResult returned null blob");
    return DxcShaderCompiler::CompileResult {
      .diagnostics
      = LogDxcFailureReport(ctx, "GetResult returned null blob", nullptr),
    };
  }

  const size_t size = output->GetBufferSize();
  if (size == 0) {
    ComPtr<IDxcBlobWide> output_name_blob;
    ComPtr<IDxcBlob> warning_blob;
    hr = result->GetOutput(
      DXC_OUT_ERRORS, IID_PPV_ARGS(&warning_blob), &output_name_blob);

    auto* diagnostics_blob = (SUCCEEDED(hr) ? warning_blob.Get() : nullptr);
    return DxcShaderCompiler::CompileResult {
      .diagnostics
      = LogDxcFailureReport(ctx, "Empty bytecode", diagnostics_blob),
    };
  }

  auto& tracking_include_handler = static_cast<
    oxygen::graphics::d3d12::tools::shader_bake::TrackingIncludeHandler&>(
    include_handler);

  return DxcShaderCompiler::CompileResult {
    .bytecode
    = std::make_unique<oxygen::graphics::ShaderByteCode<ComPtr<IDxcBlob>>>(
      std::move(output)),
    .dependencies = tracking_include_handler.Dependencies(),
  };
}

auto DiagnosticsBlobToString(IDxcBlob* diagnostics_blob) -> std::string
{
  if (diagnostics_blob == nullptr) {
    return {};
  }

  const auto* const message
    = static_cast<const char*>(diagnostics_blob->GetBufferPointer());
  const size_t message_size = diagnostics_blob->GetBufferSize();
  if ((message == nullptr) || (message_size == 0U)) {
    return {};
  }

  return std::string(message, message_size);
}

void LogMultilineDiagnostics(std::string_view diagnostics)
{
  std::string_view remaining = diagnostics;
  while (!remaining.empty()) {
    const auto newline_pos = remaining.find('\n');
    std::string_view line = remaining.substr(0, newline_pos);
    if (!line.empty() && (line.back() == '\r')) {
      line.remove_suffix(1);
    }
    if (!line.empty()) {
      LOG_F(ERROR, "{}", std::string(line));
    }

    if (newline_pos == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(newline_pos + 1);
  }
}

auto LogDxcFailureReport(const DxcFailureContext& ctx, std::string_view reason,
  IDxcBlob* diagnostics_blob) -> std::string
{
  auto report = fmt::format("reason: {}\n", reason);

  if (ctx.shader_identifier != nullptr) {
    report += fmt::format("shader: {}\n", *ctx.shader_identifier);
  }
  if (ctx.profile_name != nullptr) {
    report += fmt::format("profile: {}\n", WideToUtf8String(ctx.profile_name));
  }
  report += fmt::format("entry point: {}\n", ctx.entry_point_utf8);
  report += fmt::format("args: {}\n", ctx.args_for_log);

  const auto include_dirs_for_log = UniqueIncludeDirsForLog(ctx.include_dirs);
  report += fmt::format("include dirs ({}):\n", include_dirs_for_log.size());
  for (const auto& dir : include_dirs_for_log) {
    report += fmt::format("  - {}\n", dir);
  }

  report += fmt::format("source contains entry token: {}\n",
    ContainsEntryPointToken(ctx.shader_source_utf8, ctx.entry_point_utf8));

  const auto diagnostics = DiagnosticsBlobToString(diagnostics_blob);
  if (!diagnostics.empty()) {
    report += "DXC diagnostics:\n";
    report += diagnostics;
    if (report.back() != '\n') {
      report.push_back('\n');
    }
  }

  LOG_SCOPE_F(ERROR, "DXC shader compilation failed");
  LogMultilineDiagnostics(report);
  return report;
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
}

DxcShaderCompiler::~DxcShaderCompiler() = default;

auto DxcShaderCompiler::CompileFromSource(const std::u8string& shader_source,
  const ShaderInfo& shader_info, const CompileOptions& options) const
  -> CompileResult
{
  using oxygen::windows::ThrowOnFailed;

  if (shader_source.empty()) {
    LOG_F(WARNING, "Attempt to compile a shader from empty source");
    return CompileResult {
      .diagnostics = "Attempt to compile a shader from empty source\n",
    };
  }

  DCHECK_F(shader_source.size() < (std::numeric_limits<uint32_t>::max)());

  const auto* profile_name = GetProfileForShaderType(shader_info.type);
  std::wstring source_name_w;
  oxygen::string_utils::Utf8ToWide(shader_info.relative_path, source_name_w);

  ComPtr<IDxcBlobEncoding> src_blob;
  ThrowOnFailed(utils_->CreateBlob(shader_source.data(),
    static_cast<UINT32>(shader_source.size()), CP_UTF8,
    src_blob.GetAddressOf()));

  auto args = MakeDxcArguments(*utils_.Get(), source_name_w, profile_name,
    shader_info.entry_point, options.include_dirs, config_.global_defines,
    options.defines);

  DxcBuffer source_buffer {
    .Ptr = src_blob->GetBufferPointer(),
    .Size = src_blob->GetBufferSize(),
    .Encoding = DXC_CP_UTF8,
  };

  auto* const compiler = compiler_.Get();
  auto* const utils = utils_.Get();
  DCHECK_NOTNULL_F(compiler);
  DCHECK_NOTNULL_F(utils);

  const std::string shader_identifier
    = oxygen::graphics::FormatShaderLogKey(shader_info);
  ComPtr<IDxcIncludeHandler> include_handler;
  include_handler.Attach(new TrackingIncludeHandler(
    *utils, options.workspace_root, options.include_dirs));
  return CompileDxc(*compiler, *include_handler.Get(), source_buffer, args,
    shader_identifier, profile_name, shader_info.entry_point,
    std::string_view(reinterpret_cast<const char*>(shader_source.data()),
      shader_source.size()),
    options.include_dirs);
}
