//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::console {
class Console;
}

namespace oxygen::examples::internal {

auto ApplyDemoShellConsoleDefaults(console::Console& console) -> void;

} // namespace oxygen::examples::internal
