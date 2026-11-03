//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::cooked {

//! Checks that a cooked source is one the runtime loads: its stored bytes
//! match what it records of them, every descriptor is of the current version
//! and parses, its references stay inside its tables, and its scenes'
//! surface overrides name surfaces of its geometries.
/*!
 A loose root and a pak are checked the same way.

 @throw data::CookedFormatError Naming the first defect, with its code; a
   descriptor's loader may refuse it with its own typed error.
 @throw std::system_error When the storage cannot list or read the source.
*/
OXGN_COOK_API auto Validate(const CookedContent& content,
  lc::IntegrityCheck check = lc::IntegrityCheck::kFull) -> void;

} // namespace oxygen::content::cooked
