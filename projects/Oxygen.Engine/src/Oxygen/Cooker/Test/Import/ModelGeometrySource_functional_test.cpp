//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/fbx/FbxAdapter.cpp,
//   Import/Internal/gltf/GltfAdapter.cpp,
//   Import/Internal/Pipelines/MeshBuildPipeline.cpp

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  class GeometrySink final : public adapters::GeometryWorkItemSink {
  public:
    GeometrySink() = default;
    ~GeometrySink() override = default;
    OXYGEN_MAKE_NON_COPYABLE(GeometrySink)
    OXYGEN_MAKE_NON_MOVABLE(GeometrySink)

    auto Consume(MeshBuildPipeline::WorkItem item) -> bool override
    {
      items.push_back(std::move(item));
      return true;
    }

    std::vector<MeshBuildPipeline::WorkItem> items;
  };

  template <typename Adapter>
  auto CheckGeometryIdentities(const std::string_view filename) -> void
  {
    const auto path = oxygen::cooker::test::ModelPath(filename);
    const auto source_id = path.string();
    auto naming = NamingService({
      .strategy = std::make_shared<NoOpNamingStrategy>(),
      .enable_namespacing = true,
      .enforce_uniqueness = true,
    });
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request.source_path = path;
    input.request.options.coordinate.bake_transforms_into_meshes = true;
    input.naming_service = observer_ptr { &naming };
    auto adapter = Adapter {};
    ASSERT_TRUE(
      adapter.Parse(path, input, adapters::ModelParseMode::kMetadata).success);
    const auto prepared = adapter.PrepareGeometry(input);
    ASSERT_TRUE(prepared.success);
    ASSERT_FALSE(prepared.sources.empty());
    const auto declared_names = naming.GetNameCount(ImportNameKind::kMesh);
    naming.Reset();
    ASSERT_TRUE(adapter.Parse(path, input).success);
    auto sink = GeometrySink {};
    const auto streamed
      = adapter.BuildWorkItems(adapters::GeometryWorkTag {}, sink, input);
    ASSERT_TRUE(streamed.success);
    ASSERT_EQ(sink.items.size(), prepared.sources.size());
    for (size_t i = 0; i < prepared.sources.size(); ++i) {
      const auto& source = prepared.sources.at(i);
      const auto& item = sink.items.at(i);
      EXPECT_EQ(source.name, item.mesh_name);
      EXPECT_EQ(source.name, item.storage_mesh_name);
      EXPECT_EQ(source.source_id, item.source_id);
      EXPECT_EQ(
        source.variant.transform.has_value(), item.bake_transform.has_value());
    }
    EXPECT_EQ(declared_names, naming.GetNameCount(ImportNameKind::kMesh));
  }

  NOLINT_TEST(ModelGeometrySourceTest, GltfDeclaredIdentitiesMatchProduction)
  {
    CheckGeometryIdentities<adapters::GltfAdapter>(
      "static_scalar_triangle.gltf");
  }

  NOLINT_TEST(ModelGeometrySourceTest, FbxDeclaredIdentitiesMatchProduction)
  {
    CheckGeometryIdentities<adapters::FbxAdapter>("static_scalar_triangle.fbx");
  }
  NOLINT_TEST(
    ModelGeometrySourceTest, GltfMetadataDoesNotLoadExternalGeometryBuffers)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "buffers":[{"uri":"absent.bin","byteLength":36}],
      "bufferViews":[{"buffer":0,"byteLength":36}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
      "meshes":[{"name":"Triangle","primitives":[{"attributes":{"POSITION":0}}]}]
    })" };
    auto naming = NamingService({
      .strategy = std::make_shared<NoOpNamingStrategy>(),
      .enable_namespacing = true,
      .enforce_uniqueness = true,
    });
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "logical.gltf";
    input.request.source_path = "logical.gltf";
    input.naming_service = observer_ptr { &naming };
    auto adapter = adapters::GltfAdapter {};
    const auto bytes
      = std::as_bytes(std::span(document.data(), document.size()));
    ASSERT_TRUE(
      adapter.Parse(bytes, input, adapters::ModelParseMode::kMetadata).success);
    const auto geometry = adapter.PrepareGeometry(input);
    ASSERT_TRUE(geometry.success);
    ASSERT_EQ(geometry.sources.size(), 1U);
    EXPECT_FALSE(adapter.Parse(bytes, input).success);
  }
} // namespace
} // namespace oxygen::content::import::test
