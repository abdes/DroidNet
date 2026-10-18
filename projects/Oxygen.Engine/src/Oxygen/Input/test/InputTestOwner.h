//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Input/CodeInput.h>

namespace oxygen::input::testing {

//! The owner of the actions and contexts the Input tests create in code (see
//! MakeCodeAction).
inline constexpr std::string_view kTestInputOwner = "Test/Input";

} // namespace oxygen::input::testing
