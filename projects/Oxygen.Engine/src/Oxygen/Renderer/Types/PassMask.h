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
#include <Oxygen/Base/NoStd.h>

namespace oxygen::engine {

//! Render pass classification flags for DrawMetadata records.
/*!
 * Each DrawMetadata carries a bit mask describing which high-level rendering
 * bucket(s) it belongs to. Current taxonomy is conservative; future bits are
 * reserved and documented for design continuity.
 *
 * Active bits:
 *  - kDoubleSided  : Disable backface culling for this draw.
 *  - kOpaque       : Depth-writing opaque surfaces.
 *  - kMasked       : Depth-writing alpha-tested (cutout) surfaces.
 *  - kTransparent  : Alpha blended surfaces (depth read, no depth write).
 *
 * Reserved (not yet produced):
 *  - kAdditive     : Additive/emissive order-dependent.
 *  - kTransmission : Refraction / glass / subsurface.
 *  - kDecal        : Projected decals.
 *  - kUi           : Overlay / UI.
 */
enum class PassMaskBit : uint32_t {
  kNone = 0u,
  kDoubleSided = 1u << 0,
  kOpaque = 1u << 1,
  kMasked = 1u << 2,
  kTransparent = 1u << 3,
  kAdditive = 1u << 4,
  kTransmission = 1u << 5,
  kDecal = 1u << 6,
  kUi = 1u << 7,
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
    : PassMaskBase(nostd::to_underlying(value))
  {
  }

  PassMask(std::initializer_list<PassMaskBit> flags)
    : PassMaskBase(nostd::to_underlying(PassMaskBit::kNone))
  {
    auto new_value = get();
    for (auto f : flags) {
      new_value |= nostd::to_underlying(f);
    }
    *this = PassMask(static_cast<PassMaskBit>(new_value));
  }

  OXYGEN_DEFAULT_COPYABLE(PassMask)
  OXYGEN_DEFAULT_MOVABLE(PassMask)

  ~PassMask() noexcept = default;

  bool IsEmpty() const
  {
    return get() == nostd::to_underlying(PassMaskBit::kNone);
  }

  bool IsSet(PassMaskBit flag) const
  {
    return (get() & nostd::to_underlying(flag)) == nostd::to_underlying(flag);
  }

  void Set(PassMaskBit flag)
  {
    auto new_value = get() | nostd::to_underlying(flag);
    *this = PassMask { static_cast<PassMaskBit>(new_value) };
  }

  void Unset(PassMaskBit flag)
  {
    auto new_value = get() & ~nostd::to_underlying(flag);
    *this = PassMask { static_cast<PassMaskBit>(new_value) };
  }

  void Toggle(PassMaskBit flag)
  {
    auto new_value = get() ^ nostd::to_underlying(flag);
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
    { PassMaskBit::kDoubleSided, "DoubleSided" },
    { PassMaskBit::kOpaque, "Opaque" },
    { PassMaskBit::kMasked, "Masked" },
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
