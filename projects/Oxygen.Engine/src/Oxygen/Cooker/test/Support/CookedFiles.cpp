//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::cooker::test {

namespace {

  //! The files the index of `cooked_root` lists whose layout path is
  //! `layout_relpath`; none when the root has no readable index.
  auto ListedFiles(const std::filesystem::path& cooked_root,
    const std::string_view layout_relpath,
    const content::cooked::CookerStorageReader& storage)
    -> std::vector<std::filesystem::path>
  {
    auto bytes = content::cooked::ReadWholeFile(storage,
      cooked_root / data::loose_cooked::kIndexFileName,
      content::cooked::kMaxMetadataBytes);
    if (!bytes) {
      return {};
    }
    auto found = std::vector<std::filesystem::path> {};
    try {
      const auto index
        = content::lc::LooseCookedIndex::LoadFromBytes(std::move(*bytes));
      for (const auto& file : index.GetFileInventory()) {
        if (LayoutPathOf(file.relative_path) == layout_relpath) {
          found.push_back(cooked_root / file.relative_path);
        }
      }
    } catch (const std::exception&) {
      return {};
    }
    return found;
  }

  //! Every file below `cooked_root` whose layout path is `layout_relpath`:
  //! the ones its index lists, else every one stored there.
  auto CookedFiles(const std::filesystem::path& cooked_root,
    const std::string_view layout_relpath,
    const content::cooked::CookerStorageReader& storage)
    -> std::vector<std::filesystem::path>
  {
    if (auto listed = ListedFiles(cooked_root, layout_relpath, storage);
      !listed.empty()) {
      return listed;
    }
    const auto entries = storage.List(cooked_root);
    if (!entries) {
      return {};
    }
    auto found = std::vector<std::filesystem::path> {};
    for (const auto& entry : *entries) {
      if (entry.kind == content::lc::RootEntryKind::kFile
        && LayoutPathOf(entry.relative_path) == layout_relpath) {
        found.push_back(cooked_root / entry.relative_path);
      }
    }
    return found;
  }

} // namespace

auto LayoutPathOf(const std::string_view relpath) -> std::string
{
  if (!data::loose_cooked::IsContentRelPath(relpath)) {
    return std::string(relpath);
  }
  // `<stem>.<digest>.<extension>` or `<stem>.<digest>`: the digest is the
  // last dotted segment of the name, or the one before the extension.
  constexpr auto kDigestSegment = std::string_view::size_type { 17 };
  const auto name_start
    = relpath.rfind('/') == std::string_view::npos ? 0 : relpath.rfind('/') + 1;
  const auto last_dot = relpath.rfind('.');
  if (last_dot != std::string_view::npos
    && last_dot >= name_start + kDigestSegment
    && data::loose_cooked::IsContentRelPath(relpath.substr(0, last_dot))) {
    const auto stem = relpath.substr(0, last_dot - kDigestSegment);
    return std::string(stem) + std::string(relpath.substr(last_dot));
  }
  return std::string(relpath.substr(0, relpath.size() - kDigestSegment));
}

auto CookedFile(const std::filesystem::path& cooked_root,
  const std::string_view layout_relpath,
  const content::cooked::CookerStorageReader& storage) -> std::filesystem::path
{
  const auto found = CookedFiles(cooked_root, layout_relpath, storage);
  if (found.size() != 1U) {
    throw std::runtime_error(std::to_string(found.size())
      + " cooked files have the layout path '" + std::string(layout_relpath)
      + "' below '" + cooked_root.generic_string() + "'");
  }
  return found.front();
}

auto HasCookedFile(const std::filesystem::path& cooked_root,
  const std::string_view layout_relpath,
  const content::cooked::CookerStorageReader& storage) -> bool
{
  return !CookedFiles(cooked_root, layout_relpath, storage).empty();
}

auto FindPhysicsResource(const content::cooked::CookedContent& content,
  const data::AssetKey& key) -> std::optional<CookedPhysicsResource>
{
  const auto& table
    = content.ResourceTable(content::cooked::TableKind::kPhysics);
  // Row zero is the reserved empty row.
  for (auto index = uint32_t { 1U }; index < table.RowCount(); ++index) {
    const auto row = ResourceIndexT { index };
    const auto desc = table.Row<data::pak::physics::PhysicsResourceDesc>(row);
    if (desc.resource_asset_key == key) {
      return CookedPhysicsResource {
        .format = desc.format,
        .payload = table.Payload(row),
      };
    }
  }
  return std::nullopt;
}

} // namespace oxygen::cooker::test
