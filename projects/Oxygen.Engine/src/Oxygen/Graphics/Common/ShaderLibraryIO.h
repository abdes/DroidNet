//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::graphics {

// OXSL v1 header.
inline constexpr uint32_t kOxslMagic = 0x4F58534CU; // "OXSL"
inline constexpr uint32_t kOxslVersion = 1;
inline constexpr size_t kOxslBackendFieldSize = 8;

namespace serio_utils {

  inline auto ThrowOnError(const oxygen::Result<void>& r, std::string_view what)
    -> void
  {
    if (!r) {
      throw std::runtime_error(std::string(what) + ": " + r.error().message());
    }
  }

  template <typename T>
  inline auto ValueOrThrow(oxygen::Result<T> r, std::string_view what) -> T
  {
    if (!r) {
      throw std::runtime_error(std::string(what) + ": " + r.error().message());
    }
    return std::move(r).move_value();
  }

  inline auto CheckedU16Size(const size_t n, std::string_view what) -> uint16_t
  {
    if (n > 0xFFFFu) {
      throw std::runtime_error(std::string(what) + " too long");
    }
    return static_cast<uint16_t>(n);
  }

  inline auto PackedGuard(serio::AnyWriter& w) -> serio::AlignmentGuard
  {
    return w.ScopedAlignment(1);
  }

  inline auto PackedGuard(serio::AnyReader& r) -> serio::AlignmentGuard
  {
    return r.ScopedAlignment(1);
  }

  inline auto WriteFixed8(serio::AnyWriter& w, const std::array<char, 8>& s)
    -> void
  {
    const auto bytes = std::as_bytes(std::span(s));
    ThrowOnError(w.WriteBlob(bytes), "write fixed8");
  }

  inline auto ReadFixed8(serio::AnyReader& r) -> std::array<char, 8>
  {
    std::array<char, 8> out {};
    ThrowOnError(
      r.ReadBlobInto(std::as_writable_bytes(std::span(out))), "read fixed8");
    return out;
  }

  inline auto WriteUtf8String16(serio::AnyWriter& w, std::string_view s) -> void
  {
    ThrowOnError(w.Write<uint16_t>(CheckedU16Size(s.size(), "string")),
      "write string16 length");
    if (!s.empty()) {
      ThrowOnError(
        w.WriteBlob(std::as_bytes(std::span<const char>(s.data(), s.size()))),
        "write string16 bytes");
    }
  }

  inline auto ReadUtf8String16(serio::AnyReader& r) -> std::string
  {
    const uint16_t len
      = ValueOrThrow(r.Read<uint16_t>(), "read string16 length");
    std::string s;
    s.resize(len);
    if (len > 0) {
      ThrowOnError(
        r.ReadBlobInto(std::as_writable_bytes(std::span(s.data(), s.size()))),
        "read string16 bytes");
    }
    return s;
  }

  inline auto BackendStringToView(const std::array<char, 8>& backend)
    -> std::string_view
  {
    const auto end = std::ranges::find(backend, '\0');
    return std::string_view(
      backend.data(), static_cast<size_t>(std::distance(backend.begin(), end)));
  }

  inline auto CheckedSizeT(const uint64_t v, std::string_view what) -> size_t
  {
    if (v > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
      throw std::runtime_error(std::string(what) + " is too large");
    }
    return static_cast<size_t>(v);
  }

} // namespace serio_utils

class ShaderLibraryWriter {
public:
  struct ModuleView {
    ShaderType stage { ShaderType::kUnknown };
    std::string_view source_path;
    std::string_view entry_point;
    std::span<const ShaderDefine> defines;
    std::span<const std::byte> dxil;
    std::span<const std::byte> reflection;
  };

  ShaderLibraryWriter(std::array<char, 8> backend, uint64_t toolchain_hash)
    : backend_(backend)
    , toolchain_hash_(toolchain_hash)
  {
  }

