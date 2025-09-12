//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Headless/Command.h>

namespace oxygen::graphics::headless {

class ClearDepthStencilCommand : public Command {
public:
  ClearDepthStencilCommand(const Texture* texture, const NativeView& dsv,
    ClearFlags flags, float depth, uint8_t stencil);

  [[nodiscard]] auto GetName() const noexcept -> const char* override
  {
    return "ClearDepthStencilCommand";
  }
  auto Serialize(std::ostream& os) const -> void override;

protected:
  auto DoExecute(CommandContext& ctx) -> void override;

private:
  const Texture* texture_ = nullptr;
  NativeView dsv_ {};
  ClearFlags flags_ {};
  float depth_ = 1.0f;
  uint8_t stencil_ = 0;
};

} // namespace oxygen::graphics::headless
