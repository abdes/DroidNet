//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <vector>

#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Headless/Command.h>

namespace oxygen::graphics::headless {

class ClearFramebufferCommand : public Command {
public:
  ClearFramebufferCommand(const Framebuffer* fb,
    std::optional<std::vector<std::optional<Color>>> color_clear_values,
    std::optional<float> depth_clear_value,
    std::optional<uint8_t> stencil_clear_value);

  [[nodiscard]] auto GetName() const noexcept -> const char* override
  {
    return "ClearFramebufferCommand";
  }
  auto Serialize(std::ostream& os) const -> void override;

protected:
  auto DoExecute(CommandContext& ctx) -> void override;

private:
  const Framebuffer* framebuffer_ = nullptr;
  std::optional<std::vector<std::optional<Color>>> color_clear_values_;
  std::optional<float> depth_clear_value_;
  std::optional<uint8_t> stencil_clear_value_;
};

} // namespace oxygen::graphics::headless