  auto WriteToFile(const std::filesystem::path& out_file,
    std::span<const ModuleView> modules) const -> void
  {
    std::filesystem::create_directories(out_file.parent_path());

    const auto tmp_path = out_file.wstring() + L".tmp";
    const std::filesystem::path tmp_file(tmp_path);

    // Ensure the file handle is released before rename on Windows.
    {
      serio::FileStream<> stream(tmp_file, std::ios::out | std::ios::trunc);
      serio::Writer<serio::FileStream<>> w(stream);
      const auto packed = serio_utils::PackedGuard(w);
      (void)packed;

      auto size_of_string16 = [](std::string_view s) -> uint64_t {
        return 2U + static_cast<uint64_t>(s.size());
      };
      auto size_of_entry
        = [&](const ModuleView& m,
            std::span<const ShaderDefine> sorted_defines) -> uint64_t {
        uint64_t size = 0;
        size += 1U; // stage
        size += size_of_string16(m.source_path);
        size += size_of_string16(m.entry_point);
        size += 2U; // define count
        for (const auto& d : sorted_defines) {
          size += size_of_string16(d.name);
          size += size_of_string16(d.value.value_or(""));
        }
        size += 8U * 4U; // offsets/sizes
        return size;
      };

      struct ComputedEntry {
        std::vector<ShaderDefine> sorted_defines;
        uint64_t dxil_offset {};
        uint64_t dxil_size {};
        uint64_t refl_offset {};
        uint64_t refl_size {};
      };

      constexpr uint64_t kHeaderSize = 4U + 4U + 8U + 8U + 4U;
      std::vector<ComputedEntry> computed;
      computed.reserve(modules.size());

      uint64_t table_size = 0;
      for (const auto& m : modules) {
        ComputedEntry e;
        e.sorted_defines.assign(m.defines.begin(), m.defines.end());
        std::ranges::sort(
          e.sorted_defines, {}, [](const ShaderDefine& d) { return d.name; });

        table_size += size_of_entry(m, e.sorted_defines);
        computed.push_back(std::move(e));
      }

      uint64_t payload_offset = kHeaderSize + table_size;
      for (size_t i = 0; i < modules.size(); ++i) {
        auto& e = computed[i];
        const auto& m = modules[i];

        e.dxil_offset = payload_offset;
        e.dxil_size = static_cast<uint64_t>(m.dxil.size());
        payload_offset += e.dxil_size;

        e.refl_offset = payload_offset;
        e.refl_size = static_cast<uint64_t>(m.reflection.size());
        payload_offset += e.refl_size;
      }

      serio_utils::ThrowOnError(w.Write<uint32_t>(kOxslMagic), "write magic");
      serio_utils::ThrowOnError(
        w.Write<uint32_t>(kOxslVersion), "write version");
      serio_utils::WriteFixed8(w, backend_);
      serio_utils::ThrowOnError(
        w.Write<uint64_t>(toolchain_hash_), "write toolchain_hash");
      serio_utils::ThrowOnError(
        w.Write<uint32_t>(static_cast<uint32_t>(modules.size())),
        "write module_count");

      for (size_t i = 0; i < modules.size(); ++i) {
        const auto& m = modules[i];
        const auto& e = computed[i];

        serio_utils::ThrowOnError(
          w.Write<uint8_t>(static_cast<uint8_t>(m.stage)), "write stage");
        serio_utils::WriteUtf8String16(w, m.source_path);
        serio_utils::WriteUtf8String16(w, m.entry_point);

        serio_utils::ThrowOnError(w.Write<uint16_t>(serio_utils::CheckedU16Size(
                                    e.sorted_defines.size(), "define list")),
          "write define_count");
        for (const auto& d : e.sorted_defines) {
          serio_utils::WriteUtf8String16(w, d.name);
          serio_utils::WriteUtf8String16(w, d.value.value_or(""));
        }

        serio_utils::ThrowOnError(
          w.Write<uint64_t>(e.dxil_offset), "write dxil_offset");
        serio_utils::ThrowOnError(
          w.Write<uint64_t>(e.dxil_size), "write dxil_size");
        serio_utils::ThrowOnError(
          w.Write<uint64_t>(e.refl_offset), "write refl_offset");
        serio_utils::ThrowOnError(
          w.Write<uint64_t>(e.refl_size), "write refl_size");
      }

      for (const auto& m : modules) {
        if (!m.dxil.empty()) {
          serio_utils::ThrowOnError(w.WriteBlob(m.dxil), "write dxil blob");
        }
        if (!m.reflection.empty()) {
          serio_utils::ThrowOnError(
            w.WriteBlob(m.reflection), "write reflection blob");
        }
      }

      serio_utils::ThrowOnError(w.Flush(), "flush");
    }

    if (std::filesystem::exists(out_file)) {
      std::filesystem::remove(out_file);
    }
    std::filesystem::rename(tmp_file, out_file);
  }

private:
  std::array<char, 8> backend_ {};
  uint64_t toolchain_hash_ {};
};

