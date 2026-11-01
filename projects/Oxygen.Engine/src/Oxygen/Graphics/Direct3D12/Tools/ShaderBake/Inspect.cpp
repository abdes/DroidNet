//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/ShaderLibraryIO.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Inspect.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  inline constexpr uint32_t kOxygenReflectionMagic = 0x4F585246U; // "OXRF"
  inline constexpr uint32_t kOxygenReflectionVersion = 1;

  //! Reads a string prefixed by its 16-bit length.
  auto ReadString16(serio::ByteReader& reader) -> Result<std::string>
  {
    const auto length = reader.Read<uint16_t>();
    if (!length) {
      return ::oxygen::Err(length.error());
    }
    const auto text = reader.ReadStringView(serio::CountOf(*length));
    if (!text) {
      return ::oxygen::Err(text.error());
    }
    return ::oxygen::Ok(std::string(*text));
  }

  auto BindKindToString(const uint8_t kind) -> std::string_view
  {
    switch (kind) {
    case 0:
      return "cbv";
    case 1:
      return "srv";
    case 2:
      return "uav";
    case 3:
      return "sampler";
    default:
      return "unknown";
    }
  }

  auto PrintReflectionBlob(std::span<const std::byte> blob) -> void
  {
    LOG_SCOPE_F(INFO, "Reflection");

    if (blob.empty()) {
      LOG_F(INFO, "(empty)");
      return;
    }

    auto cur = serio::ByteReader { blob };

    const auto magic = cur.Read<uint32_t>();
    const auto version = cur.Read<uint32_t>();
    const auto stage_u8 = cur.Read<uint8_t>();
    const auto sm_major = cur.Read<uint8_t>();
    const auto sm_minor = cur.Read<uint8_t>();
    const auto reserved = cur.Read<uint8_t>();
    const auto entry_point = ReadString16(cur);
    const auto bound_resources = cur.Read<uint32_t>();
    const auto tgx = cur.Read<uint32_t>();
    const auto tgy = cur.Read<uint32_t>();
    const auto tgz = cur.Read<uint32_t>();

    if (!magic || !version || !stage_u8 || !sm_major || !sm_minor || !reserved
      || !entry_point || !bound_resources || !tgx || !tgy || !tgz) {
      LOG_F(ERROR, "invalid OXRF: truncated header ({} bytes)", blob.size());
      return;
    }

    if (*magic != kOxygenReflectionMagic) {
      LOG_F(ERROR, "invalid OXRF: bad magic 0x{:08x}", *magic);
      return;
    }
    if (*version != kOxygenReflectionVersion) {
      LOG_F(ERROR, "unsupported OXRF version {}", *version);
      return;
    }

    LOG_F(INFO, "stage={}",
      oxygen::to_string(static_cast<oxygen::ShaderType>(*stage_u8)));
    LOG_F(INFO, "entry_point={}", *entry_point);
    LOG_F(INFO, "shader_model={}.{}", *sm_major, *sm_minor);
    LOG_F(INFO, "resources={}", *bound_resources);
    LOG_F(INFO, "threadgroup=({}, {}, {})", *tgx, *tgy, *tgz);

    if (*bound_resources == 0) {
      return;
    }

    LOG_SCOPE_F(INFO, "Resources");
    for (uint32_t i = 0; i < *bound_resources; ++i) {
      const auto resource_type = cur.Read<uint8_t>();
      const auto bind_kind = cur.Read<uint8_t>();
      const auto space = cur.Read<uint16_t>();
      const auto bind_point = cur.Read<uint32_t>();
      const auto bind_count = cur.Read<uint32_t>();
      const auto byte_size = cur.Read<uint32_t>();
      const auto name = ReadString16(cur);

      if (!resource_type || !bind_kind || !space || !bind_point || !bind_count
        || !byte_size || !name) {
        LOG_F(ERROR, "invalid OXRF: truncated resource {}", i);
        return;
      }

      LOG_F(INFO,
        "{}: name='{}' kind={} type={} space={} reg={} count={} byte_size={}",
        i, *name, BindKindToString(*bind_kind), *resource_type, *space,
        *bind_point, *bind_count, *byte_size);
    }
  }

  auto BackendToStringView(const std::array<char, 8>& backend)
    -> std::string_view
  {
    return oxygen::graphics::serio_utils::BackendStringToView(backend);
  }

  auto PrintHeader(const oxygen::graphics::ShaderLibraryReader::Library& lib)
    -> void
  {
    LOG_SCOPE_F(INFO, "Header");
    const auto backend = BackendToStringView(lib.backend);
    LOG_F(INFO, "backend={}", backend);
    LOG_F(INFO, "toolchain_hash=0x{:016x}", lib.toolchain_hash);
    LOG_F(INFO, "modules={}", lib.modules.size());
  }

  auto PrintModule(const oxygen::graphics::ShaderLibraryReader::Module& m,
    const bool show_defines, const bool show_offsets,
    const bool show_reflection) -> void
  {
    LOG_SCOPE_F(INFO,
      fmt::format("Module: {}:{} ({})", m.source_path, m.entry_point,
        oxygen::to_string(m.stage))
        .c_str());

    LOG_F(INFO, "defines={}", m.defines.size());
    LOG_F(INFO, "dxil={} bytes", m.dxil_blob.size());
    LOG_F(INFO, "reflection={} bytes", m.reflection_blob.size());

    if (show_defines && !m.defines.empty()) {
      LOG_SCOPE_F(INFO, "Defines");
      for (const auto& d : m.defines) {
        if (d.value.has_value()) {
          LOG_F(INFO, "-D{}={}", d.name, *d.value);
        } else {
          LOG_F(INFO, "-D{}", d.name);
        }
      }
    }

    if (show_offsets) {
      LOG_SCOPE_F(INFO, "Offsets");
      LOG_F(INFO, "dxil_offset={} dxil_size={}", m.dxil_offset, m.dxil_size);
      LOG_F(INFO, "refl_offset={} refl_size={}", m.reflection_offset,
        m.reflection_size);
    }

    if (show_reflection) {
      PrintReflectionBlob(std::span<const std::byte>(m.reflection_blob));
    }
  }

} // namespace

auto InspectShaderLibrary(const InspectArgs& args) -> int
{
  if (args.file.empty()) {
    LOG_F(ERROR, "inspect: --file is required");
    return 2;
  }

  auto lib = oxygen::graphics::ShaderLibraryReader::ReadFromFile(args.file);

  const bool any_section_selected = args.header_only || args.modules_only;
  const bool show_header = args.header_only || !any_section_selected;
  const bool show_modules = args.modules_only || !any_section_selected;

  if (show_header) {
    PrintHeader(lib);
  }

  if (show_modules) {
    for (const auto& m : lib.modules) {
      PrintModule(
        m, args.show_defines, args.show_offsets, args.show_reflection);
    }
  }

  return 0;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
