//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {

struct CommandContext;

class Command {
public:
  Command() = default;

  OXYGEN_DEFAULT_COPYABLE(Command)
  OXYGEN_DEFAULT_MOVABLE(Command)

  virtual ~Command() = default;

  OXGN_HDLS_API auto Execute(CommandContext& ctx) -> void;

  [[nodiscard]] virtual auto GetName() const noexcept -> const char* = 0;

  virtual auto Serialize(std::ostream& /*os*/) const -> void { }

protected:
  virtual auto DoExecute(CommandContext& ctx) -> void = 0;
};

// A simple no-op command useful for testing and sequencing.
class NoopCommand final : public Command {
public:
  [[nodiscard]] auto GetName() const noexcept -> const char* override
  {
    return "NoopCommand";
  }
  auto Serialize(std::ostream& /*os*/) const -> void override { }

protected:
  auto DoExecute(CommandContext& /*ctx*/) -> void override { /* nothing */ }
};

} // namespace oxygen::graphics::headless
