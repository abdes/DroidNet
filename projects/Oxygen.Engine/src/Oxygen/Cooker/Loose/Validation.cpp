//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Loose/Validation.h>

namespace oxygen::content::lc {

auto ValidateRoot(const std::filesystem::path& cooked_root) -> void
{
  (void)LooseCookedIndex::LoadFromRoot(cooked_root);
}

} // namespace oxygen::content::lc
