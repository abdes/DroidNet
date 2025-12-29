//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::testing {

class GeometryUploaderTest : public ::testing::Test {
protected:
  auto SetUp() -> void override;

  [[nodiscard]] auto GfxPtr() const -> observer_ptr<Graphics>;

  [[nodiscard]] auto Uploader() const -> engine::upload::UploadCoordinator&;
  [[nodiscard]] auto Staging() const -> engine::upload::StagingProvider&;
  [[nodiscard]] auto GeoUploader() const -> resources::GeometryUploader&;

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
  std::unique_ptr<engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<engine::upload::StagingProvider> staging_provider_;
  std::unique_ptr<resources::GeometryUploader> geo_uploader_;
  std::shared_ptr<const data::MaterialAsset> default_material_;
};

} // namespace oxygen::renderer::testing
