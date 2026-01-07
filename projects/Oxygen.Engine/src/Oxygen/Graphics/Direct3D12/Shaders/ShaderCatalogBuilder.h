//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ShaderCatalogBuilder.h
//! @brief Compile-time shader catalog generation with automatic permutation
//! expansion.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Shaders.h>

namespace oxygen::graphics::d3d12 {

//! Maximum number of defines per shader entry.
inline constexpr size_t kMaxDefinesPerShader = 8;

//! A single shader entry in the catalog.
//!
//! Uses string_view for zero-allocation constexpr storage. The strings
//! point directly into the read-only data segment.
struct ShaderEntry {
  ShaderType type {};
  std::string_view path;
  std::string_view entry_point;
  std::array<std::string_view, kMaxDefinesPerShader> defines {};
  size_t define_count { 0 };
};

//! An entry point specification: shader type and entry function name.
struct EntryPoint {
  ShaderType type;
  std::string_view name;
};

//! Specification for a shader file with its entry points and permutations.
//!
//! Template parameters encode sizes for constexpr evaluation:
//! - E: number of entry points
//! - P: number of permutations (boolean defines)
template <size_t E, size_t P = 0> struct ShaderFileSpec {
  std::string_view path;
  std::array<EntryPoint, E> entries;
  std::array<std::string_view, P> permutations {};

  //! Number of ShaderEntry variants this spec expands to.
  [[nodiscard]] static consteval auto variant_count() -> size_t
  {
    return E * (size_t { 1 } << P);
  }
};

// CTAD guides for clean syntax
template <size_t E>
ShaderFileSpec(std::string_view, std::array<EntryPoint, E>)
  -> ShaderFileSpec<E, 0>;

template <size_t E, size_t P>
ShaderFileSpec(std::string_view, std::array<EntryPoint, E>,
  std::array<std::string_view, P>) -> ShaderFileSpec<E, P>;

//! Computes total shader count for a list of specs.
template <typename... Specs>
consteval auto ComputeShaderCount(const Specs&... specs) -> size_t
{
  return (specs.variant_count() + ...);
}

//! Expands a single spec into shader entries, writing to output starting at
//! index.
template <size_t E, size_t P, size_t N>
consteval auto ExpandSpec(const ShaderFileSpec<E, P>& spec,
  std::array<ShaderEntry, N>& output, size_t index) -> size_t
{
  constexpr size_t variant_count = size_t { 1 } << P;

  for (const auto& entry : spec.entries) {
    for (size_t mask = 0; mask < variant_count; ++mask) {
      ShaderEntry shader_entry {
        .type = entry.type,
        .path = spec.path,
        .entry_point = entry.name,
      };

      // Build defines from permutation mask
      size_t define_idx = 0;
      for (size_t i = 0; i < P; ++i) {
        if (mask & (size_t { 1 } << i)) {
          shader_entry.defines[define_idx++] = spec.permutations[i];
        }
      }
      shader_entry.define_count = define_idx;

      output[index++] = shader_entry;
    }
  }

  return index;
}

//! Generates a complete shader catalog from specs at compile time.
//!
//! Example:
//! @code
//! inline constexpr auto kEngineShaders = GenerateCatalog(
//!   ShaderFileSpec { "Forward.hlsl",
//!     std::array { EntryPoint{ShaderType::kPixel, "PS"},
//!                  EntryPoint{ShaderType::kVertex, "VS"} },
//!     std::array<std::string_view, 1> { "ALPHA_TEST" } },
//!   ShaderFileSpec { "ImGui.hlsl",
//!     std::array { EntryPoint{ShaderType::kVertex, "VS"},
//!                  EntryPoint{ShaderType::kPixel, "PS"} } }
//! );
//! @endcode
template <typename... Specs>
consteval auto GenerateCatalog(const Specs&... specs)
  -> std::array<ShaderEntry, ComputeShaderCount(specs...)>
{
  constexpr size_t total = ComputeShaderCount(specs...);
  std::array<ShaderEntry, total> result {};

  size_t index = 0;
  ((index = ExpandSpec(specs, result, index)), ...);

  return result;
}

//! Converts a ShaderEntry (constexpr catalog type) to ShaderInfo (runtime
//! type).
//!
//! Use this when interfacing with APIs that require std::string-based
//! ShaderInfo, such as shader compilation or library writing.
inline auto ToShaderInfo(const ShaderEntry& entry)
  -> oxygen::graphics::ShaderInfo
{
  oxygen::graphics::ShaderInfo info {
    .type = entry.type,
    .relative_path = std::string(entry.path),
    .entry_point = std::string(entry.entry_point),
  };

  for (size_t i = 0; i < entry.define_count; ++i) {
    info.defines.push_back(oxygen::graphics::ShaderDefine {
      .name = std::string(entry.defines[i]),
      .value = "1",
    });
  }

  return info;
}

} // namespace oxygen::graphics::d3d12
