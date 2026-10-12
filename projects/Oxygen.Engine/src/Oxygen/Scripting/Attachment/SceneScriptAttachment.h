//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::data {
class SceneAsset;
} // namespace oxygen::data

namespace oxygen::scripting {

class IScriptCompilationService;
class IScriptSourceResolver;

//! Where one attached script slot stands when Attach() returns.
struct SceneScriptSlotOutcome {
  enum class State : uint8_t {
    //! Embedded bytecode: the slot runs from the next frame.
    kReady,
    //! Source submitted to the compilation service, which marks the slot
    //! ready or failed when it finishes.
    kCompiling,
    //! The slot is in CompilationFailed; `diagnostics` says why.
    kFailed,
  };

  uint32_t node_index { 0 };
  //! Position of the slot on its node.
  uint32_t slot_index { 0 };
  data::AssetKey script;
  State state { State::kFailed };
  //! Rejected overrides (the slot still runs) and the failure, if any.
  std::vector<ScriptDiagnostic> diagnostics;
};

struct SceneScriptAttachmentResult {
  std::vector<SceneScriptSlotOutcome> slots;
};

//! Receives the diagnostics of a slot whose compilation fails after
//! `Attach()` returned. Called on the thread that completes compilations.
using LateScriptDiagnosticHandler
  = std::function<void(const ScriptDiagnostic&)>;

//! Attaches the scripts a scene asset records to the nodes created for it.
/*!
 For each scripting component record, in record order, every slot is added
 to its node with its execution order. Overrides are checked against the
 script's declarations: an override of an undeclared parameter, of another
 type or asset kind, or naming a node the scene does not have is rejected
 with a diagnostic and the declared default stays. Node parameters are bound
 to the nodes created for their indexes. The executable comes from embedded
 bytecode at once, or from source through the compilation service. A slot
 whose script asset is missing, whose source cannot be resolved or whose
 compilation fails is left in CompilationFailed with its diagnostic; the
 rest of the scene runs.
*/
class SceneScriptAttachment final {
public:
  //! `on_late_diagnostic` may be empty; it outlives the attachment in the
  //! pending compilations.
  OXGN_SCRP_API SceneScriptAttachment(content::IAssetLoader& loader,
    const IScriptSourceResolver& resolver,
    observer_ptr<IScriptCompilationService> compiler,
    LateScriptDiagnosticHandler on_late_diagnostic = {});
  OXGN_SCRP_API ~SceneScriptAttachment() = default;

  OXYGEN_MAKE_NON_COPYABLE(SceneScriptAttachment)
  OXYGEN_MAKE_NON_MOVABLE(SceneScriptAttachment)

  //! Attach every scripting component of `asset`; `nodes[i]` is the node
  //! created for scene node index i.
  OXGN_SCRP_NDAPI auto Attach(const data::SceneAsset& asset,
    std::span<const scene::SceneNode> nodes) const
    -> SceneScriptAttachmentResult;

private:
  observer_ptr<content::IAssetLoader> loader_;
  observer_ptr<const IScriptSourceResolver> resolver_;
  observer_ptr<IScriptCompilationService> compiler_;
  LateScriptDiagnosticHandler on_late_diagnostic_;
};

} // namespace oxygen::scripting
