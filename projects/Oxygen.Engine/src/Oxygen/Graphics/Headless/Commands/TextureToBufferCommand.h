//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Headless/Command.h>

namespace oxygen::graphics::headless {

class TextureToBufferCommand : public Command {
public:
  TextureToBufferCommand(graphics::Buffer* dst, const graphics::Texture* src,
    TextureBufferCopyRegion region);

  [[nodiscard]] auto GetName() const noexcept -> const char* override
  {
    return "TextureToBufferCommand";
  }

protected:
  auto DoExecute(CommandContext& ctx) -> void override;

private:
  Buffer* dst_;
  const graphics::Texture* src_;
  TextureBufferCopyRegion region_;
};

} // namespace oxygen::graphics::headless
