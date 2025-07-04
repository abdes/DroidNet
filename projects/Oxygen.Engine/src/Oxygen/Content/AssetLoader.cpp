//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
// #include <Oxygen/Content/Loaders/MeshLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>

using namespace oxygen::content;

AssetLoader::AssetLoader()
{
  using oxygen::serio::FileStream;

  LOG_SCOPE_FUNCTION(INFO);

  RegisterLoader(loaders::LoadGeometryAsset<FileStream<>>);
  RegisterLoader(loaders::LoadMaterialAsset<FileStream<>>);
}

void AssetLoader::AddPakFile(const std::filesystem::path& path)
{
  paks_.push_back(std::make_unique<PakFile>(path));
}

void AssetLoader::AddTypeErasedLoader(
  oxygen::TypeId type_id, std::string_view type_name, LoaderFnErased loader)
{
  auto [it, inserted] = loaders_.insert_or_assign(type_id, std::move(loader));
  if (!inserted) {
    LOG_F(WARNING, "Replacing loader for type: {}/{}", type_id, type_name);
  } else {
    LOG_F(INFO, "Registered loader for type: {}/{}", type_id, type_name);
  }
}

void AssetLoader::AddAssetDependency(const oxygen::data::AssetKey& dependent,
  const oxygen::data::AssetKey& dependency)
{
  LOG_SCOPE_F(2, "Add Asset Dependency");
  LOG_F(2, "dependent: {} -> dependency: {}", nostd::to_string(dependent),
    nostd::to_string(dependency));

  // Add forward dependency
  asset_dependencies_[dependent].insert(dependency);

  // Add reverse dependency for reference counting
  reverse_asset_dependencies_[dependency].insert(dependent);
}

void AssetLoader::AddResourceDependency(const oxygen::data::AssetKey& dependent,
  oxygen::data::pak::ResourceIndexT resource_index)
{
  LOG_SCOPE_F(2, "Add Resource Dependency");
  LOG_F(2, "dependent: {} -> resource: {}", nostd::to_string(dependent),
    resource_index);

  // Add forward dependency
  resource_dependencies_[dependent].insert(resource_index);

  // Add reverse dependency for reference counting
  reverse_resource_dependencies_[resource_index].insert(dependent);
}
