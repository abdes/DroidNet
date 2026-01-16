//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Clap/Command.h>

namespace oxygen::content::import::tool {

class ImportCommand {
public:
  virtual ~ImportCommand() = default;

  [[nodiscard]] virtual auto Name() const -> std::string_view = 0;
  [[nodiscard]] virtual auto BuildCommand() -> std::shared_ptr<clap::Command>
    = 0;
  [[nodiscard]] virtual auto Run() -> int = 0;
};

} // namespace oxygen::content::import::tool
