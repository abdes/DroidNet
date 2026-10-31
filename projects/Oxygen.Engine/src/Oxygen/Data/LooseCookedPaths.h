//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Data/api_export.h>

namespace oxygen::data::loose_cooked {

//! Refuses `relpath` unless a loose cooked index may list it.
/*!
 A listed path is relative to the cooked root: not empty, `/`-separated, with
 no `:`, no leading or trailing `/`, no `//`, and no `.` or `..` segment. It
 names none of the root's reserved files (the index and the generation
 lease). Writers check what they list, and readers what they load, with this
 one rule.

 @throw std::runtime_error naming the rule `relpath` breaks.
*/
OXGN_DATA_API auto RequireIndexRelPath(std::string_view relpath) -> void;

} // namespace oxygen::data::loose_cooked
