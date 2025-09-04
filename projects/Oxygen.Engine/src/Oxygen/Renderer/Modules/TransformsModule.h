//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::engine {

//! Propagates scene hierarchy transforms during kTransformPropagation phase.
/*!
 Performs world transform propagation (scene.Update()) before snapshot
 publication so that downstream parallel tasks (ScenePrep, culling, animation
 skinning consumers) observe stable world matrices.
*/
class TransformsModule final : public EngineModule {
  OXYGEN_TYPED(TransformsModule)

public:
  explicit TransformsModule() { }
  ~TransformsModule() override = default;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "TransformsModule";
  }
  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    // This module must run last, after all modules that may modify transforms.
    return kModulePriorityLowest;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask override
  {
    return MakeModuleMask<core::PhaseId::kTransformPropagation>();
  }

  OXGN_RNDR_NDAPI auto OnTransformPropagation(FrameContext& context)
    -> co::Co<> override;
};

} // namespace oxygen::engine
