//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

namespace oxygen {

class Engine;
using EngineWeakPtr = std::weak_ptr<Engine>;

namespace core {

  class Module;
  class InputHandler;
  class System;

} // namespace core

} // namespace oxygen
