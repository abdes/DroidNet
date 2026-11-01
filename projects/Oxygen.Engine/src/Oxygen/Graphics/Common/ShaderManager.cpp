//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/ShaderManager.h>
#include <Oxygen/Serio/Record.h>
#include <Oxygen/Serio/Types.h>

// NOTE: ShaderManager is intentionally compilation-free.

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/ShaderLibraryIO.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Serio/ByteReader.h>

using oxygen::ShaderType;
using oxygen::graphics::ShaderManager;

namespace {

inline constexpr uint32_t kOxrfMagic = 0x4F585246U; // "OXRF"
inline constexpr uint32_t kOxrfVersion = 1;

inline constexpr uint8_t kExpectedShaderModelMajor = 6;
inline constexpr uint8_t kExpectedShaderModelMinor = 6;

// DXC reflects cbuffer sizes using HLSL packing rules which differ from C++.
// The C++ GpuData struct is padded to 256 bytes to match the HLSL cbuffer ABI.
inline constexpr uint32_t kExpectedViewConstantsByteSize = 256;
// Note: DXC reports constant buffer sizes rounded up to 16-byte alignment.
// RootConstants is modeled as a 2x32-bit root constant range, but is declared
// as a cbuffer in HLSL; reflection reports 16 bytes for two uints.
inline constexpr uint32_t kExpectedRootConstantsByteSize = 16U;

enum class OxrfBindKind : uint8_t {
  kCbv = 0,
  kSrv = 1,
  kUav = 2,
  kSampler = 3,
};

struct OxrfHeader {
  ShaderType stage { ShaderType::kUnknown };
  uint8_t shader_model_major {};
  uint8_t shader_model_minor {};
  std::string entry_point;
  uint32_t bound_resources {};
  uint32_t tgx {};
  uint32_t tgy {};
  uint32_t tgz {};
};

struct OxrfResource {
  uint8_t resource_type {}; // D3D_SIT_* (serialized as u8)
  OxrfBindKind bind_kind { OxrfBindKind::kCbv };
  uint16_t space {};
  uint32_t bind_point {};
  uint32_t bind_count {};
  uint32_t byte_size {};
  std::string name;
};

template <oxygen::serio::BulkElement T>
auto ReadOrThrow(oxygen::serio::ByteReader& reader, const std::string_view what)
  -> T
{
  const auto value = reader.Read<T>();
  if (!value) {
    throw std::runtime_error(std::string(what) + ": truncated");
  }
  return *value;
}

auto ReadString16OrThrow(
  oxygen::serio::ByteReader& reader, const std::string_view what) -> std::string
{
  const auto length = ReadOrThrow<uint16_t>(reader, "read string length");
  const auto text = reader.ReadStringView(oxygen::serio::CountOf(length));
  if (!text) {
    throw std::runtime_error(std::string(what) + ": truncated");
  }
  return std::string(*text);
}

auto NormalizePathForCompare(std::string_view s) -> std::string
{
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    const char normalized = (c == '\\') ? '/' : c;
    out.push_back(
      static_cast<char>(std::tolower(static_cast<unsigned char>(normalized))));
  }
  return out;
}

auto EndsWith(std::string_view s, std::string_view suffix) -> bool
{
  return s.size() >= suffix.size()
    && s.substr(s.size() - suffix.size()) == suffix;
}

auto IsExcludedFromReflectionValidation(
  const oxygen::graphics::ShaderRequest& r) -> bool
{
  const auto normalized = NormalizePathForCompare(r.source_path);
  return EndsWith(normalized, "ui/imgui.hlsl");
}

