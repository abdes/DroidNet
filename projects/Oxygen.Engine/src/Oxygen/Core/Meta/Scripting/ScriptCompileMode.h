//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace oxygen::core::meta::scripting {

enum class ScriptCompileMode : uint8_t {
// NOLINTNEXTLINE(*-macro-*)
#define OXGN_SCRIPT_COMPILE_MODE(name, value) name = value,
#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.inc>
#undef OXGN_SCRIPT_COMPILE_MODE
};

static_assert(
  sizeof(std::underlying_type_t<ScriptCompileMode>) <= sizeof(uint8_t),
  "ScriptCompileMode enum fit in `uint8_t`");

[[nodiscard]] constexpr auto to_string(const ScriptCompileMode mode) noexcept
  -> std::string_view
{
  switch (mode) {
    // clang-format off
  case ScriptCompileMode::kDebug: return "debug";
  case ScriptCompileMode::kOptimized: return "optimized";
  default: return "__Unknown__";
    // clang-format on
  }
}

} // namespace oxygen::core::meta::scripting
