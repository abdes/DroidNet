//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Loose/LibraryFacts.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_scripting.h>

namespace oxygen::content::lc {

namespace {

  namespace fs = std::filesystem;
  using data::CookedOpenError;
  using data::CookedOpenFailure;

  auto Unreadable(const fs::path& path) -> CookedOpenError
  {
    return { CookedOpenFailure::kCorrupt,
      "'" + path.generic_string() + "' cannot be read" };
  }

  //! The whole file `path`.
  auto ReadAll(const ILibraryFiles& files, const fs::path& path)
    -> std::vector<std::byte>
  {
    const auto size = files.SizeOf(path);
    if (!size.has_value()) {
      throw Unreadable(path);
    }
    auto bytes = files.Read(path, { .offset = 0, .size = *size });
    if (!bytes.has_value()) {
      throw Unreadable(path);
    }
    return std::move(*bytes);
  }

  //! A packed struct read at `offset` of the file `path`.
  template <typename T>
  auto ReadStruct(const ILibraryFiles& files, const fs::path& path,
    const uint64_t offset) -> T
  {
    const auto bytes
      = files.Read(path, { .offset = offset, .size = sizeof(T) });
    if (!bytes.has_value() || bytes->size() != sizeof(T)) {
      throw Unreadable(path);
    }
    auto out = T {};
    std::memcpy(&out, bytes->data(), sizeof(T));
    return out;
  }

  auto AddRoot(std::set<std::string>& roots, const std::string_view path)
    -> void
  {
    if (const auto root = content::MountRootOf(path); !root.empty()) {
      roots.emplace(root.substr(1));
    }
  }

  //! True when the script descriptor names an external source.
  auto HasExternalSource(const ILibraryFiles& files, const fs::path& descriptor)
    -> bool
  {
    using data::pak::scripting::ScriptAssetDesc;
    return ReadStruct<ScriptAssetDesc>(files, descriptor, 0)
             .external_source_path_offset
      != 0;
  }

  auto ReadLoose(const ILibraryFiles& files, const fs::path& root)
    -> LibraryFacts
  {
    auto facts = LibraryFacts {
      .kind = LibraryKind::kLoose,
      .unreadable = std::nullopt,
      .roots = {},
      .external_scripts = {},
    };
    const auto index_path = root / import::LooseCookedLayout {}.index_file_name;
    if (files.KindOf(index_path) != PathKind::kFile) {
      throw CookedOpenError(CookedOpenFailure::kWrongFormat,
        "the folder holds no " + import::LooseCookedLayout {}.index_file_name);
    }
    const auto index
      = LooseCookedIndex::LoadFromBytes(ReadAll(files, index_path));

    auto roots = std::set<std::string> {};
    auto scripts = std::set<std::string> {};
    for (const auto& key : index.GetAllAssetKeys()) {
      const auto virtual_path = index.FindVirtualPath(key).value_or("");
      AddRoot(roots, virtual_path);
      const auto relpath = index.FindDescriptorRelPath(key);
      if (index.FindAssetType(key)
          == static_cast<uint8_t>(data::AssetType::kScript)
        && relpath.has_value() && HasExternalSource(files, root / *relpath)) {
        scripts.emplace(virtual_path);
      }
    }
    for (const auto& file : index.GetFileInventory()) {
      AddRoot(roots, file.virtual_path);
    }
    facts.roots.assign(roots.begin(), roots.end());
    facts.external_scripts.assign(scripts.begin(), scripts.end());
    return facts;
  }

  //! Checks the header, the footer and the catalog version; a pak states no
  //! root before its catalog names its library.
  auto ReadPak(const ILibraryFiles& files, const fs::path& pak) -> LibraryFacts
  {
    using data::pak::core::PakFooter;
    using data::pak::core::PakHeader;

    const auto size = files.SizeOf(pak);
    if (!size.has_value()) {
      throw Unreadable(pak);
    }
    if (*size < sizeof(PakHeader) + sizeof(PakFooter)) {
      throw CookedOpenError(
        CookedOpenFailure::kWrongFormat, "File too small to hold a pak");
    }
    PakFile::CheckHeader(ReadStruct<PakHeader>(files, pak, 0));
    const auto footer
      = ReadStruct<PakFooter>(files, pak, *size - sizeof(PakFooter));
    PakFile::CheckFooter(footer, *size);
    const auto catalog = files.Read(
      pak, { .offset = footer.catalog_offset, .size = footer.catalog_size });
    if (!catalog.has_value()) {
      throw Unreadable(pak);
    }
    if (const auto decoded = data::PakCatalog::Decode(*catalog); !decoded) {
      throw CookedOpenError(decoded.error());
    }
    return LibraryFacts {
      .kind = LibraryKind::kPak,
      .unreadable = std::nullopt,
      .roots = {},
      .external_scripts = {},
    };
  }

} // namespace

auto ReadLibraryFacts(const ILibraryFiles& files, const fs::path& path)
  -> LibraryFacts
{
  const auto kind = files.KindOf(path) == PathKind::kFolder
    ? LibraryKind::kLoose
    : LibraryKind::kPak;
  try {
    return kind == LibraryKind::kLoose ? ReadLoose(files, path)
                                       : ReadPak(files, path);
  } catch (const CookedOpenError& error) {
    return LibraryFacts {
      .kind = kind,
      .unreadable = error,
      .roots = {},
      .external_scripts = {},
    };
  }
}

} // namespace oxygen::content::lc
