//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

struct ImGuiTestEngine;

namespace oxygen::examples::testing {

// Compiled only with OXYGEN_BUILD_UI_TESTS. Registers the file browser widget
// tests; they host their own browser and need no application state.
auto RegisterFileBrowserUiTests(ImGuiTestEngine* engine) -> void;

} // namespace oxygen::examples::testing
