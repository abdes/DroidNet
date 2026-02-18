//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <type_traits> // IWYU pragma: keep

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen::input {

enum class ActionState : uint8_t {
// NOLINTNEXTLINE(*-macro-*)
#define OXNPUT_ACTION_STATE(name, value)                                       \
  name = ((value) == 0 ? 0 : OXYGEN_FLAG(value)),
#include <Oxygen/Core/Meta/Input/ActionState.inc>
#undef OXNPUT_ACTION_STATE
};

OXYGEN_DEFINE_FLAGS_OPERATORS(ActionState)

OXGN_NPUT_NDAPI auto to_string(ActionState value) -> std::string;

} // namespace oxygen::input
