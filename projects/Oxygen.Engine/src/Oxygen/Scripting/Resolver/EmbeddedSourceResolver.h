//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scripting/IScriptSourceResolver.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

class EmbeddedSourceResolver {
public:
  OXGN_SCRP_NDAPI static auto Resolve(
    const IScriptSourceResolver::ResolveRequest& request)
    -> IScriptSourceResolver::ResolveResult;
};

} // namespace oxygen::scripting
