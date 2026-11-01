//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

namespace oxygen::examples {

//! Opens the folder of `path` in the file explorer with `path` selected;
//! a folder that is gone opens its nearest existing parent.
auto ShowInFolder(const std::filesystem::path& path) -> void;

} // namespace oxygen::examples
