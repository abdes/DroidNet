//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/IScriptSourceResolver.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

class ExternalFileResolver {
public:
  OXGN_SCRP_API explicit ExternalFileResolver(PathFinder path_finder);
  OXGN_SCRP_API ~ExternalFileResolver() = default;

  OXYGEN_MAKE_NON_COPYABLE(ExternalFileResolver)
  OXYGEN_MAKE_NON_MOVABLE(ExternalFileResolver)

  OXGN_SCRP_NDAPI auto Resolve(
    const IScriptSourceResolver::ResolveRequest& request) const
    -> IScriptSourceResolver::ResolveResult;

  //! The file the asset's external source path resolves to: the first script
  //! source root that contains it. Nothing when the asset has no valid
  //! external source path or no root contains it.
  OXGN_SCRP_NDAPI auto ResolvePath(const data::ScriptAsset& asset) const
    -> std::optional<std::filesystem::path>;

private:
  PathFinder path_finder_;
};

} // namespace oxygen::scripting
