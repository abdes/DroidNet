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
#include <Oxygen/Content/Loaders/ShaderLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>

using namespace oxygen::content;

AssetLoader::AssetLoader()
{
  using oxygen::serio::FileStream;

  LOG_SCOPE_FUNCTION(INFO);

  // RegisterLoader(AssetType::kGeometry, loaders::LoadGeometry);
  // RegisterLoader(AssetType::kMesh, loaders::LoadMesh);
  RegisterLoader(
    AssetType::kMaterial, loaders::LoadMaterialAsset<FileStream<>>);
  RegisterLoader(AssetType::kShader, loaders::LoadShaderAsset<FileStream<>>);
  RegisterLoader(AssetType::kTexture, loaders::LoadTextureAsset<FileStream<>>);
}

void AssetLoader::AddPakFile(const std::filesystem::path& path)
{
  paks_.push_back(std::make_unique<PakFile>(path));
}

void AssetLoader::AddTypeErasedLoader(AssetType type, LoaderFnErased loader)
{
  auto [it, inserted] = loaders_.insert_or_assign(type, std::move(loader));
  if (!inserted) {
    LOG_F(WARNING, "Replacing loader for type: {}", nostd::to_string(type));
  } else {
    LOG_F(INFO, "Registered loader for type: {}", nostd::to_string(type));
  }
}
