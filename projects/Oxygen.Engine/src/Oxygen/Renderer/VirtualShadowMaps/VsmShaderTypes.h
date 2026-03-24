//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

namespace oxygen::renderer::vsm {

// These are narrow shader-facing ABI payloads. Keep the canonical CPU cache
// snapshots separate unless a record is already stable enough to share
// directly, like VsmPhysicalPageMeta.

inline constexpr std::uint32_t kVsmPageTableMappedBit = 0x80000000U;
inline constexpr std::uint32_t kVsmPageTablePhysicalPageMask = 0x7fffffffU;

enum class VsmShaderPageFlagBits : std::uint32_t {
  kNone = 0U,
  kAllocated = OXYGEN_FLAG(0),
  kDynamicUncached = OXYGEN_FLAG(1),
  kStaticUncached = OXYGEN_FLAG(2),
  kDetailGeometry = OXYGEN_FLAG(3),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(VsmShaderPageFlagBits)

struct VsmShaderPageTableEntry {
  std::uint32_t encoded { 0U };

  auto operator==(const VsmShaderPageTableEntry&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageTableEntry>);
static_assert(sizeof(VsmShaderPageTableEntry) == 4U);

[[nodiscard]] constexpr auto MakeUnmappedShaderPageTableEntry() noexcept
  -> VsmShaderPageTableEntry
{
  return {};
}

[[nodiscard]] constexpr auto MakeMappedShaderPageTableEntry(
  const VsmPhysicalPageIndex physical_page) noexcept -> VsmShaderPageTableEntry
{
  return VsmShaderPageTableEntry {
    .encoded = kVsmPageTableMappedBit
      | (physical_page.value & kVsmPageTablePhysicalPageMask),
  };
}

[[nodiscard]] constexpr auto IsMapped(
  const VsmShaderPageTableEntry entry) noexcept -> bool
{
  return (entry.encoded & kVsmPageTableMappedBit) != 0U;
}

[[nodiscard]] constexpr auto DecodePhysicalPageIndex(
  const VsmShaderPageTableEntry entry) noexcept -> VsmPhysicalPageIndex
{
  return VsmPhysicalPageIndex {
    .value = entry.encoded & kVsmPageTablePhysicalPageMask,
  };
}

struct VsmShaderPageFlags {
  std::uint32_t bits { 0U };

  auto operator==(const VsmShaderPageFlags&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageFlags>);
static_assert(sizeof(VsmShaderPageFlags) == 4U);

[[nodiscard]] constexpr auto HasAnyFlag(const VsmShaderPageFlags flags,
  const VsmShaderPageFlagBits bits) noexcept -> bool
{
  return (flags.bits & static_cast<std::uint32_t>(bits)) != 0U;
}

enum class VsmProjectionLightType : std::uint32_t {
  kLocal = 0U,
  kDirectional = 1U,
};

struct VsmProjectionData {
  glm::mat4 view_matrix { 1.0F };
  glm::mat4 projection_matrix { 1.0F };
  glm::vec4 view_origin_ws_pad { 0.0F, 0.0F, 0.0F, 0.0F };
  glm::ivec2 clipmap_corner_offset { 0, 0 };
  std::uint32_t clipmap_level { 0U };
  std::uint32_t light_type { static_cast<std::uint32_t>(
    VsmProjectionLightType::kLocal) };

  auto operator==(const VsmProjectionData&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmProjectionData>);
static_assert(sizeof(VsmProjectionData) == 160U);
static_assert(offsetof(VsmProjectionData, view_matrix) == 0U);
static_assert(offsetof(VsmProjectionData, projection_matrix) == 64U);
static_assert(offsetof(VsmProjectionData, view_origin_ws_pad) == 128U);
static_assert(offsetof(VsmProjectionData, clipmap_corner_offset) == 144U);
static_assert(offsetof(VsmProjectionData, clipmap_level) == 152U);
static_assert(offsetof(VsmProjectionData, light_type) == 156U);

} // namespace oxygen::renderer::vsm
