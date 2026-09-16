//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

namespace oxygen::content::inspection {

//! Validates loose index/files and parses scene descriptors with native
//! loaders. Throws on invalid content, including retired scene versions and
//! flag modes.
auto ValidateRootOrThrow(const std::filesystem::path& cooked_root) -> void;

} // namespace oxygen::content::inspection
