//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Resolver/ExternalFileResolver.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::data {
class ScriptAsset;
} // namespace oxygen::data

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::scripting {

class IScriptCompilationService;

//! Recompiles changed script sources into the slots that use them.
/*!
 A script asset cooked with an external source records its path relative to
 the script source roots. When that file changes, the source is analyzed and
 compiled again. On success every slot of the scene that uses the asset gets
 the new declarations (ScriptingComponent::ReplaceSlotAsset) and the new
 executable (MarkSlotReady), which ends its instance and starts a new one;
 a slot whose compilation had failed recovers. On failure the slots keep
 their last good executable and the diagnostics are reported.
*/
class ScriptSourceReload final {
public:
  using DiagnosticSink = std::function<void(const ScriptDiagnostic&)>;

  OXGN_SCRP_API ScriptSourceReload(PathFinder path_finder,
    observer_ptr<IScriptCompilationService> compiler, DiagnosticSink sink);
  OXGN_SCRP_API ~ScriptSourceReload() = default;

  OXYGEN_MAKE_NON_COPYABLE(ScriptSourceReload)
  OXYGEN_MAKE_NON_MOVABLE(ScriptSourceReload)

  //! Recompiles the scripts of `scene` whose source is `changed_file`;
  //! returns how many were submitted.
  OXGN_SCRP_API auto OnSourceChanged(const std::shared_ptr<scene::Scene>& scene,
    const std::filesystem::path& changed_file) -> std::size_t;

  //! Recompiles every script of `scene` that has an external source; returns
  //! how many were submitted.
  OXGN_SCRP_API auto ReloadAll(const std::shared_ptr<scene::Scene>& scene)
    -> std::size_t;

private:
  auto Reload(const std::shared_ptr<scene::Scene>& scene,
    const std::shared_ptr<const data::ScriptAsset>& asset) -> bool;

  ExternalFileResolver resolver_;
  observer_ptr<IScriptCompilationService> compiler_;
  DiagnosticSink sink_;
  ScriptAnalyzer analyzer_;
  //! Compilation callbacks that outlive this object do nothing.
  std::shared_ptr<int> alive_ { std::make_shared<int>(0) };
};

} // namespace oxygen::scripting
