//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

//! Why `value` cannot override parameter `key` of `script`, or nothing when
//! it can.
/*!
 An override must name a declared parameter, have the declared type and,
 for an asset, the declared asset kind. A node override must name a node the
 scene has; `node_count` is the scene's node count, or nothing to skip that
 check (a node already bound).
*/
OXGN_SCRP_NDAPI auto CheckParameterOverride(const data::ScriptAsset& script,
  std::string_view key, const data::ScriptParam& value,
  std::optional<std::size_t> node_count) -> std::optional<std::string>;

} // namespace oxygen::scripting
