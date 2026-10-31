//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <system_error>

#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/testing/Materialize.h>

namespace oxygen::cooker::testing {

auto Materialize(const std::map<std::string, std::vector<std::byte>>& files,
  const std::filesystem::path& folder) -> void
{
  for (const auto& [relpath, bytes] : files) {
    Materialize(bytes, folder / relpath);
  }
}

auto Materialize(const std::span<const std::byte> bytes,
  const std::filesystem::path& path) -> void
{
  const auto published = content::cooked::PublishWholeFile(
    content::cooked::RealCookerStorage(), path, bytes);
  if (!published) {
    throw std::system_error(
      published.error(), "materialize '" + path.string() + "'");
  }
}

} // namespace oxygen::cooker::testing
