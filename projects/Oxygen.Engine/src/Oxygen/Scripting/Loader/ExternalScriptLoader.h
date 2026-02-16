//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Scripting/Loader/IScriptLoader.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

class ExternalScriptLoader final : public IScriptLoader {
public:
  OXGN_SCRP_API explicit ExternalScriptLoader(
    std::shared_ptr<const PathFinderConfig> config,
    std::filesystem::path working_directory);
  OXGN_SCRP_API ~ExternalScriptLoader() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ExternalScriptLoader)
  OXYGEN_MAKE_NON_MOVABLE(ExternalScriptLoader)

  OXGN_SCRP_NDAPI auto LoadScript(std::string_view script_id) const
    -> ScriptLoadResult override;

private:
  PathFinder path_finder_;
};

} // namespace oxygen::scripting
