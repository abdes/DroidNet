//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::engine::asyncsim {

using ViewIndex = NamedType<size_t,
  // clang-format off
  struct ViewIndexTag,
  oxygen::Arithmetic>; // clang-format on

inline [[nodiscard]] auto to_string(const ViewIndex& v) -> std::string
{
  return std::to_string(v.get());
}

} // namespace oxygen::engine::asyncsim