auto ParseOxrfOrThrow(std::span<const std::byte> blob)
  -> std::pair<OxrfHeader, std::vector<OxrfResource>>
{
  auto c = oxygen::serio::ByteReader { blob };
  const auto magic = ReadOrThrow<uint32_t>(c, "read OXRF magic");
  const auto version = ReadOrThrow<uint32_t>(c, "read OXRF version");

  if (magic != kOxrfMagic || version != kOxrfVersion) {
    throw std::runtime_error("invalid OXRF blob header");
  }

  const auto stage_u8 = ReadOrThrow<uint8_t>(c, "read stage");
  const auto sm_major = ReadOrThrow<uint8_t>(c, "read shader_model_major");
  const auto sm_minor = ReadOrThrow<uint8_t>(c, "read shader_model_minor");
  (void)ReadOrThrow<uint8_t>(c, "read reserved");
  auto entry_point = ReadString16OrThrow(c, "read entry_point");

  OxrfHeader header {
    .stage = static_cast<ShaderType>(static_cast<uint32_t>(stage_u8)),
    .shader_model_major = sm_major,
    .shader_model_minor = sm_minor,
    .entry_point = std::move(entry_point),
    .bound_resources = ReadOrThrow<uint32_t>(c, "read bound_resources"),
    .tgx = ReadOrThrow<uint32_t>(c, "read tgx"),
    .tgy = ReadOrThrow<uint32_t>(c, "read tgy"),
    .tgz = ReadOrThrow<uint32_t>(c, "read tgz"),
  };

  std::vector<OxrfResource> resources;
  resources.reserve(header.bound_resources);

  for (uint32_t i = 0; i < header.bound_resources; ++i) {
    const auto resource_type = ReadOrThrow<uint8_t>(c, "read resource_type");
    const auto kind_u8 = ReadOrThrow<uint8_t>(c, "read bind_kind");
    const auto bind_kind = static_cast<OxrfBindKind>(kind_u8);

    OxrfResource r {
      .resource_type = resource_type,
      .bind_kind = bind_kind,
      .space = ReadOrThrow<uint16_t>(c, "read space"),
      .bind_point = ReadOrThrow<uint32_t>(c, "read bind_point"),
      .bind_count = ReadOrThrow<uint32_t>(c, "read bind_count"),
      .byte_size = ReadOrThrow<uint32_t>(c, "read byte_size"),
      .name = ReadString16OrThrow(c, "read name"),
    };
    resources.push_back(std::move(r));
  }

  if (c.Remaining().get() != 0U) {
    throw std::runtime_error("OXRF blob has trailing bytes");
  }

  return { std::move(header), std::move(resources) };
}

auto ValidateThreadGroupOrThrow(const oxygen::graphics::ShaderRequest& request,
  const OxrfHeader& header) -> void
{
  if (request.stage == ShaderType::kCompute) {
    if (header.tgx == 0U || header.tgy == 0U || header.tgz == 0U) {
      throw std::runtime_error("compute shader has invalid threadgroup size");
    }
    if (header.tgx > 1024U || header.tgy > 1024U || header.tgz > 64U) {
      throw std::runtime_error("compute shader threadgroup size too large");
    }
    const uint64_t product = static_cast<uint64_t>(header.tgx)
      * static_cast<uint64_t>(header.tgy) * static_cast<uint64_t>(header.tgz);
    if (product > 1024U) {
      throw std::runtime_error(
        "compute shader threadgroup size product exceeds 1024");
    }
  } else {
    if (header.tgx != 0U || header.tgy != 0U || header.tgz != 0U) {
      throw std::runtime_error(
        "non-compute shader must have threadgroup size 0,0,0");
    }
  }
}

auto ValidateBindingsOrThrow(const oxygen::graphics::ShaderRequest& request,
  const std::vector<OxrfResource>& resources) -> void
{
  bool saw_b1 = false;
  bool saw_b2 = false;

  for (const auto& r : resources) {
    if (r.bind_kind != OxrfBindKind::kCbv) {
      throw std::runtime_error("unexpected non-CBV resource binding");
    }

    if (r.space != 0U) {
      throw std::runtime_error("CBV must be in space 0");
    }
    if (r.bind_count != 1U) {
      throw std::runtime_error("CBV array bindings are not allowed");
    }

    if (r.bind_point == 1U) {
      saw_b1 = true;

      if (r.name != "ViewConstants") {
        throw std::runtime_error("b1 must bind ViewConstants");
      }
      if (r.byte_size != kExpectedViewConstantsByteSize) {
        throw std::runtime_error(
          fmt::format("ViewConstants byte_size mismatch (expected {}, got {})",
            kExpectedViewConstantsByteSize, r.byte_size));
      }
      continue;
    }
    if (r.bind_point == 2U) {
      saw_b2 = true;

      if (r.name != "RootConstants") {
        throw std::runtime_error("b2 must bind RootConstants");
      }
      if (r.byte_size != kExpectedRootConstantsByteSize) {
        throw std::runtime_error(
          fmt::format("RootConstants byte_size mismatch (expected {}, got {})",
            kExpectedRootConstantsByteSize, r.byte_size));
      }
      continue;
    }

    throw std::runtime_error(
      "only CBV bindings b1 and b2 are allowed (space0)");
  }

  // Note: Some stages may not reference the constant buffers.
  // We keep this permissive and only gate on disallowed bindings.
  (void)saw_b1;
  (void)saw_b2;
  (void)request;
}

