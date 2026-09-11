//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BuiltinGeometry.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/ProceduralMeshes.h>

namespace {

constexpr auto kUriPrefix
  = std::string_view { "asset:///Engine/Generated/BasicShapes/" };
constexpr auto kNames = std::array<std::string_view, 11> { "Cube",
  "SubdividedCube", "Sphere", "IcoSphere", "GeodesicSphere", "Plane",
  "Cylinder", "Cone", "Torus", "Quad", "ArrowGizmo" };

constexpr auto AsciiLower(const char value) noexcept -> char
{
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                      : value;
}

auto EqualsName(std::string_view first, std::string_view second) noexcept
  -> bool
{
  return first.size() == second.size()
    && std::equal(first.begin(), first.end(), second.begin(),
      [](const char lhs, const char rhs) {
        return AsciiLower(lhs) == AsciiLower(rhs);
      });
}

struct GeometryCache {
  std::mutex mutex;
  std::unordered_map<oxygen::data::AssetKey,
    std::shared_ptr<const oxygen::data::GeometryAsset>>
    assets;
};

auto Cache() -> GeometryCache&
{
  static GeometryCache cache;
  return cache;
}

auto CreateGeometry(const oxygen::data::BuiltinGeometryIdentity& identity)
  -> std::shared_ptr<const oxygen::data::GeometryAsset>
{
  using namespace oxygen::data;
  auto name = std::string(identity.name);
  std::ranges::transform(name, name.begin(), AsciiLower);
  auto mesh = GenerateMesh(std::string(identity.generator) + "/" + name, {});
  if (!mesh) {
    return {};
  }

  auto descriptor = pak::geometry::GeometryAssetDesc {};
  descriptor.header.asset_type = static_cast<uint8_t>(AssetType::kGeometry);
  descriptor.header.version = pak::geometry::kGeometryAssetVersion;
  const auto name_size
    = (std::min)(name.size(), sizeof(descriptor.header.name) - 1);
  std::memcpy(descriptor.header.name, name.data(), name_size);
  descriptor.lod_count = 1;
  const auto minimum = mesh->BoundingBoxMin();
  const auto maximum = mesh->BoundingBoxMax();
  for (auto axis = 0; axis < 3; ++axis) {
    descriptor.bounding_box_min[axis] = minimum[axis];
    descriptor.bounding_box_max[axis] = maximum[axis];
  }

  auto meshes = std::vector<std::shared_ptr<Mesh>> {};
  meshes.emplace_back(std::move(mesh));
  return std::make_shared<const GeometryAsset>(
    AssetKey::FromVirtualPath(identity.asset_uri), descriptor,
    std::move(meshes));
}

} // namespace

namespace oxygen::data {

auto GetBuiltinGeometryNames() noexcept -> std::span<const std::string_view>
{
  return kNames;
}

auto IsBuiltinGeometryUri(const std::string_view asset_uri) noexcept -> bool
{
  return asset_uri.size() >= kUriPrefix.size()
    && EqualsName(asset_uri.substr(0, kUriPrefix.size()), kUriPrefix);
}

auto ResolveBuiltinGeometryIdentity(const std::string_view asset_uri)
  -> std::optional<BuiltinGeometryIdentity>
{
  if (!IsBuiltinGeometryUri(asset_uri)) {
    return std::nullopt;
  }

  const auto requested = asset_uri.substr(kUriPrefix.size());
  for (const auto name : kNames) {
    if (EqualsName(requested, name)) {
      return BuiltinGeometryIdentity { .name = name,
        .generator = name == "GeodesicSphere" ? "IcoSphere" : name,
        .asset_uri = std::string(kUriPrefix) + std::string(name),
        .descriptor_name
        = "Engine_Generated_BasicShapes_" + std::string(name) };
    }
  }
  return std::nullopt;
}

auto ResolveBuiltinGeometry(const std::string_view asset_uri)
  -> std::shared_ptr<const GeometryAsset>
{
  const auto identity = ResolveBuiltinGeometryIdentity(asset_uri);
  if (!identity) {
    return {};
  }
  auto& cache = Cache();
  const auto key = AssetKey::FromVirtualPath(identity->asset_uri);
  {
    const auto lock = std::scoped_lock(cache.mutex);
    if (const auto found = cache.assets.find(key);
      found != cache.assets.end()) {
      return found->second;
    }
  }

  auto geometry = CreateGeometry(*identity);
  if (!geometry) {
    return {};
  }
  const auto lock = std::scoped_lock(cache.mutex);
  return cache.assets.try_emplace(key, std::move(geometry)).first->second;
}

} // namespace oxygen::data
