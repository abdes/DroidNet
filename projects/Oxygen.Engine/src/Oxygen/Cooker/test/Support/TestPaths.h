//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::cooker::test {

//! Root of `src/Oxygen/Cooker` in the source tree.
[[nodiscard]] auto CookerSourceDir() -> const std::filesystem::path&;

//! `Cooker/test/Assets`. Throws `std::runtime_error` if it is missing.
[[nodiscard]] auto AssetsDir() -> const std::filesystem::path&;

//! `Cooker/test/Assets/Models/<name>`. Throws if the file is missing.
[[nodiscard]] auto ModelPath(std::string_view name) -> std::filesystem::path;

//! The identity these tests give a model: its file name at the default mount
//! root (`/.cooked/Tabuleiro.glb`), under which its products are named.
[[nodiscard]] auto ModelVirtualPath(const std::filesystem::path& source)
  -> std::string;

//! The recorded source these tests give a request: the file's name in the
//! `Content` mount (`/Content/Tabuleiro.glb`). A request that cooks something
//! needs one, or the import fails with `import.source_outside_mount`.
[[nodiscard]] auto SourceMountPath(const std::filesystem::path& source)
  -> std::string;

//! The mount path of `file` in a `Content` mount on `root`
//! (`/Content/<file relative to root>`): how a descriptor names a file without
//! an absolute path.
[[nodiscard]] auto MountPathIn(const std::filesystem::path& root,
  const std::filesystem::path& file) -> std::string;

//! The mounts of a test project: one source mount, `Content`, on `root`, which
//! also is the project root. Jobs that resolve the files their descriptors
//! reference (`ImportRequest::source_mounts`) are given this.
[[nodiscard]] auto ContentMountTable(const std::filesystem::path& root)
  -> std::shared_ptr<const content::import::MountTable>;

//! The mounts of a project with one source mount, `name`, on `root`, which
//! also is the project root.
[[nodiscard]] auto SourceMountTable(
  std::string_view name, const std::filesystem::path& root)
  -> std::shared_ptr<const content::import::MountTable>;

//! The mounts of a test project with source mounts `Content` and `Other` on
//! the `Content` and `Other` folders of `root`: two mounts a reference can
//! leave one for the other.
[[nodiscard]] auto ContentAndOtherMountTable(const std::filesystem::path& root)
  -> std::shared_ptr<const content::import::MountTable>;

//! A file under `Cooker/`, for example
//! `"Import/Schemas/oxygen.input.schema.json"`. Throws if it is missing.
[[nodiscard]] auto SchemaPath(std::string_view relative_to_cooker)
  -> std::filesystem::path;

} // namespace oxygen::cooker::test
