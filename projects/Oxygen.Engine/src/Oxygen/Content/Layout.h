//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

namespace oxygen::content::import {

//! Common, container-agnostic layout conventions.
/*!
 Names the resource folder's default. It does not prescribe any particular
 on-disk layout.

 @note This type is intended to be used as a base for concrete container
  layouts (eg. loose cooked).
*/
struct Layout {
  //! Default virtual/physical directory name for resource blobs.
  static constexpr std::string_view kResourcesDirName = "Resources";
};

} // namespace oxygen::content::import
