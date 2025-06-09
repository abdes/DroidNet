//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/SceneFlags.h>

using oxygen::scene::SceneFlag;

/*!
 Processes a dirty flag by:
 1. Storing current effective value as previous value.
 2. Moving pending value to effective value.
 3. Clearing dirty flag.

 @note Called during the scene update cycle.

 @return true if the flag was successfully processed, false if it was not dirty
 or applying the effective value failed.
*/
auto SceneFlag::ProcessDirty() noexcept -> bool
{
  DCHECK_F(IsDirty(), "expecting flag to be dirty");
  if (!IsDirty()) {
    return false;
  }
  DLOG_F(2, "flag bits: {}", nostd::to_string(*this));
  SetPreviousValueBit(GetEffectiveValueBit());
  SetEffectiveValueBit(GetPendingValueBit());
  SetDirtyBit(false);
  return true;
}

/*!
 This method should only be called for flags that are marked as inherited. It
 updates the pending value from the parent's effective value and marks the flag
 as dirty if the value changes.

 @note This method is typically called during the scene update cycle.
*/
auto SceneFlag::UpdateValueFromParent(const bool value) noexcept -> SceneFlag&
{
  DCHECK_F(IsInherited(), "expecting flag to be inherited");
  if (!IsInherited()) {
    return *this; // No inheritance, nothing to do
  }

  // Do not trigger a value change if the value does not change
  if (GetPendingValueBit() == value) {
    return *this;
  }

  // Resetting the pending value to be the same as the effective value,
  // means reverting a pending change.
  if (GetEffectiveValueBit() == value) {
    SetPendingValueBit(value);
    SetDirtyBit(false); // No change, no need to mark dirty
    return *this;
  }

  SetPendingValueBit(value);
  SetDirtyBit(true);
  return *this;
}

auto constexpr oxygen::scene::to_string(const SceneFlag value) noexcept
  -> std::string
{
  // The string format is "SF{EV:b,I:b,D:b,PV:b}" where b is '0' or '1'.
  // Example: "SF{EV:1,I:1,D:1,PV:1}"
  constexpr auto size
    = sizeof("SF{EV:1,I:1,D:1,PV:1}") - 1; // -1 for null terminator
  std::array<char, size> buffer;
  char* current = buffer.data();

  // Helper lambda to append a string literal to the buffer.
  // This lambda is constexpr.
  auto append_literal = [&](const char* lit_val) constexpr {
    while (*lit_val) {
      *current++ = *lit_val++;
    }
  };

  // Helper lambda to append a boolean value as '1' or '0' to the buffer.
  // This lambda is constexpr.
  auto append_bool_as_char
    = [&](const bool b_val) constexpr { *current++ = b_val ? '1' : '0'; };

  append_literal("SF{EV:");
  append_bool_as_char(value.GetEffectiveValueBit());
  append_literal(",I:");
  append_bool_as_char(value.GetInheritedBit());
  append_literal(",D:");
  append_bool_as_char(value.GetDirtyBit());
  append_literal(",PV:");
  append_bool_as_char(value.GetPreviousValueBit());
  append_literal("}");

  // Construct std::string from the buffer. In C++20, std::string(const
  // CharT*, SizeType) is constexpr. The noexcept depends on whether this
  // construction can throw (e.g., if SSO is too small and allocation fails).
  // Given the fixed small size, it's likely to fit in SSO for many
  // std::string implementations.
  return { buffer.data(), buffer.size() };
}
