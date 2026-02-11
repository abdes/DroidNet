//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>


#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/NoStd.h>

namespace oxygen::engine {

//! Render pass classification flags for DrawMetadata records.
/*!
 Each DrawMetadata carries a bit mask describing which high-level rendering
 bucket(s) it belongs to. Current taxonomy is conservative; future bits are
 reserved and documented for design continuity.

 Active bits:
  - kDoubleSided  : Disable backface culling for this draw.
  - kOpaque       : Depth-writing opaque surfaces.
  - kMasked       : Depth-writing alpha-tested (cutout) surfaces.
  - kTransparent  : Alpha blended surfaces (depth read, no depth write).

 Reserved (not yet produced):
  - kAdditive     : Additive/emissive order-dependent.
  - kTransmission : Refraction / glass / subsurface.
  - kDecal        : Projected decals.
  - kUi           : Overlay / UI.
 */
enum class PassMaskBit : uint32_t { // NOLINT(*-enum-size)
  kNone = 0,
  kDoubleSided = OXYGEN_FLAG(1),
  kOpaque = OXYGEN_FLAG(2),
  kMasked = OXYGEN_FLAG(3),
  kTransparent = OXYGEN_FLAG(4),
  kAdditive = OXYGEN_FLAG(5),
  kTransmission = OXYGEN_FLAG(6),
  kDecal = OXYGEN_FLAG(7),
  kUi = OXYGEN_FLAG(8),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(PassMaskBit)

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
  PassMask()
    : PassMask(PassMaskBit::kNone)
  {
  }

  explicit PassMask(PassMaskBit value)
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

  [[nodiscard]] auto IsEmpty() const -> bool
  {
    return get() == nostd::to_underlying(PassMaskBit::kNone);
  }

  [[nodiscard]] auto IsSet(PassMaskBit flag) const -> bool
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
  static constexpr std::array<Entry, 8> kTable = { {
    { .flag = PassMaskBit::kDoubleSided, .name = "DoubleSided" },
    { .flag = PassMaskBit::kOpaque, .name = "Opaque" },
    { .flag = PassMaskBit::kMasked, .name = "Masked" },
    { .flag = PassMaskBit::kTransparent, .name = "Transparent" },
    { .flag = PassMaskBit::kAdditive, .name = "Additive" },
    { .flag = PassMaskBit::kTransmission, .name = "Transmission" },
    { .flag = PassMaskBit::kDecal, .name = "Decal" },
    { .flag = PassMaskBit::kUi, .name = "UI" },
  } };
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
