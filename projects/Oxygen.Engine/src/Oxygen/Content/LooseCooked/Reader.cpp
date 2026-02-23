//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/LooseCooked/Reader.h>

namespace oxygen::content::lc {

auto Reader::LoadFromRoot(const std::filesystem::path& cooked_root) -> Index
{
  return LoadFromFile(cooked_root / "container.index.bin");
}

auto Reader::LoadFromFile(const std::filesystem::path& index_path) -> Index
{
  auto index = std::make_unique<internal::LooseCookedIndexImpl>(
    internal::LooseCookedIndexImpl::LoadFromFile(index_path));
  return Index(std::move(index));
}

} // namespace oxygen::content::lc
