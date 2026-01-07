//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Graphics/Common/ShaderLibraryIO.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Bake.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/DxcShaderCompiler.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Reflect.h>

using oxygen::graphics::ShaderInfo;
using oxygen::graphics::d3d12::kEngineShaders;
using oxygen::graphics::d3d12::ShaderEntry;
using oxygen::graphics::d3d12::ToShaderInfo;
using oxygen::graphics::d3d12::tools::shader_bake::DxcShaderCompiler;

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  constexpr std::array<char, 8> kBackendString = {
    'd',
    '3',
    'd',
    '1',
    '2',
    '\0',
    '\0',
    '\0',
  };

  auto WideToUtf8String(std::wstring_view in) -> std::string
  {
    std::string out;
    oxygen::string_utils::WideToUtf8(in, out);
    return out;
  }

  auto GetLoadedModulePath(const wchar_t* module_name)
    -> std::optional<std::filesystem::path>
  {
    HMODULE module = GetModuleHandleW(module_name);
    if (module == nullptr) {
      return std::nullopt;
    }

    std::wstring buffer;
    buffer.resize(MAX_PATH);
    DWORD length = GetModuleFileNameW(
      module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return std::nullopt;
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
  }

  auto GetFileVersionString(const std::filesystem::path& file)
    -> std::optional<std::string>
  {
    std::wstring file_w = file.wstring();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(file_w.c_str(), &handle);
    if (size == 0) {
      return std::nullopt;
    }

    std::vector<std::byte> data;
    data.resize(size);
    if (!GetFileVersionInfoW(file_w.c_str(), handle, size, data.data())) {
      return std::nullopt;
    }

    VS_FIXEDFILEINFO* file_info = nullptr;
    UINT file_info_len = 0;
    if (!VerQueryValueW(data.data(), L"\\",
          reinterpret_cast<void**>(&file_info), &file_info_len)
      || file_info == nullptr || file_info_len < sizeof(VS_FIXEDFILEINFO)) {
      return std::nullopt;
    }

    const uint16_t major = HIWORD(file_info->dwFileVersionMS);
    const uint16_t minor = LOWORD(file_info->dwFileVersionMS);
    const uint16_t patch = HIWORD(file_info->dwFileVersionLS);
    const uint16_t build = LOWORD(file_info->dwFileVersionLS);
    return std::to_string(major) + "." + std::to_string(minor) + "."
      + std::to_string(patch) + "." + std::to_string(build);
  }

  auto ComputeToolchainHash() -> uint64_t
  {
    std::string version = "unknown";
    if (const auto dxcompiler_path = GetLoadedModulePath(L"dxcompiler.dll")) {
      if (const auto ver = GetFileVersionString(*dxcompiler_path)) {
        version = *ver;
      }
    }

#if !defined(NDEBUG)
    constexpr std::string_view build_mode = "Debug";
#else
    constexpr std::string_view build_mode = "Release";
#endif

    const std::string schema = std::string("dxc;") + "version=" + version + ";"
      + "-Ges;" + "-enable-16bit-types;" + "-HV=2021;" + "sm=6_6;"
      + "mode=" + std::string(build_mode) + ";";

    return oxygen::ComputeFNV1a64(schema.data(), schema.size());
  }

  auto ReadFileUtf8(const std::filesystem::path& file) -> std::u8string
  {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
      throw std::runtime_error("failed to open shader source file");
    }

    std::vector<char> bytes(
      (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return std::u8string(reinterpret_cast<const char8_t*>(bytes.data()),
      reinterpret_cast<const char8_t*>(bytes.data()) + bytes.size());
  }

  struct ModuleRecord {
    ShaderInfo info;
    std::vector<std::byte> dxil;
    std::vector<std::byte> reflection;
  };

  auto BuildIncludeDirs(const BakeArgs& args)
    -> std::vector<std::filesystem::path>
  {
    std::vector<std::filesystem::path> include_dirs;
    include_dirs.reserve(2 + args.extra_include_dirs.size());
    include_dirs.push_back(args.oxygen_include_root);
    include_dirs.push_back(args.shader_source_root);
    for (const auto& include_dir : args.extra_include_dirs) {
      include_dirs.push_back(include_dir);
    }
    return include_dirs;
  }

  auto BakeEngineShaders(const BakeArgs& args, DxcShaderCompiler& compiler,
    const DxcShaderCompiler::CompileOptions& base_options)
    -> std::optional<std::vector<ModuleRecord>>
  {
    std::vector<ModuleRecord> modules;
    modules.reserve(kEngineShaders.size());

    size_t index = 0;
    for (const auto& entry : kEngineShaders) {
      ++index;

      // Convert constexpr ShaderEntry to runtime ShaderInfo
      const auto shader = ToShaderInfo(entry);

      // Build per-shader compile options with the shader's defines.
      DxcShaderCompiler::CompileOptions shader_options = base_options;
      shader_options.defines = shader.defines;

      std::string defines_str;
      for (const auto& def : shader.defines) {
        if (!defines_str.empty()) {
          defines_str += ",";
        }
        defines_str += def.name;
        if (def.value) {
          defines_str += "=" + *def.value;
        }
      }
      LOG_F(INFO, "[{}/{}] {}:{}{}{}", index, kEngineShaders.size(),
        shader.relative_path, shader.entry_point,
        defines_str.empty() ? "" : " [",
        defines_str.empty() ? "" : defines_str + "]");

      const auto shader_file = args.shader_source_root / shader.relative_path;
      const auto source = ReadFileUtf8(shader_file);

      auto bytecode
        = compiler.CompileFromSource(source, shader, shader_options);
      if (!bytecode) {
        LOG_F(ERROR, "Failed to compile {}:{}", shader.relative_path,
          shader.entry_point);
        return std::nullopt;
      }

      const auto* dxil_words = bytecode->Data();
      const size_t dxil_size_bytes = bytecode->Size();
      if ((dxil_size_bytes % sizeof(uint32_t)) != 0U) {
        throw std::runtime_error("DXIL bytecode size is not 4-byte aligned");
      }

      const auto dxil_word_count = dxil_size_bytes / sizeof(uint32_t);
      const auto dxil_as_bytes
        = std::as_bytes(std::span(dxil_words, dxil_word_count));
      std::vector<std::byte> dxil(dxil_as_bytes.begin(), dxil_as_bytes.end());

      auto reflection = ExtractAndSerializeReflection(
        shader, std::span<const std::byte>(dxil));

      LOG_F(INFO, "  dxil={} bytes, reflection={} bytes", dxil.size(),
        reflection.size());

      modules.push_back(ModuleRecord {
        .info = shader,
        .dxil = std::move(dxil),
        .reflection = std::move(reflection),
      });
    }

    return modules;
  }

  auto WriteLibrary(
    const BakeArgs& args, const std::vector<ModuleRecord>& modules) -> void
  {
    const uint64_t toolchain_hash = ComputeToolchainHash();

    std::vector<oxygen::graphics::ShaderLibraryWriter::ModuleView> views;
    views.reserve(modules.size());
    for (const auto& m : modules) {
      views.push_back(oxygen::graphics::ShaderLibraryWriter::ModuleView {
        .stage = m.info.type,
        .source_path = m.info.relative_path,
        .entry_point = m.info.entry_point,
        .defines = m.info.defines,
        .dxil = std::span<const std::byte>(m.dxil),
        .reflection = std::span<const std::byte>(m.reflection),
      });
    }

    const oxygen::graphics::ShaderLibraryWriter writer(
      kBackendString, toolchain_hash);
    writer.WriteToFile(args.out_file, views);

    LOG_F(INFO, "Wrote {} modules to {}", modules.size(),
      WideToUtf8String(args.out_file.wstring()));
  }

} // namespace

auto BakeShaderLibrary(const BakeArgs& args) -> int
{
  LOG_SCOPE_F(INFO, "ShaderBake");

  DxcShaderCompiler compiler(DxcShaderCompiler::Config { .name = "DXC" });
  DxcShaderCompiler::CompileOptions options {
    .include_dirs = BuildIncludeDirs(args),
    .defines = {},
  };

  const auto modules_opt = BakeEngineShaders(args, compiler, options);
  if (!modules_opt.has_value()) {
    return 2;
  }

  WriteLibrary(args, *modules_opt);
  return 0;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
