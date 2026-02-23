//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/LooseCooked/Reader.h>
#include <Oxygen/Content/LooseCooked/Validation.h>

namespace oxygen::content::lc {

auto ValidateRoot(const std::filesystem::path& cooked_root) -> void
{
  (void)Reader::LoadFromRoot(cooked_root);
}

} // namespace oxygen::content::lc
