//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/AssetLoader.h>
// #include <Oxygen/Content/Loaders/GeometryLoader.h>
// #include <Oxygen/Content/Loaders/MeshLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>

using namespace oxygen::content;

AssetLoader::AssetLoader()
{
  using oxygen::serio::FileStream;

  LOG_SCOPE_FUNCTION(INFO);

  // RegisterLoader(loaders::LoadGeometry);
  // RegisterLoader(loaders::LoadMesh);
  RegisterLoader(loaders::LoadMaterialAsset<FileStream<>>);
  RegisterLoader(loaders::LoadTextureResource<FileStream<>>);
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
