//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content {

namespace {

  auto ValidateVirtualPathOrThrow(std::string_view virtual_path) -> void
  {
    if (virtual_path.empty()) {
      throw std::invalid_argument("Virtual path must not be empty");
    }
    if (virtual_path.find('\\') != std::string_view::npos) {
      throw std::invalid_argument("Virtual path must use '/' as the separator");
    }
    if (virtual_path.front() != '/') {
      throw std::invalid_argument("Virtual path must start with '/'");
    }
    if (virtual_path.size() > 1 && virtual_path.back() == '/') {
      throw std::invalid_argument(
        "Virtual path must not end with '/' (except the root)");
    }
    if (virtual_path.find("//") != std::string_view::npos) {
      throw std::invalid_argument("Virtual path must not contain '//'");
    }

    size_t pos = 0;
    while (pos <= virtual_path.size()) {
      const auto next = virtual_path.find('/', pos);
      const auto len = (next == std::string_view::npos)
        ? (virtual_path.size() - pos)
        : (next - pos);
      const auto segment = virtual_path.substr(pos, len);
      if (segment == ".") {
        throw std::invalid_argument("Virtual path must not contain '.'");
      }
      if (segment == "..") {
        throw std::invalid_argument("Virtual path must not contain '..'");
      }

      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
  }

} // namespace

struct VirtualPathResolver::Impl final {
  struct LooseCookedMount {
    uint16_t source_id { 0 };
    data::SourceKey source_key {};
    std::filesystem::path root;
    internal::LooseCookedIndexImpl index;
  };

  struct PakMount {
    uint16_t source_id { 0 };
    data::SourceKey source_key {};
    std::filesystem::path pak_path;
    std::shared_ptr<PakFile> pak;
  };

  std::vector<std::variant<LooseCookedMount, PakMount>> mounts {};
  std::vector<uint16_t> source_ids {};
  std::unordered_map<uint16_t, size_t> source_id_to_mount_index {};
  std::unordered_map<uint16_t, std::unordered_set<data::AssetKey>>
    tombstones_by_source_id {};
  uint16_t next_source_id { 0 };

  auto AddLooseCookedMount(
    std::filesystem::path root, internal::LooseCookedIndexImpl index) -> void
  {
    const auto source_id = next_source_id++;
    source_ids.push_back(source_id);
    source_id_to_mount_index.insert_or_assign(source_id, mounts.size());
    mounts.emplace_back(LooseCookedMount {
      .source_id = source_id,
      .source_key = index.Guid(),
      .root = std::move(root),
      .index = std::move(index),
    });
  }

  auto AddPakMount(std::filesystem::path pak_path, std::shared_ptr<PakFile> pak)
    -> uint16_t
  {
    const auto source_id = next_source_id++;
    source_ids.push_back(source_id);
    source_id_to_mount_index.insert_or_assign(source_id, mounts.size());
    mounts.emplace_back(PakMount {
      .source_id = source_id,
      .source_key = pak->Guid(),
      .pak_path = std::move(pak_path),
      .pak = std::move(pak),
    });
    return source_id;
  }

  auto SetTombstones(const uint16_t source_id,
    const std::span<const data::AssetKey> tombstones) -> void
  {
    auto& source_tombstones = tombstones_by_source_id[source_id];
    source_tombstones.clear();
    source_tombstones.insert(tombstones.begin(), tombstones.end());
  }

  [[nodiscard]] auto IsTombstoned(
    const uint16_t source_id, const data::AssetKey& key) const -> bool
  {
    if (const auto it = tombstones_by_source_id.find(source_id);
      it != tombstones_by_source_id.end()) {
      return it->second.contains(key);
    }
    return false;
  }

  [[nodiscard]] auto SourceHasAsset(
    const uint16_t source_id, const data::AssetKey& key) const -> bool
  {
    const auto mount_it = source_id_to_mount_index.find(source_id);
    if (mount_it == source_id_to_mount_index.end()) {
      return false;
    }
    const auto& mount = mounts.at(mount_it->second);
    if (std::holds_alternative<LooseCookedMount>(mount)) {
      const auto& loose_mount = std::get<LooseCookedMount>(mount);
      return loose_mount.index.FindAssetType(key).has_value();
    }
    const auto& pak_mount = std::get<PakMount>(mount);
    return pak_mount.pak->FindEntry(key).has_value();
  }

  [[nodiscard]] auto ResolveVirtualPathInSource(const uint16_t source_id,
    const std::string_view virtual_path) const -> std::optional<data::AssetKey>
  {
    const auto mount_it = source_id_to_mount_index.find(source_id);
    if (mount_it == source_id_to_mount_index.end()) {
      return std::nullopt;
    }
    const auto& mount = mounts.at(mount_it->second);
    if (std::holds_alternative<LooseCookedMount>(mount)) {
      const auto& loose_mount = std::get<LooseCookedMount>(mount);
      return loose_mount.index.FindAssetKeyByVirtualPath(virtual_path);
    }
    const auto& pak_mount = std::get<PakMount>(mount);
    return pak_mount.pak->ResolveAssetKeyByVirtualPath(virtual_path);
  }

