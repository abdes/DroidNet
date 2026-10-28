//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::inspection {

//! `input-actions`: dumps the input action descriptors.
[[nodiscard]] auto MakeInputActionsCommand()
  -> std::unique_ptr<toolkit::ToolCommand>;

//! `input-mappings`: dumps the input mapping context descriptors.
[[nodiscard]] auto MakeInputMappingsCommand()
  -> std::unique_ptr<toolkit::ToolCommand>;

//! `physics`: dumps the physics material, collision shape and scene
//! descriptors.
[[nodiscard]] auto MakePhysicsAssetsCommand()
  -> std::unique_ptr<toolkit::ToolCommand>;

//! `script-slots`: dumps scene-local script slots, grouped by scene and node.
[[nodiscard]] auto MakeScriptSlotsCommand()
  -> std::unique_ptr<toolkit::ToolCommand>;

//! `script-params`: dumps scene-local script slots and their parameter values.
[[nodiscard]] auto MakeScriptParametersCommand()
  -> std::unique_ptr<toolkit::ToolCommand>;

} // namespace oxygen::content::inspection
