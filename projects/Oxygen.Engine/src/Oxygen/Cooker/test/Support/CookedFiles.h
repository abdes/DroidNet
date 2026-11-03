//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Cooked/CookerStorage.h>

//! Finding cooked files by the layout path a cook gives them.
/*!
 A loose root names every file but its data files and its index by its
 content (`data::loose_cooked::ContentRelPath`): `Materials/Wood.omat` is
 stored as `Materials/Wood.<digest>.omat`. A test that checks what a cook
 wrote names the layout path and finds the file the cook stored for it.
*/
namespace oxygen::cooker::test {

//! `relpath` without the digest its content name carries; a path of no
//! content name is its own layout path.
[[nodiscard]] auto LayoutPathOf(std::string_view relpath) -> std::string;

//! The one file below `cooked_root` whose layout path is `layout_relpath`,
//! found through `storage`: the one its index lists, so an earlier commit's
//! retired file is no match, or the one stored there when no index lists it.
/*!
 @throw std::runtime_error naming the layout path when no file, or more than
   one, has it.
*/
[[nodiscard]] auto CookedFile(const std::filesystem::path& cooked_root,
  std::string_view layout_relpath,
  const content::cooked::CookerStorageReader& storage
  = content::cooked::RealCookerStorage()) -> std::filesystem::path;

//! Whether a file below `cooked_root` has the layout path `layout_relpath`.
[[nodiscard]] auto HasCookedFile(const std::filesystem::path& cooked_root,
  std::string_view layout_relpath,
  const content::cooked::CookerStorageReader& storage
  = content::cooked::RealCookerStorage()) -> bool;

} // namespace oxygen::cooker::test
