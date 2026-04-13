//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace oxygen::vortex::testing {

[[nodiscard]] inline auto MakeGeometryAssetKey(std::string_view token)
  -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath(
    "/Engine/Tests/Renderer/GeometryUploader/" + std::string(token) + ".ogeo");
}

class GeometryUploaderTest : public ::testing::Test {
protected:
  auto SetUp() -> void override;
  auto TearDown() -> void override;

  [[nodiscard]] auto GfxPtr() const -> observer_ptr<Graphics>;

  [[nodiscard]] auto Uploader() const -> vortex::upload::UploadCoordinator&;
  [[nodiscard]] auto Staging() const -> vortex::upload::StagingProvider&;
  [[nodiscard]] auto GeoUploader() const -> resources::GeometryUploader&;
  [[nodiscard]] auto Loader() const -> FakeAssetLoader&;

  auto BeginFrame(frame::Slot slot) -> void;

  [[nodiscard]] auto MakeValidTriangleMesh(std::string_view name,
    bool indexed = true) const -> std::shared_ptr<const data::Mesh>;

  [[nodiscard]] auto MakeInvalidMesh_NoVertices(std::string_view name) const
    -> std::shared_ptr<const data::Mesh>;

  [[nodiscard]] auto MakeInvalidMesh_NonFiniteVertex(
    std::string_view name) const -> std::shared_ptr<const data::Mesh>;

private:
  [[nodiscard]] auto DefaultMaterial() const
    -> std::shared_ptr<const data::MaterialAsset>;

  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<vortex::upload::UploadCoordinator> uploader_;
  std::shared_ptr<vortex::upload::StagingProvider> staging_provider_;
  std::unique_ptr<FakeAssetLoader> asset_loader_;
  std::unique_ptr<resources::GeometryUploader> geo_uploader_;
  std::shared_ptr<const data::MaterialAsset> default_material_;
};

} // namespace oxygen::vortex::testing
