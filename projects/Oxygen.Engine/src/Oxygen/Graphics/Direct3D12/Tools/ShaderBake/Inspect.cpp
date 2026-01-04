//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/ShaderLibraryIO.h>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Inspect.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  inline constexpr uint32_t kOxygenReflectionMagic = 0x4F585246U; // "OXRF"
  inline constexpr uint32_t kOxygenReflectionVersion = 1;

  class ByteCursor {
  public:
    explicit ByteCursor(std::span<const std::byte> bytes) noexcept
      : bytes_(bytes)
    {
    }

    [[nodiscard]] auto Remaining() const noexcept -> size_t
    {
      return bytes_.size() - pos_;
    }

    auto ReadU8() -> std::optional<uint8_t>
    {
      if (Remaining() < sizeof(uint8_t)) {
        return std::nullopt;
      }
      const uint8_t v = static_cast<uint8_t>(bytes_[pos_]);
      pos_ += sizeof(uint8_t);
      return v;
    }

    auto ReadU16() -> std::optional<uint16_t>
    {
      if (Remaining() < sizeof(uint16_t)) {
        return std::nullopt;
      }
      const uint8_t b0 = static_cast<uint8_t>(bytes_[pos_ + 0]);
      const uint8_t b1 = static_cast<uint8_t>(bytes_[pos_ + 1]);
      pos_ += sizeof(uint16_t);
      return static_cast<uint16_t>(
        static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8));
    }

    auto ReadU32() -> std::optional<uint32_t>
    {
      if (Remaining() < sizeof(uint32_t)) {
        return std::nullopt;
      }
      const uint8_t b0 = static_cast<uint8_t>(bytes_[pos_ + 0]);
      const uint8_t b1 = static_cast<uint8_t>(bytes_[pos_ + 1]);
      const uint8_t b2 = static_cast<uint8_t>(bytes_[pos_ + 2]);
      const uint8_t b3 = static_cast<uint8_t>(bytes_[pos_ + 3]);
      pos_ += sizeof(uint32_t);
      return static_cast<uint32_t>(static_cast<uint32_t>(b0)
        | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16)
        | (static_cast<uint32_t>(b3) << 24));
    }

    auto ReadString16() -> std::optional<std::string>
    {
      const auto len_opt = ReadU16();
      if (!len_opt) {
        return std::nullopt;
      }
      const auto len = *len_opt;
      if (Remaining() < len) {
        return std::nullopt;
      }
      const char* data = reinterpret_cast<const char*>(bytes_.data() + pos_);
      std::string out(data, data + len);
      pos_ += len;
      return out;
    }

  private:
    std::span<const std::byte> bytes_;
    size_t pos_ = 0;
  };

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

    ByteCursor cur(blob);

    const auto magic = cur.ReadU32();
    const auto version = cur.ReadU32();
    const auto stage_u8 = cur.ReadU8();
    const auto sm_major = cur.ReadU8();
    const auto sm_minor = cur.ReadU8();
    const auto reserved = cur.ReadU8();
    const auto entry_point = cur.ReadString16();
    const auto bound_resources = cur.ReadU32();
    const auto tgx = cur.ReadU32();
    const auto tgy = cur.ReadU32();
    const auto tgz = cur.ReadU32();

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
      const auto resource_type = cur.ReadU8();
      const auto bind_kind = cur.ReadU8();
      const auto space = cur.ReadU16();
      const auto bind_point = cur.ReadU32();
      const auto bind_count = cur.ReadU32();
      const auto byte_size = cur.ReadU32();
      const auto name = cur.ReadString16();

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