auto ValidateReflectionOrThrow(const oxygen::graphics::ShaderRequest& request,
  std::span<const std::byte> reflection_blob) -> void
{
  if (IsExcludedFromReflectionValidation(request)) {
    return;
  }

  if (reflection_blob.empty()) {
    throw std::runtime_error("missing reflection blob");
  }

  const auto [header, resources] = ParseOxrfOrThrow(reflection_blob);

  if (header.stage != request.stage) {
    throw std::runtime_error("reflection stage does not match module stage");
  }
  if (header.entry_point != request.entry_point) {
    throw std::runtime_error(
      "reflection entry_point does not match module entry_point");
  }
  if (header.shader_model_major != kExpectedShaderModelMajor
    || header.shader_model_minor != kExpectedShaderModelMinor) {
    throw std::runtime_error("unsupported shader model in reflection");
  }

  ValidateThreadGroupOrThrow(request, header);
  ValidateBindingsOrThrow(request, resources);
}

auto GetArchivePath(const ShaderManager::Config& config)
  -> std::filesystem::path
{
  std::filesystem::path archive_path = config.archive_dir;
  try {
    std::filesystem::create_directories(archive_path);
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_F(ERROR, "Failed to create archive directory `{}`: {}",
      archive_path.string(), e.what());
    throw;
  }

  archive_path /= config.archive_file_name;
  LOG_F(INFO, "Using shader library at: {}", archive_path.string());
  return archive_path;
}

auto BytesToU32Words(const std::vector<std::byte>& bytes)
  -> std::vector<uint32_t>
{
  if ((bytes.size() % 4U) != 0U) {
    throw std::runtime_error("DXIL blob size is not 4-byte aligned");
  }
  std::vector<uint32_t> words;
  words.resize(bytes.size() / 4U);
  if (!bytes.empty()) {
    std::memcpy(words.data(), bytes.data(), bytes.size());
  }
  return words;
}

} // namespace

auto ShaderManager::Initialize() -> void
{
  archive_path_ = GetArchivePath(config_);
  Load();
}

auto ShaderManager::GetShaderBytecode(const ShaderRequest& request) const
  -> std::shared_ptr<IShaderByteCode>
{
  const auto canonical = CanonicalizeShaderRequest(ShaderRequest(request));
  const auto key = ComputeShaderRequestKey(canonical);
  const auto it = shader_cache_.find(key);
  if (it == shader_cache_.end()) {
    return nullptr;
  }

  return it->second.bytecode;
}
auto ShaderManager::Load() -> void
{
  Clear();

  if (!std::filesystem::exists(archive_path_)) {
    LOG_F(WARNING,
      "Shader library not found at `{}`; continuing with an empty shader cache",
      archive_path_.string());
    return;
  }

  try {
    const auto lib = oxygen::graphics::ShaderLibraryReader::ReadFromFile(
      archive_path_, config_.backend_name);

    for (const auto& m : lib.modules) {
      auto dxil_words = BytesToU32Words(m.dxil_blob);

      const auto request = CanonicalizeShaderRequest(ShaderRequest {
        .stage = m.stage,
        .source_path = m.source_path,
        .entry_point = m.entry_point,
        .defines = m.defines,
      });

      ValidateReflectionOrThrow(request, m.reflection_blob);

      const auto cache_key = ComputeShaderRequestKey(request);

      shader_cache_[cache_key] = ShaderModule {
        .request = request,
        .cache_key = cache_key,
        .bytecode = std::make_shared<
          oxygen::graphics::ShaderByteCode<std::vector<uint32_t>>>(
          std::move(dxil_words)),
        .reflection_blob = m.reflection_blob,
      };
    }
  } catch (const std::exception& e) {
    Clear();
    throw std::runtime_error("Failed to load shader library "
      + archive_path_.string() + ": " + std::string(e.what()));
  }
}

auto ShaderManager::Clear() noexcept -> void { shader_cache_.clear(); }

auto ShaderManager::HasShader(const ShaderRequest& request) const noexcept
  -> bool
{
  try {
    const auto canonical = CanonicalizeShaderRequest(ShaderRequest(request));
    const auto key = ComputeShaderRequestKey(canonical);
    return shader_cache_.contains(key);
  } catch (...) {
    return false;
  }
}

auto ShaderManager::GetShaderCount() const noexcept -> size_t
{
  return shader_cache_.size();
}
