//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <ostream>
#include <utility>

namespace oxygen::graphics::headless {

struct CommandContext { };

class Command {
public:
  virtual ~Command() = default;
  virtual void Execute(CommandContext& ctx) = 0;
  // Optional serialization hook. Default is no-op.
  virtual void Serialize(std::ostream& /*os*/) const { }
};

// A simple no-op command useful for testing and sequencing.
class NoopCommand : public Command {
public:
  NoopCommand() = default;
  void Execute(CommandContext& /*ctx*/) override { }
  void Serialize(std::ostream& /*os*/) const override { }
};

} // namespace oxygen::graphics::headless
