//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/PakFileSource.h>
#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content {

struct VirtualPathResolver::Impl final {
  internal::ContentSourceRegistry sources;
};

VirtualPathResolver::VirtualPathResolver()
  : impl_(std::make_unique<Impl>())
{
}

VirtualPathResolver::~VirtualPathResolver() = default;

VirtualPathResolver::VirtualPathResolver(VirtualPathResolver&&) noexcept
  = default;
auto VirtualPathResolver::operator=(VirtualPathResolver&&) noexcept
  -> VirtualPathResolver& = default;

auto VirtualPathResolver::Swap(VirtualPathResolver& other) noexcept -> void
{
  impl_.swap(other.impl_);
}

auto VirtualPathResolver::AddLooseCookedRoot(
  const std::filesystem::path& cooked_root) -> void
{
  std::filesystem::path normalized = base::ToLogicalPath(
    std::filesystem::weakly_canonical(base::ToNativePath(cooked_root)));
  auto source = std::make_shared<internal::LooseCookedSource>(
    normalized, internal::LooseCookedSource::OpenMode::kIndexOnly);
  static_cast<void>(impl_->sources.MountLoose(source->DebugName(), source,
    internal::ContentSourceRegistry::MountMode::kAppend));
}

auto VirtualPathResolver::AddPakFile(const std::filesystem::path& pak_path)
  -> void
{
  std::filesystem::path normalized
    = std::filesystem::weakly_canonical(pak_path);

  auto source = std::make_shared<internal::PakFileSource>(normalized, false);
  static_cast<void>(impl_->sources.MountPak(std::move(normalized),
    std::move(source), internal::ContentSourceRegistry::MountMode::kAppend));
}

auto VirtualPathResolver::ClearMounts() -> void { impl_->sources.Clear(); }

auto VirtualPathResolver::ResolveAssetKey(
  const std::string_view virtual_path) const -> std::optional<data::AssetKey>
{
  if (const auto error = ValidateCanonicalVirtualPath(
        virtual_path, VirtualPathRuleSet::kSyntaxAndStandardMountRoot);
    error.has_value()) {
    throw std::invalid_argument(
      "Virtual path is not canonical: " + std::string(*error));
  }

  const internal::VirtualPathResolutionCallbacks callbacks {
    .key_resolution = {
      .source_has_asset
      = [impl = impl_.get()](const data::SourceInstanceId source_id,
          const data::AssetKey& key) -> bool {
        const auto source = impl->sources.AcquireSource(source_id);
        return source && source->HasAsset(key);
      },
      .source_tombstones_asset
      = [impl = impl_.get()](const data::SourceInstanceId source_id,
          const data::AssetKey& key) -> bool {
        return impl->sources.IsSourceTombstoningAsset(source_id, key);
      },
    },
    .resolve_virtual_path
    = [impl = impl_.get()](const data::SourceInstanceId source_id,
        const std::string_view path) -> std::optional<data::AssetKey> {
      const auto source = impl->sources.AcquireSource(source_id);
      return source ? source->FindAssetKeyByVirtualPath(path) : std::nullopt;
    },
  };

  const auto resolution = internal::ResolveVirtualPathByPrecedence(
    impl_->sources.SourceIds(), virtual_path, callbacks);
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
