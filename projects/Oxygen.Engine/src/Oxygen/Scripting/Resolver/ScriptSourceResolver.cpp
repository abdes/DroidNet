//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/Logging.h>
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
  const auto& asset = request.asset.get();
  const auto ext_path = asset.TryGetExternalSourcePath();

  if (asset.AllowsExternalSource() && ext_path) {
    auto external_result = external_resolver_.Resolve(request);
    if (external_result.ok || !external_result.error_message.empty()) {
      return external_result;
    }
  }

  auto result = EmbeddedSourceResolver::Resolve(request);
  if (asset.AllowsExternalSource() && !result.ok) {
    DLOG_F(1,
      "ScriptSourceResolver: External resolution skipped or failed, "
      "falling back to embedded. Error: {}",
      result.error_message);
  }
  return result;
}

} // namespace oxygen::scripting