  [[nodiscard]] auto MountedSourceKeys() const -> std::vector<data::SourceKey>
  {
    std::vector<data::SourceKey> source_keys {};
    source_keys.reserve(mounts.size());
    for (const auto& mount : mounts) {
      if (std::holds_alternative<LooseCookedMount>(mount)) {
        source_keys.push_back(std::get<LooseCookedMount>(mount).source_key);
      } else {
        source_keys.push_back(std::get<PakMount>(mount).source_key);
      }
    }
    return source_keys;
  }

  auto Clear() -> void
  {
    mounts.clear();
    source_ids.clear();
    source_id_to_mount_index.clear();
    tombstones_by_source_id.clear();
    next_source_id = 0;
  }
};

VirtualPathResolver::VirtualPathResolver()
  : impl_(std::make_unique<Impl>())
{
}

VirtualPathResolver::~VirtualPathResolver() = default;

auto VirtualPathResolver::AddLooseCookedRoot(
  const std::filesystem::path& cooked_root) -> void
{
  std::filesystem::path normalized
    = std::filesystem::weakly_canonical(cooked_root);
  const auto index_path = normalized / "container.index.bin";

  DLOG_F(
    INFO, "VirtualPathResolver: loading index from {}", index_path.string());
  auto index = internal::LooseCookedIndexImpl::LoadFromFile(index_path);
  DLOG_F(INFO, "VirtualPathResolver: loaded index with {} assets",
    index.GetAllAssetKeys().size());

  impl_->AddLooseCookedMount(std::move(normalized), std::move(index));
}

auto VirtualPathResolver::AddPakFile(const std::filesystem::path& pak_path)
  -> void
{
  std::filesystem::path normalized
    = std::filesystem::weakly_canonical(pak_path);

  auto pak = std::make_shared<PakFile>(normalized);
  (void)impl_->AddPakMount(std::move(normalized), std::move(pak));
}

auto VirtualPathResolver::AddPatchPakFile(const std::filesystem::path& pak_path,
  const data::PatchManifest& manifest,
  const std::span<const data::PakCatalog> mounted_base_catalogs) -> void
{
  const auto mounted_source_keys = impl_->MountedSourceKeys();
  const auto compatibility = internal::ValidatePatchCompatibility(
    mounted_source_keys, mounted_base_catalogs, manifest);
  if (!compatibility.compatible) {
    for (const auto& diagnostic : compatibility.diagnostics) {
      LOG_F(ERROR, "Patch compatibility violation [{}]: {}",
        internal::to_string(diagnostic.code), diagnostic.message);
    }
    throw std::runtime_error(
      "Patch compatibility validation failed against mounted base set");
  }

  std::filesystem::path normalized
    = std::filesystem::weakly_canonical(pak_path);
  auto pak = std::make_shared<PakFile>(normalized);
  const auto source_id
    = impl_->AddPakMount(std::move(normalized), std::move(pak));
  impl_->SetTombstones(source_id, manifest.deleted);
}

auto VirtualPathResolver::ClearMounts() -> void { impl_->Clear(); }

auto VirtualPathResolver::ResolveAssetKey(
  const std::string_view virtual_path) const -> std::optional<data::AssetKey>
{
  ValidateVirtualPathOrThrow(virtual_path);

  const internal::VirtualPathResolutionCallbacks callbacks {
    .key_resolution = {
      .source_has_asset = [impl = impl_.get()](const uint16_t source_id,
                            const data::AssetKey& key) -> bool {
        return impl->SourceHasAsset(source_id, key);
      },
      .source_tombstones_asset = [impl = impl_.get()](const uint16_t source_id,
                                   const data::AssetKey& key) -> bool {
        return impl->IsTombstoned(source_id, key);
      },
    },
    .resolve_virtual_path
    = [impl = impl_.get()](const uint16_t source_id,
        const std::string_view path) -> std::optional<data::AssetKey> {
      return impl->ResolveVirtualPathInSource(source_id, path);
    },
  };

  const auto resolution = internal::ResolveVirtualPathByPrecedence(
    impl_->source_ids, virtual_path, callbacks);
  for (const auto& collision : resolution.collisions) {
    LOG_F(WARNING,
      "Virtual path collision masked by precedence: path='{}' winner_source={} "
      "winner_key='{}' masked_source={} masked_key='{}'",
      std::string(virtual_path), collision.winner_source_id,
      data::to_string(collision.winner_key), collision.masked_source_id,
      data::to_string(collision.masked_key));
  }

  return resolution.asset_key;
}

} // namespace oxygen::content
