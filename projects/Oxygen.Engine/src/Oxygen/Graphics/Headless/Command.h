//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
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

// Small helper command that wraps a callable.
class LambdaCommand : public Command {
public:
  explicit LambdaCommand(std::function<void(CommandContext&)> f)
    : fn_(std::move(f))
  {
  }
  void Execute(CommandContext& ctx) override { fn_(ctx); }

private:
  std::function<void(CommandContext&)> fn_;
};

} // namespace oxygen::graphics::headless
