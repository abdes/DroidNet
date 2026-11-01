//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace oxygen::examples::ui {

//! A folder models are imported from: a `Source` mount of the scratch
//! project, as the Mounts tab lists it.
struct SourceFolder final {
  std::string name;
  std::filesystem::path folder;
  //! Added by the panel to the scratch project's own `.mounts.json`, so the
  //! panel can remove it; the scratch project declares the others.
  bool removable { false };
};

//! The scratch project's own mounts file, where the panel adds the source
//! folders of this machine.
[[nodiscard]] auto LocalMountsFile(const std::filesystem::path& scratch_root)
  -> std::filesystem::path;

//! A mount name for `folder`: its name, with every character a mount name
//! does not allow made `_`, suffixed with a number while `taken` holds it.
[[nodiscard]] auto SourceFolderMountName(const std::filesystem::path& folder,
  std::span<const std::string> taken) -> std::string;

//! Adds the mount `name` of `folder` to the mounts file `file`, creating it
//! when missing and keeping its other content. Throws when the file is not a
//! JSON object or cannot be written.
auto AddLocalMount(const std::filesystem::path& file, std::string_view name,
  const std::filesystem::path& folder) -> void;

//! Removes the mount `name` from the mounts file `file`. Throws when the file
//! cannot be read or written.
auto RemoveLocalMount(const std::filesystem::path& file, std::string_view name)
  -> void;

} // namespace oxygen::examples::ui
