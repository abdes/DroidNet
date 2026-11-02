//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/ResourceTypes.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::internal {

auto IContentSource::PayloadOf(
  const TypeId resource_type, const serio::ByteRange extent) const
  -> std::optional<serio::Region<serio::AnyAsyncDevice>>
{
  const auto payloads = PayloadRegion(resource_type);
  return payloads ? PayloadWithin(*payloads, extent) : std::nullopt;
}

auto IContentSource::PayloadWithin(
  const serio::Region<serio::AnyAsyncDevice>& payloads,
  const serio::ByteRange extent)
  -> std::optional<serio::Region<serio::AnyAsyncDevice>>
{
  const auto origin = payloads.Range().offset;
  if (extent.offset < origin) {
    return std::nullopt;
  }
  auto payload = payloads.Sub({
    .offset = serio::ByteOffset { extent.offset.get() - origin.get() },
    .count = extent.count,
  });
  if (!payload) {
    return std::nullopt;
  }
  return *payload;
}

auto IContentSource::CreateAssetDescriptorReader(
  const data::AssetKey& key) const -> std::unique_ptr<serio::AnyReader>
{
  auto reader = OpenAssetDescriptor(key);
  if (!reader) {
    return nullptr;
  }
  const auto bytes = reader->ReadBlob(reader->Remaining().get());
  if (!bytes) {
    throw std::system_error(
      bytes.error(), "Could not read asset descriptor " + data::to_string(key));
  }
  data::CheckDescriptorHash(*bytes);
  if (const auto rewound = reader->Seek(0); !rewound) {
    throw std::system_error(rewound.error(),
      "Could not rewind asset descriptor " + data::to_string(key));
  }
  return reader;
}

auto IContentSource::ResourceDescriptorBytes(
  const TypeId resource_type, const data::pak::core::ResourceIndexT index) const
  -> std::optional<std::span<const std::byte>>
{
  using Row = std::optional<std::span<const std::byte>>;
  return VisitResourceType(
    resource_type,
    [&]<typename T> -> Row {
      const auto* table = TableOf<T>();
      if (table == nullptr) {
        return std::nullopt;
      }
      const auto offset = table->GetResourceOffset(index);
      const auto space = ResourceRowSpace(resource_type);
      constexpr auto kSize = sizeof(typename ResourceTable<T>::DescT);
      if (!offset || *offset > space.size() || kSize > space.size() - *offset) {
        return std::nullopt;
      }
      return space.subspan(static_cast<size_t>(*offset), kSize);
    },
    [] -> Row { return std::nullopt; });
}

auto IContentSource::FindTextureVirtualPath(const data::AssetKey& key) const
  -> std::optional<std::string>
{
  std::call_once(texture_key_once_, [this] -> void {
    for (auto& path : TextureVirtualPaths()) {
      const auto texture_key = data::TextureKeyFromVirtualPath(path);
      texture_paths_by_key_.emplace(texture_key, std::move(path));
    }
  });
  const auto found = texture_paths_by_key_.find(key);
  if (found == texture_paths_by_key_.end()) {
    return std::nullopt;
  }
  return found->second;
}

auto IContentSource::FindTextureIndexByKey(const data::AssetKey& key) const
  -> std::optional<data::pak::core::ResourceIndexT>
{
  const auto path = FindTextureVirtualPath(key);
  if (!path.has_value()) {
    return std::nullopt;
  }
  return FindTextureIndexByVirtualPath(*path);
}

auto IContentSource::FindPhysicsResource(const data::AssetKey& key) const
  -> std::optional<data::pak::core::ResourceIndexT>
{
  std::call_once(physics_index_once_, [this] -> void {
    const auto* table = GetPhysicsTable();
    if (!table) {
      return;
    }
    std::unordered_map<data::AssetKey, data::pak::core::ResourceIndexT> index;
    for (uint32_t i = 0; i < table->Size().get(); ++i) {
      const auto position = data::pak::core::ResourceIndexT { i };
      const auto row = ResourceDescriptorBytes(
        data::PhysicsResource::ClassTypeId(), position);
      if (!row) {
        throw std::runtime_error("Invalid physics resource table offset");
      }
      const auto descriptor
        = serio::ByteReader(*row)
            .Read<data::pak::physics::PhysicsResourceDesc>();
      if (!descriptor) {
        throw std::runtime_error("Cannot decode physics resource descriptor");
      }
      if (i == 0U) {
        if (!descriptor->resource_asset_key.IsNil()
          || descriptor->size_bytes != 0U) {
          throw std::runtime_error(
            "Physics resource row zero must be the absent sentinel");
        }
        continue;
      }
      if (descriptor->resource_asset_key.IsNil()) {
        throw std::runtime_error("Physics resource has no stable key");
      }
      if (!index.emplace(descriptor->resource_asset_key, position).second) {
        throw std::runtime_error("Duplicate physics resource key in source");
      }
    }
    physics_index_ = std::move(index);
  });
  const auto found = physics_index_.find(key);
  return found == physics_index_.end() ? std::nullopt
                                       : std::optional(found->second);
}

} // namespace oxygen::content::internal
