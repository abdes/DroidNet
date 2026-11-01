//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

struct ImGuiTestEngine;

namespace oxygen::examples::testing {

class UiRunnerModule;

//! The content panel's scenario tests: loading a scene found by search, the
//! details of a refused pak, and going from a log row to its mount. Each
//! writes its cooked sources with the cooked content builders.
auto RegisterContentPanelUiTests(ImGuiTestEngine* engine, UiRunnerModule& app)
  -> void;

} // namespace oxygen::examples::testing
