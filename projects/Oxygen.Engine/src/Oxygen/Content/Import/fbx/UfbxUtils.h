//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Content/Import/fbx/ufbx.h>

namespace oxygen::content::import::fbx {

//! Converts ufbx_string to std::string_view.
[[nodiscard]] inline auto ToStringView(const ufbx_string& s) -> std::string_view
{
  return std::string_view(s.data, s.length);
}

} // namespace oxygen::content::import::fbx
