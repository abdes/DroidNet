//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Headless/Command.h>

namespace oxygen::graphics::headless {

class ResourceBarrierCommand final : public Command {
public:
  explicit ResourceBarrierCommand(std::vector<detail::Barrier> barriers)
    : barriers_(std::move(barriers))
  {
  }

  auto Serialize(std::ostream& os) const -> void override;
  [[nodiscard]] auto GetName() const noexcept -> const char* override
  {
    return "ResourceBarrierCommand";
  }

protected:
  auto DoExecute(CommandContext& ctx) -> void override;

private:
  std::vector<detail::Barrier> barriers_;
  // observed_states is provided via CommandContext at execute-time.
};

} // namespace oxygen::graphics::headless
