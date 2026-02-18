//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Scripting/IScriptSourceResolver.h>
#include <Oxygen/Scripting/Resolver/ExternalFileResolver.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

class ScriptSourceResolver final : public IScriptSourceResolver {
public:
  OXGN_SCRP_API explicit ScriptSourceResolver(PathFinder path_finder);
  OXGN_SCRP_API ~ScriptSourceResolver() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ScriptSourceResolver)
  OXYGEN_MAKE_NON_MOVABLE(ScriptSourceResolver)

  OXGN_SCRP_NDAPI auto Resolve(const ResolveRequest& request) const
    -> ResolveResult override;

private:
  ExternalFileResolver external_resolver_;
};

} // namespace oxygen::scripting
