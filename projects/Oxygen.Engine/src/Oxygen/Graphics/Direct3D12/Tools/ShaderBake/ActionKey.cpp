//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <windows.h> // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <libloaderapi.h>
#include <minwindef.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCatalogBuilder.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ActionKey.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto GetLoadedModulePath(const wchar_t* module_name)
    -> std::optional<std::filesystem::path>
  {
    const HMODULE module = GetModuleHandleW(module_name);
    if (module == nullptr) {
      return std::nullopt;
    }

    std::wstring buffer;
    buffer.resize(MAX_PATH);
    const DWORD length = GetModuleFileNameW(
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

    std::vector<std::byte> data(size);
    if (GetFileVersionInfoW(file_w.c_str(), handle, size, data.data()) == 0) {
      return std::nullopt;
    }

    void* file_info_data = nullptr;
    UINT file_info_len = 0;
    if (VerQueryValueW(data.data(), L"\\", &file_info_data, &file_info_len) == 0
      || file_info_data == nullptr
      || file_info_len < sizeof(VS_FIXEDFILEINFO)) {
      return std::nullopt;
    }
    const auto* const file_info
      = static_cast<const VS_FIXEDFILEINFO*>(file_info_data);

    const uint16_t major = HIWORD(file_info->dwFileVersionMS);
    const uint16_t minor = LOWORD(file_info->dwFileVersionMS);
    const uint16_t patch = HIWORD(file_info->dwFileVersionLS);
    const uint16_t build = LOWORD(file_info->dwFileVersionLS);
    return std::to_string(major) + "." + std::to_string(minor) + "."
      + std::to_string(patch) + "." + std::to_string(build);
  }

  auto GetDxcVersionString() -> std::string
  {
    if (const auto dxcompiler_path = GetLoadedModulePath(L"dxcompiler.dll")) {
      if (const auto version = GetFileVersionString(*dxcompiler_path)) {
        return *version;
      }
    }

    return "unknown";
  }

  auto NormalizeIncludeRoot(const std::filesystem::path& include_dir)
    -> std::string
  {
    return include_dir.lexically_normal().generic_string();
  }

  auto HashCombine(const uint64_t seed, const uint64_t value) -> uint64_t
  {
    constexpr uint64_t kGoldenRatio = 0x9e3779b97f4a7c15ULL;
    constexpr unsigned kShiftLeft = 6U;
    constexpr unsigned kShiftRight = 2U;
    return seed
      ^ (value + kGoldenRatio + (seed << kShiftLeft) + (seed >> kShiftRight));
  }

  constexpr uint64_t kShaderBakeActionSchemaVersion = 5;
  //! Hashed in place of a define value, so `NAME` and `NAME=` differ.
  constexpr uint64_t kValuelessDefineMarker = 0xA5A5A5A5U;

} // namespace

auto ComputeToolchainHash(const ShaderCompileProfile& profile) -> uint64_t
{
  const std::string version = GetDxcVersionString();
  const std::string schema = std::string("dxc;") + "version=" + version + ";"
    + GetDxcArgumentSchema(profile);

  return oxygen::ComputeFNV1a64(schema.data(), schema.size());
}

auto ComputeShaderActionKey(const ShaderRequest& request,
  const ShaderViewBinding view_binding,
  std::span<const std::filesystem::path> include_dirs,
  const ShaderCompileProfile& profile) -> uint64_t
{
  const auto canonical_request
    = oxygen::graphics::CanonicalizeShaderRequest(ShaderRequest { request });

  uint64_t seed = 0;
  seed = HashCombine(seed,
    static_cast<uint64_t>(static_cast<std::underlying_type_t<ShaderType>>(
      canonical_request.stage)));
  seed = HashCombine(seed,
    oxygen::ComputeFNV1a64(canonical_request.source_path.data(),
      canonical_request.source_path.size()));
  seed = HashCombine(seed,
    oxygen::ComputeFNV1a64(canonical_request.entry_point.data(),
      canonical_request.entry_point.size()));
  for (const auto& define : canonical_request.defines) {
    seed = HashCombine(
      seed, oxygen::ComputeFNV1a64(define.name.data(), define.name.size()));
    if (define.value.has_value()) {
      seed = HashCombine(seed,
        oxygen::ComputeFNV1a64(define.value->data(), define.value->size()));
    } else {
      seed = HashCombine(seed, kValuelessDefineMarker);
    }
  }

  seed = HashCombine(seed, static_cast<uint64_t>(view_binding));

  const std::string dxc_version = GetDxcVersionString();
  seed = HashCombine(
    seed, oxygen::ComputeFNV1a64(dxc_version.data(), dxc_version.size()));

  const std::string argument_schema = GetDxcArgumentSchema(profile);
  seed = HashCombine(seed,
    oxygen::ComputeFNV1a64(argument_schema.data(), argument_schema.size()));

  for (const auto& include_dir : include_dirs) {
    const auto normalized = NormalizeIncludeRoot(include_dir);
    seed = HashCombine(
      seed, oxygen::ComputeFNV1a64(normalized.data(), normalized.size()));
  }

  seed = HashCombine(seed, kShaderBakeActionSchemaVersion);
  return seed;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
