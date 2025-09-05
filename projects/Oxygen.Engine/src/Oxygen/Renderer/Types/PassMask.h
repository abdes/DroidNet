//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>

namespace oxygen::engine {

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
enum class PassMaskBit : uint32_t {
  kNone = 0u,
  kOpaqueOrMasked = 1u << 0,
  kTransparent = 1u << 1,
  kAdditive = 1u << 2,
  kTransmission = 1u << 3,
  kDecal = 1u << 4,
  kUi = 1u << 5,
};

using PassMaskBase = oxygen::NamedType<uint32_t,
  // clang-format off
  struct PassMaskTag,
  oxygen::Comparable,
  oxygen::BitWiseAndable,
  oxygen::BitWiseOrable,
  oxygen::BitWiseXorable,
  oxygen::BitWiseLeftShiftable,
  oxygen::BitWiseRightShiftable,
  oxygen::BitWiseInvertable,
  oxygen::Hashable>; // clang-format on

//! Bitset representing which render passes an item participates in.
/*!
 Each bit corresponds to a renderer-defined pass ID. Pass assignment is
 deterministic: equivalent inputs must produce identical masks. Provides
 methods to manipulate PassMask bits.
*/
class PassMask : public PassMaskBase {
  static_assert(
    std::is_same_v<typename PassMaskBase::UnderlyingType, uint32_t>);

public:
  PassMask(PassMaskBit value = PassMaskBit::kNone)
    : PassMaskBase(std::to_underlying(value))
  {
  }

  PassMask(std::initializer_list<PassMaskBit> flags)
    : PassMaskBase(std::to_underlying(PassMaskBit::kNone))
  {
    auto new_value = get();
    for (auto f : flags) {
      new_value |= std::to_underlying(f);
    }
    *this = PassMask(static_cast<PassMaskBit>(new_value));
  }

  OXYGEN_DEFAULT_COPYABLE(PassMask)
  OXYGEN_DEFAULT_MOVABLE(PassMask)

  ~PassMask() noexcept = default;

  bool IsEmpty() const
  {
    return get() == std::to_underlying(PassMaskBit::kNone);
  }

  bool IsSet(PassMaskBit flag) const
  {
    return (get() & std::to_underlying(flag)) == std::to_underlying(flag);
  }

  void Set(PassMaskBit flag)
  {
    auto new_value = get() | std::to_underlying(flag);
    *this = PassMask { static_cast<PassMaskBit>(new_value) };
  }

  void Clear(PassMaskBit flag)
  {
    auto new_value = get() & ~std::to_underlying(flag);
    *this = PassMask { static_cast<PassMaskBit>(new_value) };
  }

  void Toggle(PassMaskBit flag)
  {
    auto new_value = get() ^ std::to_underlying(flag);
    *this = PassMask { static_cast<PassMaskBit>(new_value) };
  }
};

static_assert(sizeof(PassMask) == sizeof(uint32_t));

inline auto to_string(PassMask mask) -> std::string
{
  if (mask == PassMask { PassMaskBit::kNone }) {
    return "None";
  }
  struct Entry {
    PassMaskBit flag;
    std::string_view name;
  };
  static constexpr Entry kTable[] = {
    { PassMaskBit::kOpaqueOrMasked, "OpaqueOrMasked" },
    { PassMaskBit::kTransparent, "Transparent" },
    { PassMaskBit::kAdditive, "Additive" },
    { PassMaskBit::kTransmission, "Transmission" },
    { PassMaskBit::kDecal, "Decal" },
    { PassMaskBit::kUi, "UI" },
  };
  std::string out;
  for (const auto& e : kTable) {
    if (mask.IsSet(e.flag)) {
      if (!out.empty()) {
        out += '|';
      }
      out += e.name;
    }
  }
  if (out.empty()) {
    out = "__Unknown__";
  }
  return out;
}

} // namespace oxygen::engine
