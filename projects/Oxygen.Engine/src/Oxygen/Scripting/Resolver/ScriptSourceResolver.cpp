//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Scripting/Resolver/EmbeddedSourceResolver.h>
#include <Oxygen/Scripting/Resolver/ScriptSourceResolver.h>

namespace oxygen::scripting {

ScriptSourceResolver::ScriptSourceResolver(PathFinder path_finder)
  : external_resolver_(std::move(path_finder))
{
}

auto ScriptSourceResolver::Resolve(const ResolveRequest& request) const
  -> ResolveResult
{
  auto embedded_result = EmbeddedSourceResolver::Resolve(request);
  if (embedded_result.ok) {
    return embedded_result;
  }

  const auto& asset = request.asset.get();
  if (!asset.AllowsExternalSource()) {
    return embedded_result;
  }

  const auto external_result = external_resolver_.Resolve(request);
  if (external_result.ok) {
    return external_result;
  }
  return external_result;
}

} // namespace oxygen::scripting