class ShaderLibraryReader {
public:
  struct Module {
    ShaderType stage { ShaderType::kUnknown };
    std::string source_path;
    std::string entry_point;
    std::vector<ShaderDefine> defines;
    uint64_t dxil_offset {};
    uint64_t dxil_size {};
    uint64_t reflection_offset {};
    uint64_t reflection_size {};

    std::vector<std::byte> dxil_blob;
    std::vector<std::byte> reflection_blob;
  };

  struct Library {
    std::array<char, 8> backend {};
    uint64_t toolchain_hash {};
    std::vector<Module> modules;
  };

  [[nodiscard]] static auto ReadFromFile(const std::filesystem::path& file,
    std::string_view expected_backend = {}) -> Library
  {
    serio::FileStream<> stream(file, std::ios::in);
    serio::Reader<serio::FileStream<>> r(stream);
    const auto packed = serio_utils::PackedGuard(r);
    (void)packed;

    const uint32_t magic
      = serio_utils::ValueOrThrow(r.Read<uint32_t>(), "read magic");
    const uint32_t version
      = serio_utils::ValueOrThrow(r.Read<uint32_t>(), "read version");

    if (magic != kOxslMagic || version != kOxslVersion) {
      throw std::runtime_error("invalid shader library header");
    }

    Library lib {};
    lib.backend = serio_utils::ReadFixed8(r);
    lib.toolchain_hash
      = serio_utils::ValueOrThrow(r.Read<uint64_t>(), "read toolchain_hash");
    const uint32_t module_count
      = serio_utils::ValueOrThrow(r.Read<uint32_t>(), "read module_count");

    const auto backend_view = serio_utils::BackendStringToView(lib.backend);
    if (!expected_backend.empty() && backend_view != expected_backend) {
      throw std::runtime_error("shader library backend mismatch");
    }

    lib.modules.reserve(module_count);
    for (uint32_t i = 0; i < module_count; ++i) {
      Module m {};
      const auto stage_u8
        = serio_utils::ValueOrThrow(r.Read<uint8_t>(), "read stage");
      m.stage = static_cast<ShaderType>(static_cast<uint32_t>(stage_u8));

      m.source_path = serio_utils::ReadUtf8String16(r);
      m.entry_point = serio_utils::ReadUtf8String16(r);

      const uint16_t define_count
        = serio_utils::ValueOrThrow(r.Read<uint16_t>(), "read define_count");
      m.defines.reserve(define_count);
      for (uint16_t d = 0; d < define_count; ++d) {
        auto name = serio_utils::ReadUtf8String16(r);
        auto value = serio_utils::ReadUtf8String16(r);
        m.defines.push_back(ShaderDefine {
          .name = std::move(name),
          .value = value.empty() ? std::optional<std::string> {}
                                 : std::make_optional(std::move(value)),
        });
      }

      m.dxil_offset
        = serio_utils::ValueOrThrow(r.Read<uint64_t>(), "read dxil_offset");
      m.dxil_size
        = serio_utils::ValueOrThrow(r.Read<uint64_t>(), "read dxil_size");
      m.reflection_offset
        = serio_utils::ValueOrThrow(r.Read<uint64_t>(), "read refl_offset");
      m.reflection_size
        = serio_utils::ValueOrThrow(r.Read<uint64_t>(), "read refl_size");

      lib.modules.push_back(std::move(m));
    }

    for (auto& m : lib.modules) {
      serio_utils::ThrowOnError(
        r.Seek(serio_utils::CheckedSizeT(m.dxil_offset, "dxil_offset")),
        "seek dxil blob");
      m.dxil_blob = serio_utils::ValueOrThrow(
        r.ReadBlob(serio_utils::CheckedSizeT(m.dxil_size, "dxil_size")),
        "read dxil blob");

      serio_utils::ThrowOnError(r.Seek(serio_utils::CheckedSizeT(
                                  m.reflection_offset, "reflection_offset")),
        "seek reflection blob");
      m.reflection_blob = serio_utils::ValueOrThrow(
        r.ReadBlob(
          serio_utils::CheckedSizeT(m.reflection_size, "reflection_size")),
        "read reflection blob");
    }

    return lib;
  }
};

} // namespace oxygen::graphics
