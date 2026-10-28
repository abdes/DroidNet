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
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
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

  [[nodiscard]] auto Base64(const std::span<const unsigned char> bytes)
    -> std::string
  {
    constexpr auto kAlphabet = std::string_view {
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
    };
    auto out = std::string {};
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
      const auto remaining = bytes.size() - i;
      const auto b0 = static_cast<uint32_t>(bytes[i]);
      const auto b1 = remaining > 1 ? static_cast<uint32_t>(bytes[i + 1]) : 0U;
      const auto b2 = remaining > 2 ? static_cast<uint32_t>(bytes[i + 2]) : 0U;
      const auto triple = (b0 << 16U) | (b1 << 8U) | b2;
      out.push_back(kAlphabet.at((triple >> 18U) & 0x3FU));
      out.push_back(kAlphabet.at((triple >> 12U) & 0x3FU));
      out.push_back(remaining > 1 ? kAlphabet.at((triple >> 6U) & 0x3FU) : '=');
      out.push_back(remaining > 2 ? kAlphabet.at(triple & 0x3FU) : '=');
    }
    return out;
  }

  [[nodiscard]] auto DataUri(const std::span<const unsigned char> bytes)
    -> std::string
  {
    return "data:application/octet-stream;base64," + Base64(bytes);
  }

  //! Appends `bytes` to `buffer` at a four-byte boundary; returns the offset.
  auto Append(std::vector<unsigned char>& buffer,
    const std::span<const std::byte> bytes) -> std::size_t
  {
    while (buffer.size() % 4U != 0U) {
      buffer.push_back(0);
    }
    const auto offset = buffer.size();
    for (const auto byte : bytes) {
      buffer.push_back(std::to_integer<unsigned char>(byte));
    }
    return offset;
  }

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
      = Append(buffer, std::as_bytes(std::span(kPositions)));
    const auto indices_at = Append(buffer, std::as_bytes(std::span(kIndices)));
    const auto normals_at = Append(buffer, std::as_bytes(std::span(normals)));
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
      = Append(buffer, std::as_bytes(std::span(positions)));
    const auto indices_at = Append(buffer, std::as_bytes(std::span(indices)));
    const auto normals_at = Append(buffer, std::as_bytes(std::span(normals)));

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

  //! What reading `document` gives: its parse and its geometry work items.
  struct Read final {
    adapters::GltfAdapter::ParseResult parse;
    std::vector<MeshBuildPipeline::WorkItem> items;
  };

  [[nodiscard]] auto ReadDocument(const std::string& document) -> Read
  {
    NamingService naming;
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "quad.gltf";
    input.request.source_path = "quad.gltf";
    input.naming_service = observer_ptr { &naming };
    auto adapter = adapters::GltfAdapter {};
    auto read = Read {
      .parse = adapter.Parse(
        std::as_bytes(std::span(document.data(), document.size())), input),
      .items = {},
    };
    if (!read.parse.success) {
      return read;
    }
    auto sink = GeometrySink {};
    if (adapter.BuildWorkItems(adapters::GeometryWorkTag {}, sink, input)
          .success) {
      read.items = std::move(sink.items);
    }
    return read;
  }

  //! A glTF file whose geometry uses meshopt compression imports the same
  //! positions, triangles and normals as its uncompressed twin.
  NOLINT_TEST(GltfMeshoptTest, CompressedGeometryReadsAsItsPlainTwin)
  {
    const auto plain = ReadDocument(PlainDocument());
    const auto compressed = ReadDocument(CompressedDocument(false));

    ASSERT_TRUE(plain.parse.success);
    ASSERT_TRUE(compressed.parse.success);
    EXPECT_FALSE(HasDiagnosticCode(
      compressed.parse.diagnostics, "import.source_extension_required"));
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
    const auto read = ReadDocument(CompressedDocument(true));

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(
      HasDiagnosticCode(read.parse.diagnostics, "gltf.meshopt_decode_failed"));
  }

} // namespace
} // namespace oxygen::content::import::test
