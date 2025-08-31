//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Headless/Commands/ClearDepthStencilCommand.h>
#include <Oxygen/Graphics/Headless/Texture.h>

namespace oxygen::graphics::headless {

ClearDepthStencilCommand::ClearDepthStencilCommand(
  const graphics::Texture* texture, const NativeObject& dsv, ClearFlags flags,
  float depth, uint8_t stencil)
  : texture_(texture)
  , dsv_(dsv)
  , flags_(flags)
  , depth_(depth)
  , stencil_(stencil)
{
}

auto ClearDepthStencilCommand::DoExecute(CommandContext& /*ctx*/) -> void
{
  if (!texture_) {
    LOG_F(WARNING, "ClearDepthStencilCommand: no texture provided");
    return;
  }
  auto tex = static_cast<const Texture*>(texture_);
  if (!tex) {
    LOG_F(WARNING, "ClearDepthStencilCommand: texture is not headless-backed");
    return;
  }

  LOG_F(INFO,
    "Headless: simulated depth/stencil clear depth={} stencil={} flags={}",
    depth_, static_cast<int>(stencil_), static_cast<int>(flags_));
}

auto ClearDepthStencilCommand::Serialize(std::ostream& os) const -> void
{
  os << "ClearDepthStencilCommand\n";
}

} // namespace oxygen::graphics::headless
