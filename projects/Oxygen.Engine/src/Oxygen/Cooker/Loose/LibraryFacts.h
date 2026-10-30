//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakCatalog.h>

namespace oxygen::content::lc {

//! What a path names.
enum class PathKind : uint8_t {
  kNone = 0,
  kFile,
  kFolder,
};

//! A range of bytes in a file.
struct ByteRange final {
  uint64_t offset = 0;
  uint64_t size = 0;
};

//! The files `ReadLibraryFacts` reads: the real file system, or bytes held in
//! memory by a test.
class ILibraryFiles {
public:
  ILibraryFiles() = default;
  virtual ~ILibraryFiles() = default;
  OXYGEN_MAKE_NON_COPYABLE(ILibraryFiles)
  OXYGEN_MAKE_NON_MOVABLE(ILibraryFiles)

  //! What `path` names.
  [[nodiscard]] virtual auto KindOf(const std::filesystem::path& path) const
    -> PathKind = 0;

  //! The size of the file `path`, or nothing when it cannot be read.
  [[nodiscard]] virtual auto SizeOf(const std::filesystem::path& path) const
    -> std::optional<uint64_t> = 0;

  //! The bytes `range` of the file `path`, or nothing when they cannot be
  //! read.
  [[nodiscard]] virtual auto Read(const std::filesystem::path& path,
    ByteRange range) const -> std::optional<std::vector<std::byte>> = 0;
};

//! Whether a library is a loose cooked root or a pak.
enum class LibraryKind : uint8_t {
  kLoose = 0,
  kPak,
};

//! What a library states about itself, read without loading its assets.
struct LibraryFacts final {
  //! A folder is a loose cooked root, a file a pak.
  LibraryKind kind = LibraryKind::kLoose;
  //! Why it cannot be read; absent when it can. The other facts are empty
  //! then.
  std::optional<data::CookedOpenError> unreadable;
  //! The mount names that start its virtual paths, such as `Props`, sorted:
  //! a loose root's from its index, a pak's the one library its catalog
  //! names.
  std::vector<std::string> roots;
  //! The virtual paths of its scripts whose source is external, sorted. A pak
  //! has none: PakTool seals scripts.
  std::vector<std::string> external_scripts;
  //! The assets it provides, with their types; a pak's after its deletions.
  std::map<data::AssetKey, data::AssetType> assets;
  //! The keys of the textures it provides (`TextureKeyFromVirtualPath`).
  std::set<data::AssetKey> textures;
  //! The lower assets a pak deletes, which nothing below it then provides.
  std::set<data::AssetKey> deleted;
  //! What it requires of other libraries. A loose root states none.
  std::vector<data::PakCatalogDependency> requirements;
};

/*!
 Reads the facts of the library `path` names: the `container.index.bin` of a
 folder, or the header, footer, browse index and catalog of a pak. No asset is
 loaded; a loose root's script descriptors are read only for their external
 source.

 A folder without an index, or a file that is not a pak, is
 `CookedOpenFailure::kWrongFormat`; a file that cannot be read is
 `kCorrupt`.
*/
OXGN_COOK_NDAPI auto ReadLibraryFacts(const ILibraryFiles& files,
  const std::filesystem::path& path) -> LibraryFacts;

} // namespace oxygen::content::lc
