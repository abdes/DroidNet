//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/gltf/GltfMeshopt.cpp,
//   Import/Internal/gltf/GltfAdapter.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/GltfDocuments.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {

  using nlohmann::json;
  using oxygen::cooker::test::HasDiagnosticCode;

  constexpr auto kPositions = std::to_array<float>({
    0.0F,
    0.0F,
    0.0F, //
    1.0F,
    0.0F,
    0.0F, //
    1.0F,
    1.0F,
    0.0F, //
    0.0F,
    1.0F,
    0.0F, //
  });
  constexpr auto kIndices = std::to_array<uint32_t>({ 0, 1, 2, 0, 2, 3 });
  constexpr auto kVertexCount = std::size_t { 4 };
  constexpr auto kPositionStride = std::size_t { 12 };
  constexpr auto kIndexStride = std::size_t { 4 };
  // Octahedral normals: four signed bytes per vertex.
  constexpr auto kNormalStride = std::size_t { 4 };
  constexpr auto kNormalBits = 8;
  // One step of a signed normalized byte.
  constexpr auto kNormalTolerance = 1.0F / 127.0F;

  //! One buffer view's `EXT_meshopt_compression` object.
  struct CompressedView final {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::size_t stride = 0;
    std::size_t count = 0;
    std::string_view mode;
    std::string_view filter;
  };

  [[nodiscard]] auto Compressed(const CompressedView& view) -> json
  {
    auto extension = json {
      { "buffer", 0 },
      { "byteOffset", view.offset },
      { "byteLength", view.length },
      { "byteStride", view.stride },
      { "count", view.count },
      { "mode", view.mode },
    };
    if (!view.filter.empty()) {
      extension["filter"] = view.filter;
    }
    return { { "EXT_meshopt_compression", extension } };
  }

  [[nodiscard]] auto Mesh(const json& attributes) -> json
  {
    return json::array({
      {
        { "name", "Quad" },
        {
          "primitives",
          json::array({
            { { "attributes", attributes }, { "indices", 1 } },
          }),
        },
      },
    });
  }

  //! The quad, its positions, indices and normals as plain buffer views.
  [[nodiscard]] auto PlainDocument() -> std::string
  {
    const auto normals = std::to_array<float>({
      0.0F,
      0.0F,
      1.0F, //
      0.0F,
      0.0F,
      1.0F, //
      0.0F,
      0.0F,
      1.0F, //
      0.0F,
      0.0F,
      1.0F, //
    });
    auto buffer = std::vector<unsigned char> {};
    const auto positions_at
      = AppendAligned(buffer, std::as_bytes(std::span(kPositions)));
    const auto indices_at
      = AppendAligned(buffer, std::as_bytes(std::span(kIndices)));
    const auto normals_at
      = AppendAligned(buffer, std::as_bytes(std::span(normals)));
    return json {
      { "asset", { { "version", "2.0" } } },
      {
        "buffers",
        json::array({
          { { "uri", DataUri(buffer) }, { "byteLength", buffer.size() } },
        }),
      },
      {
        "bufferViews",
        json::array({
          {
            { "buffer", 0 },
            { "byteOffset", positions_at },
            { "byteLength", sizeof(kPositions) },
          },
          {
            { "buffer", 0 },
            { "byteOffset", indices_at },
            { "byteLength", sizeof(kIndices) },
          },
          {
            { "buffer", 0 },
            { "byteOffset", normals_at },
            { "byteLength", sizeof(normals) },
          },
        }),
      },
      {
        "accessors",
        json::array({
          {
            { "bufferView", 0 },
            { "componentType", 5126 },
            { "count", kVertexCount },
            { "type", "VEC3" },
          },
          {
            { "bufferView", 1 },
            { "componentType", 5125 },
            { "count", kIndices.size() },
            { "type", "SCALAR" },
          },
          {
            { "bufferView", 2 },
            { "componentType", 5126 },
            { "count", kVertexCount },
            { "type", "VEC3" },
          },
        }),
      },
      { "meshes", Mesh({ { "POSITION", 0 }, { "NORMAL", 2 } }) },
    }
      .dump();
  }

  //! The quad compressed with `EXT_meshopt_compression`: positions as
  //! attributes, indices as triangles and normals with the octahedral filter.
  //! `corrupt` damages the compressed positions.
  [[nodiscard]] auto CompressedDocument(const bool corrupt) -> std::string
  {
    auto positions = std::vector<unsigned char>(
      meshopt_encodeVertexBufferBound(kVertexCount, kPositionStride));
    positions.resize(meshopt_encodeVertexBuffer(positions.data(),
      positions.size(), kPositions.data(), kVertexCount, kPositionStride));
    if (corrupt) {
      positions.front() = 0;
    }

    auto indices = std::vector<unsigned char>(
      meshopt_encodeIndexBufferBound(kIndices.size(), kVertexCount));
    indices.resize(meshopt_encodeIndexBuffer(
      indices.data(), indices.size(), kIndices.data(), kIndices.size()));

    const auto unit_normals = std::to_array<float>({
      0.0F,
      0.0F,
      1.0F,
      0.0F, //
      0.0F,
      0.0F,
      1.0F,
      0.0F, //
      0.0F,
      0.0F,
      1.0F,
      0.0F, //
      0.0F,
      0.0F,
      1.0F,
      0.0F, //
    });
    auto filtered = std::array<unsigned char, kVertexCount * kNormalStride> {};
    meshopt_encodeFilterOct(filtered.data(), kVertexCount, kNormalStride,
      kNormalBits, unit_normals.data());
    auto normals = std::vector<unsigned char>(
      meshopt_encodeVertexBufferBound(kVertexCount, kNormalStride));
    normals.resize(meshopt_encodeVertexBuffer(normals.data(), normals.size(),
      filtered.data(), kVertexCount, kNormalStride));

    auto buffer = std::vector<unsigned char> {};
    const auto positions_at
      = AppendAligned(buffer, std::as_bytes(std::span(positions)));
    const auto indices_at
      = AppendAligned(buffer, std::as_bytes(std::span(indices)));
    const auto normals_at
      = AppendAligned(buffer, std::as_bytes(std::span(normals)));

    constexpr auto kPositionsSize = kVertexCount * kPositionStride;
    constexpr auto kIndicesSize = sizeof(kIndices);
    constexpr auto kNormalsSize = kVertexCount * kNormalStride;
    return json {
      { "asset", { { "version", "2.0" } } },
      {
        "extensionsUsed",
        json::array({ "EXT_meshopt_compression", "KHR_mesh_quantization" }),
      },
      {
        "extensionsRequired",
        json::array({ "EXT_meshopt_compression", "KHR_mesh_quantization" }),
      },
      {
        "buffers",
        json::array({
          { { "uri", DataUri(buffer) }, { "byteLength", buffer.size() } },
          {
            { "byteLength", kPositionsSize + kIndicesSize + kNormalsSize },
            {
              "extensions",
              { { "EXT_meshopt_compression", { { "fallback", true } } } },
            },
          },
        }),
      },
      {
        "bufferViews",
        json::array({
          {
            { "buffer", 1 },
            { "byteOffset", 0 },
            { "byteLength", kPositionsSize },
            { "byteStride", kPositionStride },
            {
              "extensions",
              Compressed({
                .offset = positions_at,
                .length = positions.size(),
                .stride = kPositionStride,
                .count = kVertexCount,
                .mode = "ATTRIBUTES",
                .filter = {},
              }),
            },
          },
          {
            { "buffer", 1 },
            { "byteOffset", kPositionsSize },
            { "byteLength", kIndicesSize },
            {
              "extensions",
              Compressed({
                .offset = indices_at,
                .length = indices.size(),
                .stride = kIndexStride,
                .count = kIndices.size(),
                .mode = "TRIANGLES",
                .filter = {},
              }),
            },
          },
          {
            { "buffer", 1 },
            { "byteOffset", kPositionsSize + kIndicesSize },
            { "byteLength", kNormalsSize },
            { "byteStride", kNormalStride },
            {
              "extensions",
              Compressed({
                .offset = normals_at,
                .length = normals.size(),
                .stride = kNormalStride,
                .count = kVertexCount,
                .mode = "ATTRIBUTES",
                .filter = "OCTAHEDRAL",
              }),
            },
          },
        }),
      },
      {
        "accessors",
        json::array({
          {
            { "bufferView", 0 },
            { "componentType", 5126 },
            { "count", kVertexCount },
            { "type", "VEC3" },
          },
          {
            { "bufferView", 1 },
            { "componentType", 5125 },
            { "count", kIndices.size() },
            { "type", "SCALAR" },
          },
          {
            { "bufferView", 2 },
            { "componentType", 5120 },
            { "normalized", true },
            { "count", kVertexCount },
            { "type", "VEC3" },
          },
        }),
      },
      { "meshes", Mesh({ { "POSITION", 0 }, { "NORMAL", 2 } }) },
    }
      .dump();
  }

  //! A glTF file whose geometry uses meshopt compression imports the same
  //! positions, triangles and normals as its uncompressed twin.
  NOLINT_TEST(GltfMeshoptTest, CompressedGeometryReadsAsItsPlainTwin)
  {
    const auto plain = ReadGltfDocument(PlainDocument());
    const auto compressed = ReadGltfDocument(CompressedDocument(false));

    ASSERT_TRUE(plain.parse.success);
    ASSERT_TRUE(compressed.parse.success);
    EXPECT_FALSE(HasDiagnosticCode(
      compressed.parse.diagnostics, import_codes::kSourceExtensionRequired));
    ASSERT_EQ(plain.items.size(), 1U);
    ASSERT_EQ(compressed.items.size(), 1U);
    const auto& expected = plain.items.front().lods.front().source;
    const auto& actual = compressed.items.front().lods.front().source;
    ASSERT_EQ(actual.streams.positions.size(), kVertexCount);
    EXPECT_TRUE(
      std::ranges::equal(actual.streams.positions, expected.streams.positions));
    EXPECT_TRUE(std::ranges::equal(actual.indices, expected.indices));
    ASSERT_EQ(actual.streams.normals.size(), expected.streams.normals.size());
    for (std::size_t i = 0; i < kVertexCount; ++i) {
      const auto& normal = actual.streams.normals[i];
      const auto& twin = expected.streams.normals[i];
      EXPECT_NEAR(normal.x, twin.x, kNormalTolerance);
      EXPECT_NEAR(normal.y, twin.y, kNormalTolerance);
      EXPECT_NEAR(normal.z, twin.z, kNormalTolerance);
    }
  }

  //! Compressed bytes that do not decode refuse the document, naming the
  //! buffer view.
  NOLINT_TEST(GltfMeshoptTest, UndecodableBytesRefuseTheDocument)
  {
    const auto read = ReadGltfDocument(CompressedDocument(true));

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(HasDiagnosticCode(
      read.parse.diagnostics, import_codes::kGltfMeshoptDecodeFailed));
  }

} // namespace
} // namespace oxygen::content::import::test
