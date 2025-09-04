//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace oxygen::engine {

//=== Pass Mask Flags
//===-------------------------------------------------------------//
//! Render pass classification flags for DrawMetadata records.
/*!
 * Each DrawMetadata carries a bit mask describing which high-level rendering
 * bucket(s) it belongs to. Current taxonomy is conservative; future bits are
 * reserved and documented for design continuity.
 *
 * Active bits:
 *  - kOpaqueOrMasked : Depth writing surfaces (opaque or alpha test grouped)
 *  - kTransparent    : Alpha blended surfaces (depth read, no depth write)
 *
 * Reserved (not yet produced):
 *  - kAdditive       : Additive/emissive order-independent
 *  - kTransmission   : Refraction / glass / subsurface
 *  - kDecal          : Projected decals
 *  - kUi             : Overlay / UI
 */
enum class PassMaskFlags : uint32_t {
  kNone = 0u,
  kOpaqueOrMasked = 1u << 0,
  kTransparent = 1u << 1,
  kAdditive = 1u << 2,
  kTransmission = 1u << 3,
  kDecal = 1u << 4,
  kUi = 1u << 5,
};

inline constexpr PassMaskFlags operator|(
  PassMaskFlags a, PassMaskFlags b) noexcept
{
  return static_cast<PassMaskFlags>(
    static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr PassMaskFlags operator&(
  PassMaskFlags a, PassMaskFlags b) noexcept
{
  return static_cast<PassMaskFlags>(
    static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline constexpr PassMaskFlags& operator|=(
  PassMaskFlags& a, PassMaskFlags b) noexcept
{
  a = a | b;
  return a;
}
inline constexpr bool HasFlag(uint32_t mask, PassMaskFlags f) noexcept
{
  return (mask & static_cast<uint32_t>(f)) != 0u;
}

inline std::string PassMaskFlagsToString(uint32_t mask)
{
  if (mask == 0u) {
    return "None";
  }
  struct Entry {
    PassMaskFlags flag;
    std::string_view name;
  };
  static constexpr Entry kTable[] = {
    { PassMaskFlags::kOpaqueOrMasked, "OpaqueOrMasked" },
    { PassMaskFlags::kTransparent, "Transparent" },
    { PassMaskFlags::kAdditive, "Additive" },
    { PassMaskFlags::kTransmission, "Transmission" },
    { PassMaskFlags::kDecal, "Decal" },
    { PassMaskFlags::kUi, "Ui" },
  };
  std::string out;
  for (const auto& e : kTable) {
    if ((mask & static_cast<uint32_t>(e.flag)) != 0u) {
      if (!out.empty())
        out += '|';
      out += e.name;
    }
  }
  if (out.empty())
    out = "<UnknownMask>";
  return out;
}

} // namespace oxygen::engine
