//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Detail/LooseCookedIndex.h>
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
    std::filesystem::path root;
    detail::LooseCookedIndex index;
  };

  struct PakMount {
    std::filesystem::path pak_path;
    std::shared_ptr<PakFile> pak;
  };

  std::vector<std::variant<LooseCookedMount, PakMount>> mounts;
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
  auto index = detail::LooseCookedIndex::LoadFromFile(index_path);
  DLOG_F(INFO, "VirtualPathResolver: loaded index with {} assets",
    index.GetAllAssetKeys().size());

  impl_->mounts.emplace_back(Impl::LooseCookedMount {
    .root = std::move(normalized),
    .index = std::move(index),
  });
}

auto VirtualPathResolver::AddPakFile(const std::filesystem::path& pak_path)
  -> void
{
  std::filesystem::path normalized
    = std::filesystem::weakly_canonical(pak_path);

  auto pak = std::make_shared<PakFile>(normalized);

  impl_->mounts.emplace_back(Impl::PakMount {
    .pak_path = normalized,
    .pak = std::move(pak),
  });
}

auto VirtualPathResolver::ClearMounts() -> void { impl_->mounts.clear(); }

auto VirtualPathResolver::ResolveAssetKey(
  const std::string_view virtual_path) const -> std::optional<data::AssetKey>
{
  ValidateVirtualPathOrThrow(virtual_path);

  std::optional<data::AssetKey> first_key;
  std::optional<std::filesystem::path> first_root;

  for (const auto& mount : impl_->mounts) {
    std::optional<data::AssetKey> resolved;
    std::optional<std::filesystem::path> this_location;

    if (std::holds_alternative<Impl::LooseCookedMount>(mount)) {
      const auto& m = std::get<Impl::LooseCookedMount>(mount);
      resolved = m.index.FindAssetKeyByVirtualPath(virtual_path);
      this_location = m.root;
    } else {
      const auto& m = std::get<Impl::PakMount>(mount);
      resolved = m.pak->ResolveAssetKeyByVirtualPath(virtual_path);
      this_location = m.pak_path;
    }

    if (!resolved) {
      continue;
    }

    if (!first_key) {
      first_key = *resolved;
      first_root = *this_location;
      continue;
    }

    if (*resolved != *first_key) {
      LOG_F(WARNING,
        "Virtual path collision: path='{}' first_root='{}' first_key='{}' "
        "other_root='{}' other_key='{}'",
        std::string(virtual_path), first_root->string(),
        data::to_string(*first_key), this_location->string(),
        data::to_string(*resolved));
    }
  }

  return first_key;
}

} // namespace oxygen::content
