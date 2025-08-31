//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Headless/Command.h>

namespace oxygen::graphics::headless {

class BufferToTextureCommand : public Command {
public:
  BufferToTextureCommand(const graphics::Buffer* src,
    const TextureUploadRegion& region, Texture* dst);

  [[nodiscard]] auto GetName() const noexcept -> const char* override
  {
    return "BufferToTextureCommand";
  }

protected:
  auto DoExecute(CommandContext& ctx) -> void override;

private:
  const graphics::Buffer* src_;
  TextureUploadRegion region_;
  Texture* dst_;
};

} // namespace oxygen::graphics::headless
