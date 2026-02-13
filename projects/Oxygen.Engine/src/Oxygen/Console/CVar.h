//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Console/api_export.h>

namespace oxygen::console {

enum class CVarType : uint8_t {
  kBool,
  kInt,
  kFloat,
  kString,
};

enum class CVarFlags : uint8_t { // NOLINT(*-enum-size)
  kNone = 0,
  kArchive = OXYGEN_FLAG(0),
  kReadOnly = OXYGEN_FLAG(1),
  kCheat = OXYGEN_FLAG(2),
  kDevOnly = OXYGEN_FLAG(3),
  kRequiresRestart = OXYGEN_FLAG(4),
  kLatched = OXYGEN_FLAG(5),
  kRenderThreadSafe = OXYGEN_FLAG(6),
  kHidden = OXYGEN_FLAG(7),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(CVarFlags)

using CVarValue = std::variant<bool, int64_t, double, std::string>;

struct CVarDefinition {
  std::string name;
  std::string help;
  CVarValue default_value;
  CVarFlags flags { CVarFlags::kNone };
  std::optional<double> min_value;
  std::optional<double> max_value;
};

struct CVarSnapshot {
  CVarDefinition definition;
  CVarValue current_value;
  std::optional<CVarValue> latched_value;
  std::optional<CVarValue> restart_value;
};

class CVarHandle : public oxygen::NamedType<uint32_t, struct CVarHandleTag,
                     oxygen::Comparable, oxygen::Hashable, oxygen::Printable> {
public:
  using Base = oxygen::NamedType<uint32_t, struct CVarHandleTag,
    oxygen::Comparable, oxygen::Hashable, oxygen::Printable>;

  constexpr CVarHandle() noexcept
    : Base { 0U }
  {
  }

  explicit constexpr CVarHandle(const uint32_t value) noexcept
    : Base { value }
  {
  }

  using Base::get;
  using Base::ref;

  OXGN_CONS_NDAPI auto IsValid() const noexcept -> bool { return get() != 0U; }
};

OXGN_CONS_NDAPI constexpr auto HasFlag(
  const CVarFlags value, const CVarFlags flag) noexcept -> bool
{
  return (value & flag) != CVarFlags::kNone;
}

OXGN_CONS_NDAPI auto to_string(CVarType value) -> const char*;
OXGN_CONS_NDAPI auto to_string(CVarFlags value) -> std::string;

} // namespace oxygen::console
