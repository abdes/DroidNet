//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::inspection {

//! `buffers`: dumps `buffers.table`.
[[nodiscard]] auto MakeBuffersCommand()
  -> std::unique_ptr<toolkit::ToolCommand>;

//! `textures`: dumps `textures.table`.
[[nodiscard]] auto MakeTexturesCommand()
  -> std::unique_ptr<toolkit::ToolCommand>;

//! `physics-table`: dumps `physics.table`.
[[nodiscard]] auto MakePhysicsTableCommand()
  -> std::unique_ptr<toolkit::ToolCommand>;

} // namespace oxygen::content::inspection
