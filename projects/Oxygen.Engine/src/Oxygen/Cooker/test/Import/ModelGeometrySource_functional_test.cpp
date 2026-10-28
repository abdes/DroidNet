//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/fbx/FbxAdapter.cpp,
//   Import/Internal/gltf/GltfAdapter.cpp,
//   Import/Internal/Pipelines/MeshBuildPipeline.cpp

#include <array>
#include <cstddef>
#include <filesystem>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/epsilon.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vector_relational.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
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
    NamingService naming;
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
  //! Each corner of the first geometry's LOD 0: its position and its UV.
  struct Corner final {
    glm::vec3 position;
    glm::vec2 uv;
  };

  template <typename Adapter>
  auto CornersOf(const std::string_view filename) -> std::vector<Corner>
  {
    const auto path = oxygen::cooker::test::ModelPath(filename);
    const auto source_id = path.string();
    NamingService naming;
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request.source_path = path;
    input.naming_service = observer_ptr { &naming };
    auto adapter = Adapter {};
    auto sink = GeometrySink {};
    if (!adapter.Parse(path, input).success
      || !adapter.BuildWorkItems(adapters::GeometryWorkTag {}, sink, input)
        .success
      || sink.items.empty() || sink.items.front().lods.empty()) {
      ADD_FAILURE() << filename << " produced no geometry";
      return {};
    }
    const auto& streams = sink.items.front().lods.front().source.streams;
    auto corners = std::vector<Corner> {};
    for (const auto [position, uv] :
      std::views::zip(streams.positions, streams.texcoords)) {
      corners.push_back({ .position = position, .uv = uv });
    }
    return corners;
  }

  //! glTF's bitangent, cross(N, T) * TANGENT.w, runs up the image; Oxygen's
  //! runs down it, the way V grows (`space::texture`). The triangle's V grows
  //! along glTF +Y, Oxygen +Z, and its tangent's w of -1 is what MikkTSpace
  //! authors for that layout.
  NOLINT_TEST(ModelGeometrySourceTest, GltfBitangentRunsTheWayVGrows)
  {
    constexpr auto document = std::string_view { R"({
      "asset":{"version":"2.0"},
      "buffers":[{"byteLength":144,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAvwAAgD8AAAAAAAAAAAAAgL8AAIA/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"}],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36},
        {"buffer":0,"byteOffset":36,"byteLength":36},
        {"buffer":0,"byteOffset":72,"byteLength":48},
        {"buffer":0,"byteOffset":120,"byteLength":24}],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
         "min":[0,0,0],"max":[1,1,0]},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
        {"bufferView":3,"componentType":5126,"count":3,"type":"VEC2"}],
      "meshes":[{"primitives":[{"attributes":
        {"POSITION":0,"NORMAL":1,"TANGENT":2,"TEXCOORD_0":3}}]}],
      "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
    })" };
    NamingService naming;
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "tangents";
    input.request.source_path = "authoring/tangents.gltf";
    input.naming_service = observer_ptr { &naming };
    auto adapter = adapters::GltfAdapter {};
    auto sink = GeometrySink {};
    ASSERT_TRUE(adapter
        .Parse(
          std::as_bytes(std::span(document.data(), document.size())), input)
        .success);
    ASSERT_TRUE(
      adapter.BuildWorkItems(adapters::GeometryWorkTag {}, sink, input)
        .success);
    ASSERT_FALSE(sink.items.empty());
    ASSERT_FALSE(sink.items.front().lods.empty());

    const auto& streams = sink.items.front().lods.front().source.streams;
    ASSERT_EQ(streams.bitangents.size(), 3U);
    for (const auto& bitangent : streams.bitangents) {
      EXPECT_EQ(bitangent, glm::vec3(0.0F, 0.0F, 1.0F));
    }
  }

  //! UVs start at the image's top row, as glTF's do. FBX UVs start at its
  //! bottom row, so the importer flips V: the fixture's authored UVs (0,0),
  //! (1,0) and (0,1), one per corner in polygon order, sample the image at
  //! (0,1), (1,1) and (0,0).
  NOLINT_TEST(ModelGeometrySourceTest, FbxUvsStartAtTheImageTopRow)
  {
    constexpr auto kTolerance = 1.0e-6F;
    constexpr auto kExpected = std::to_array<glm::vec2>({
      { 0.0F, 1.0F },
      { 1.0F, 1.0F },
      { 0.0F, 0.0F },
    });

    const auto corners
      = CornersOf<adapters::FbxAdapter>("static_textured_triangle.fbx");

    ASSERT_EQ(corners.size(), kExpected.size());
    for (const auto [corner, expected] : std::views::zip(corners, kExpected)) {
      EXPECT_TRUE(glm::all(glm::epsilonEqual(corner.uv, expected, kTolerance)))
        << "uv (" << corner.uv.x << ", " << corner.uv.y << "), expected ("
        << expected.x << ", " << expected.y << ")";
    }
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
    NamingService naming;
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
