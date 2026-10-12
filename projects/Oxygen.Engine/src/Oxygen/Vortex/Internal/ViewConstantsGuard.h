//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/RenderContext.h>

namespace oxygen::vortex::internal {

//! Whether a camera pass can record: it needs the view's b1 record.
/*!
 A view whose constants were not published is rejected upstream; a camera pass
 reached without them logs and records nothing rather than drawing with an
 unbound root parameter.
*/
[[nodiscard]] inline auto HasViewConstants(
  const RenderContext& ctx, const std::string_view pass) -> bool
{
  if (ctx.view_constants.IsValid()) {
    return true;
  }
  LOG_F(ERROR, "View {}: {} has no view constants and is not recorded",
    ctx.current_view.view_id.get(), pass);
  return false;
}

} // namespace oxygen::vortex::internal
