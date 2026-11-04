//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/gltf/GltfDocumentChecks.cpp,
//   Import/Internal/ModelDependency.cpp,
//   Import/Internal/ModelTextureSource.h,
//   Import/Internal/gltf/GltfAdapter.cpp,
//   Import/Internal/fbx/FbxAdapter.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/ModelDependency.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/GltfDocuments.h>
#include <Oxygen/Cooker/test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {

  using nlohmann::json;
  using oxygen::cooker::test::ContentMountTable;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::ModelPath;
  using oxygen::cooker::test::ModelVirtualPath;
  using oxygen::cooker::test::ReadText;
  using oxygen::cooker::test::SourceMountPath;
  using oxygen::cooker::test::WriteBytes;
  using oxygen::cooker::test::WriteText;

  constexpr auto kComponentFloat = 5126;
  constexpr auto kComponentByte = 5121;
  // The largest value cgltf's JSON numbers carry.
  constexpr auto kHugeNumber = std::numeric_limits<int64_t>::max();
  constexpr auto kTrianglePositions = std::to_array<float>({
    0.0F,
    0.0F,
    0.0F, //
    1.0F,
    0.0F,
    0.0F, //
    0.0F,
    1.0F,
    0.0F, //
  });
  constexpr auto kTriangleBytes = sizeof(kTrianglePositions);

  [[nodiscard]] auto TriangleBuffer() -> json
  {
    const auto bytes = std::as_bytes(std::span(kTrianglePositions));
    auto raw = std::vector<unsigned char> {};
    AppendAligned(raw, bytes);
    return {
      { "byteLength", kTriangleBytes },
      { "uri", DataUri(raw) },
    };
  }

  //! One triangle, valid: tests change what they make hostile.
  [[nodiscard]] auto TriangleDocument() -> json
  {
    return {
      { "asset", { { "version", "2.0" } } },
      { "buffers", json::array({ TriangleBuffer() }) },
      {
        "bufferViews",
        json::array({
          { { "buffer", 0 }, { "byteLength", kTriangleBytes } },
        }),
      },
      {
        "accessors",
        json::array({
          {
            { "bufferView", 0 },
            { "componentType", kComponentFloat },
            { "type", "VEC3" },
            { "count", 3 },
          },
        }),
      },
      { "materials", json::array({ { { "name", "Paint" } } }) },
      {
        "meshes",
        json::array({
          {
            { "name", "Triangle" },
            {
              "primitives",
              json::array({
                {
                  { "attributes", { { "POSITION", 0 } } },
                  { "material", 0 },
                },
              }),
            },
          },
        }),
      },
      {
        "nodes",
        json::array({
          { { "name", "Triangle" }, { "mesh", 0 } },
        }),
      },
      { "scenes", json::array({ { { "nodes", json::array({ 0 }) } } }) },
      { "scene", 0 },
    };
  }

  //! `depth` nodes, each the child of the last; the deepest holds the mesh.
  [[nodiscard]] auto ChainDocument(const uint32_t depth) -> json
  {
    auto document = TriangleDocument();
    auto nodes = json::array();
    for (uint32_t level = 0; level < depth; ++level) {
      auto node = json { { "name", "Level" + std::to_string(level) } };
      if (level + 1U < depth) {
        node["children"] = json::array({ level + 1U });
      } else {
        node["mesh"] = 0;
      }
      nodes.push_back(std::move(node));
    }
    document["nodes"] = std::move(nodes);
    return document;
  }

  //! A material sampling the image at `uri`.
  [[nodiscard]] auto TexturedDocument(const std::string_view uri) -> std::string
  {
    return json {
      { "asset", { { "version", "2.0" } } },
      { "images", json::array({ { { "uri", uri } } }) },
      { "textures", json::array({ { { "source", 0 } } }) },
      {
        "materials",
        json::array({
          {
            {
              "pbrMetallicRoughness",
              { { "baseColorTexture", { { "index", 0 } } } },
            },
          },
        }),
      },
    }
      .dump();
  }

  //! The FBX triangle under `depth - 1` empty nodes, so `depth` levels in all.
  [[nodiscard]] auto FbxChainText(const uint32_t depth) -> std::string
  {
    constexpr auto kFirstId = uint32_t { 2000 };
    auto text = ReadText(ModelPath("static_scalar_triangle.fbx"));
    auto models = std::string {};
    auto links = std::string { "C: \"OO\"," + std::to_string(kFirstId) + ",0" };
    for (uint32_t level = 0; level + 1U < depth; ++level) {
      const auto id = std::to_string(kFirstId + level);
      models += "    Model: ";
      models += id;
      models += ", \"Model::Level";
      models += id;
      models += "\", \"Null\" {\n        Version: 232\n    }\n";
      if (level != 0U) {
        links += "\n    C: \"OO\"," + id + ","
          + std::to_string(kFirstId + level - 1U);
      }
    }
    links += "\n    C: \"OO\",1002," + std::to_string(kFirstId + depth - 2U);
    const std::string model = "    Model: 1002,";
    const auto model_at = text.find(model);
    const std::string link = "C: \"OO\",1002,0";
    const auto link_at = text.find(link);
    EXPECT_NE(model_at, std::string::npos);
    EXPECT_NE(link_at, std::string::npos);
    text.replace(link_at, link.size(), links);
    text.insert(model_at, models);
    return text;
  }

  //! True when a diagnostic with `code` names the object at `object_path`.
  [[nodiscard]] auto ReportedAt(
    const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code, const std::string_view object_path) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [&](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.code == code && diagnostic.object_path == object_path;
      });
  }

  [[nodiscard]] auto ParseBytes(adapters::GltfAdapter& adapter,
    const std::string& document, const adapters::AdapterInput& input)
    -> adapters::GltfAdapter::ParseResult
  {
    return adapter.Parse(
      std::as_bytes(std::span(document.data(), document.size())), input);
  }

  class ModelUntrustedInputTest : public ModelImportTestBase {
  protected:
    [[nodiscard]] static auto RequestFor(const std::filesystem::path& source)
      -> ImportRequest
    {
      auto request = ImportRequest {};
      request.source_path = source;
      request.asset_virtual_path = ModelVirtualPath(source);
      request.source_mount_path = SourceMountPath(source);
      request.cooked_root = source.parent_path() / "cooked";
      return request;
    }

    //! A source file to write: its name, which gives its format, and its text.
    struct SourceText final {
      std::string_view file_name;
      std::string_view text;
    };

    //! Writes `source` to the fixture's folder and imports it.
    [[nodiscard]] auto ImportText(const SourceText& source) -> ImportRunResult
    {
      const auto path = MakeTempDir("source") / source.file_name;
      WriteText(path, source.text);
      return RunImport(RequestFor(path));
    }
  };

  //! The review's input: the required size wraps around to 8 bytes of a view
  //! of 12, so `cgltf_validate` passes it and a vector of that count follows.
  NOLINT_TEST(GltfDeclaredExtentsTest, CountThatWrapsTheRequiredSizeIsRefused)
  {
    auto document = TriangleDocument();
    document["bufferViews"][0]["byteLength"] = 12;
    document["accessors"][0]["count"] = 1537228672809129302LL;

    const auto read = ReadGltfDocument(document.dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(ReportedAt(read.parse.diagnostics,
      import_codes::kGltfValidationFailed, "/accessors/0"))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  NOLINT_TEST(GltfDeclaredExtentsTest, AccessorOffsetPastItsViewIsRefused)
  {
    auto document = TriangleDocument();
    document["accessors"][0]["byteOffset"] = kHugeNumber;

    const auto read = ReadGltfDocument(document.dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(ReportedAt(read.parse.diagnostics,
      import_codes::kGltfValidationFailed, "/accessors/0"))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  NOLINT_TEST(GltfDeclaredExtentsTest, ViewPastItsBufferIsRefused)
  {
    auto document = TriangleDocument();
    document["bufferViews"][0]["byteOffset"] = kHugeNumber;

    const auto read = ReadGltfDocument(document.dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(ReportedAt(read.parse.diagnostics,
      import_codes::kGltfValidationFailed, "/bufferViews/0"))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  //! An accessor with no view reads as zeros, so only the limit bounds it: this
  //! tiny file would otherwise allocate 48 GB.
  NOLINT_TEST(GltfDeclaredExtentsTest, AccessorWithNoViewPastTheLimitIsRefused)
  {
    auto document = TriangleDocument();
    document["accessors"][0].erase("bufferView");
    document["accessors"][0]["count"] = 4000000000LL;

    const auto read = ReadGltfDocument(document.dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(ReportedAt(read.parse.diagnostics,
      import_codes::kGltfValidationFailed, "/accessors/0"))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  //! Nothing in the file backs what a compressed view decodes to.
  NOLINT_TEST(GltfDeclaredExtentsTest, CompressedViewPastTheLimitIsRefused)
  {
    constexpr auto kDecoded = int64_t { 8 } * 1024 * 1024 * 1024;
    auto document = TriangleDocument();
    document["extensionsUsed"] = json::array({ "EXT_meshopt_compression" });
    document["buffers"] = json::array({
      { { "byteLength", kDecoded } },
      TriangleBuffer(),
    });
    document["bufferViews"][0] = {
      { "buffer", 0 },
      { "byteLength", kDecoded },
      {
        "extensions",
        {
          {
            "EXT_meshopt_compression",
            {
              { "buffer", 1 },
              { "byteLength", 4 },
              { "byteStride", 4 },
              { "count", kDecoded / 4 },
              { "mode", "ATTRIBUTES" },
            },
          },
        },
      },
    };

    const auto read = ReadGltfDocument(document.dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(ReportedAt(read.parse.diagnostics,
      import_codes::kGltfValidationFailed, "/bufferViews/0"))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  //! cgltf reads sparse indices before it checks their view.
  NOLINT_TEST(GltfDeclaredExtentsTest, SparseCountPastItsAccessorIsRefused)
  {
    auto document = TriangleDocument();
    document["accessors"][0]["sparse"] = {
      { "count", 1000 },
      {
        "indices",
        { { "bufferView", 0 }, { "componentType", kComponentByte } },
      },
      { "values", { { "bufferView", 0 } } },
    };

    const auto read = ReadGltfDocument(document.dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(ReportedAt(read.parse.diagnostics,
      import_codes::kGltfValidationFailed, "/accessors/0"))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  //! cgltf measures sparse values by the element but steps through them by the
  //! accessor's stride: three values 16 apart leave a view of 36 bytes.
  NOLINT_TEST(GltfDeclaredExtentsTest, SparseValuesStepByTheAccessorStride)
  {
    constexpr auto kStride = 16;
    constexpr auto kStridedBytes = (kStride * 2) + 12;
    const auto zeros = std::vector<unsigned char>(kStridedBytes, 0);
    auto document = TriangleDocument();
    document["buffers"][0] = {
      { "byteLength", kStridedBytes },
      { "uri", DataUri(zeros) },
    };
    document["bufferViews"] = json::array({
      {
        { "buffer", 0 },
        { "byteLength", kStridedBytes },
        { "byteStride", kStride },
      },
      { { "buffer", 0 }, { "byteLength", 36 } },
    });
    document["accessors"][0]["sparse"] = {
      { "count", 3 },
      {
        "indices",
        { { "bufferView", 1 }, { "componentType", kComponentByte } },
      },
      { "values", { { "bufferView", 1 } } },
    };

    const auto read = ReadGltfDocument(document.dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(ReportedAt(read.parse.diagnostics,
      import_codes::kGltfValidationFailed, "/accessors/0"))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  NOLINT_TEST(GltfDeclaredExtentsTest, ValidSparseAccessorReadsItsValues)
  {
    const auto value = std::to_array<float>({ 1.0F, 0.0F, 0.0F });
    const auto index = std::to_array<unsigned char>({ 1 });
    auto bytes = std::vector<unsigned char> {};
    AppendAligned(bytes, std::as_bytes(std::span(value)));
    const auto index_at = AppendAligned(bytes, std::as_bytes(std::span(index)));
    auto document = TriangleDocument();
    document["buffers"][0]
      = { { "byteLength", bytes.size() }, { "uri", DataUri(bytes) } };
    document["bufferViews"] = json::array({
      { { "buffer", 0 }, { "byteLength", sizeof(value) } },
      {
        { "buffer", 0 },
        { "byteOffset", index_at },
        { "byteLength", index.size() },
      },
    });
    document["accessors"][0].erase("bufferView");
    document["accessors"][0]["sparse"] = {
      { "count", 1 },
      {
        "indices",
        { { "bufferView", 1 }, { "componentType", kComponentByte } },
      },
      { "values", { { "bufferView", 0 } } },
    };

    const auto read = ReadGltfDocument(document.dump());

    ASSERT_TRUE(read.parse.success)
      << DiagnosticSummary(read.parse.diagnostics);
    ASSERT_EQ(read.items.size(), 1U)
      << DiagnosticSummary(read.build.diagnostics);
    const auto positions
      = read.items.front().lods.front().source.streams.positions;
    ASSERT_EQ(positions.size(), 3U);
    EXPECT_FLOAT_EQ(positions[0].x, 0.0F);
    EXPECT_FLOAT_EQ(positions[1].x, 1.0F);
  }

  //! Primitives sharing one accessor each take a copy of it, so the file does
  //! not bound what a mesh asks for: two of 1.2 GB pass the limit one by one.
  NOLINT_TEST(
    GltfDeclaredExtentsTest, MeshPastTheLimitAcrossPrimitivesIsDropped)
  {
    auto document = TriangleDocument();
    document["accessors"][0].erase("bufferView");
    document["accessors"][0]["count"] = 100000000;
    const auto primitive = document["meshes"][0]["primitives"][0];
    document["meshes"][0]["primitives"] = json::array({ primitive, primitive });

    const auto read = ReadGltfDocument(document.dump());

    ASSERT_TRUE(read.parse.success)
      << DiagnosticSummary(read.parse.diagnostics);
    EXPECT_TRUE(read.items.empty());
    EXPECT_TRUE(
      HasDiagnosticCode(read.build.diagnostics, import_codes::kMeshDropped))
      << DiagnosticSummary(read.build.diagnostics);
  }

  NOLINT_TEST(
    GltfDracoTest, PrimitiveWithOnlyCompressedDataIsSkippedWithAWarning)
  {
    auto document = TriangleDocument();
    document["extensionsUsed"] = json::array({ "KHR_draco_mesh_compression" });
    document["accessors"][0].erase("bufferView");
    document["meshes"][0]["primitives"][0]["extensions"] = {
      {
        "KHR_draco_mesh_compression",
        { { "bufferView", 0 }, { "attributes", { { "POSITION", 0 } } } },
      },
    };

    const auto read = ReadGltfDocument(document.dump());

    ASSERT_TRUE(read.parse.success)
      << DiagnosticSummary(read.parse.diagnostics);
    EXPECT_TRUE(read.items.empty());
    EXPECT_TRUE(HasDiagnosticCode(
      read.build.diagnostics, import_codes::kGltfPrimitiveDracoSkipped))
      << DiagnosticSummary(read.build.diagnostics);
  }

  NOLINT_TEST(GltfDracoTest, PrimitiveWithFallbackDataImportsFromIt)
  {
    auto document = TriangleDocument();
    document["extensionsUsed"] = json::array({ "KHR_draco_mesh_compression" });
    document["meshes"][0]["primitives"][0]["extensions"] = {
      {
        "KHR_draco_mesh_compression",
        { { "bufferView", 0 }, { "attributes", { { "POSITION", 0 } } } },
      },
    };

    const auto read = ReadGltfDocument(document.dump());

    ASSERT_TRUE(read.parse.success)
      << DiagnosticSummary(read.parse.diagnostics);
    EXPECT_EQ(read.items.size(), 1U);
    EXPECT_FALSE(HasDiagnosticCode(
      read.build.diagnostics, import_codes::kGltfPrimitiveDracoSkipped));
  }

  //! The vertices name joint 1 of a skin with one joint, though the inverse
  //! bind accessor has room for two.
  NOLINT_TEST(GltfSkinTest, JointPastTheSkinDropsTheSkin)
  {
    const auto joints = std::to_array<unsigned char>({
      1,
      0,
      0,
      0, //
      1,
      0,
      0,
      0, //
      1,
      0,
      0,
      0, //
    });
    const auto weights = std::array<float, 12> {
      1.0F,
      0.0F,
      0.0F,
      0.0F, //
      1.0F,
      0.0F,
      0.0F,
      0.0F, //
      1.0F,
      0.0F,
      0.0F,
      0.0F, //
    };
    const auto bind = std::array { glm::mat4 { 1.0F }, glm::mat4 { 1.0F } };
    auto bytes = std::vector<unsigned char> {};
    const auto positions_at
      = AppendAligned(bytes, std::as_bytes(std::span(kTrianglePositions)));
    const auto joints_at
      = AppendAligned(bytes, std::as_bytes(std::span(joints)));
    const auto weights_at
      = AppendAligned(bytes, std::as_bytes(std::span(weights)));
    const auto bind_at = AppendAligned(bytes, std::as_bytes(std::span(bind)));
    const auto view
      = [](const std::size_t offset, const std::size_t size) -> json {
      return json {
        { "buffer", 0 },
        { "byteOffset", offset },
        { "byteLength", size },
      };
    };
    const auto accessor
      = [](const int index, const int component, const std::string_view type,
          const int count) -> json {
      return json {
        { "bufferView", index },
        { "componentType", component },
        { "type", type },
        { "count", count },
      };
    };
    auto document = TriangleDocument();
    document["buffers"][0]
      = { { "byteLength", bytes.size() }, { "uri", DataUri(bytes) } };
    document["bufferViews"] = json::array({
      view(positions_at, kTriangleBytes),
      view(joints_at, sizeof(joints)),
      view(weights_at, sizeof(weights)),
      view(bind_at, sizeof(bind)),
    });
    document["accessors"] = json::array({
      accessor(0, kComponentFloat, "VEC3", 3),
      accessor(1, kComponentByte, "VEC4", 3),
      accessor(2, kComponentFloat, "VEC4", 3),
      accessor(3, kComponentFloat, "MAT4", 2),
    });
    document["meshes"][0]["primitives"][0]["attributes"] = {
      { "POSITION", 0 },
      { "JOINTS_0", 1 },
      { "WEIGHTS_0", 2 },
    };
    document["skins"] = json::array({
      { { "joints", json::array({ 0 }) }, { "inverseBindMatrices", 3 } },
    });
    document["nodes"][0]["skin"] = 0;

    const auto read = ReadGltfDocument(document.dump());

    ASSERT_TRUE(read.parse.success)
      << DiagnosticSummary(read.parse.diagnostics);
    EXPECT_TRUE(HasDiagnosticCode(
      read.build.diagnostics, import_codes::kMeshSkinningBuffersMismatch))
      << DiagnosticSummary(read.build.diagnostics);
  }

  //! Reports and hash inputs are JSON, which throws on these bytes after the
  //! cooked files are written; the import repairs them and completes.
  NOLINT_TEST_F(ModelUntrustedInputTest, GltfTextThatIsNotUtf8IsRepaired)
  {
    constexpr auto kPlaceholder = std::string_view { "BAD" };
    for (const auto& bad :
      { std::string_view { "\xE9" }, std::string_view { "\\ud800" } }) {
      auto document = TriangleDocument();
      document["nodes"][0]["name"] = kPlaceholder;
      document["materials"][0]["name"] = kPlaceholder;
      auto text = document.dump();
      for (auto at = text.find(kPlaceholder); at != std::string::npos;
        at = text.find(kPlaceholder, at + bad.size())) {
        text.replace(at, kPlaceholder.size(), bad);
      }

      const auto run
        = ImportText({ .file_name = "repaired.gltf", .text = text });

      EXPECT_TRUE(run.report.success)
        << DiagnosticSummary(run.report.diagnostics);
      EXPECT_TRUE(HasDiagnosticCode(
        run.report.diagnostics, import_codes::kGltfTextNotUtf8))
        << DiagnosticSummary(run.report.diagnostics);
    }
  }

  //! JSON may spell any character as an escape; cgltf keeps the spelling.
  NOLINT_TEST(GltfTextTest, EscapedUriNamesTheFileItSpells)
  {
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "document.gltf";
    input.request.source_path = "authoring/document.gltf";
    constexpr auto kPlaceholder = std::string_view { "PLACEHOLDER" };
    auto escaped = TexturedDocument(kPlaceholder);
    escaped.replace(escaped.find(kPlaceholder), kPlaceholder.size(),
      "textures\\/caf\\u00e9.png");
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(ParseBytes(adapter, escaped, input).success);

    const auto prepared = adapter.PrepareTextures(input);

    ASSERT_EQ(prepared.sources.size(), 1U);
    EXPECT_EQ(prepared.sources.front().source_path,
      std::filesystem::path(u8"authoring/textures/café.png"));
  }

  //! Some JSON writers escape every `/`, in an embedded buffer's URI too.
  NOLINT_TEST(GltfTextTest, EscapedDataUriLoads)
  {
    auto text = TriangleDocument().dump();
    const std::string plain = "data:application/octet-stream";
    text.replace(
      text.find(plain), plain.size(), "data:application\\/octet-stream");

    const auto read = ReadGltfDocument(text);

    ASSERT_TRUE(read.parse.success)
      << DiagnosticSummary(read.parse.diagnostics);
    EXPECT_EQ(read.items.size(), 1U);
  }

  //! A percent escape can spell bytes no path holds: the image is missing, the
  //! import goes on.
  NOLINT_TEST(GltfTextTest, UriThatDecodesToNonUtf8NamesNoFile)
  {
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "document.gltf";
    input.request.source_path = "authoring/document.gltf";
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(
      ParseBytes(adapter, TexturedDocument("a%E9.png"), input).success);

    const auto prepared = adapter.PrepareTextures(input);

    ASSERT_TRUE(prepared.success);
    ASSERT_EQ(prepared.sources.size(), 1U);
    EXPECT_TRUE(prepared.sources.front().source_path.empty());
  }

  NOLINT_TEST(GltfNodeDepthTest, HierarchyPastTheLimitIsRefused)
  {
    const auto read
      = ReadGltfDocument(ChainDocument(kMaxModelNodeDepth + 1U).dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(HasDiagnosticCode(
      read.parse.diagnostics, import_codes::kModelNodeDepthExceeded))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  //! Far past any stack the recursive import stages have.
  NOLINT_TEST(
    GltfNodeDepthTest, HierarchyFarPastTheLimitIsRefusedWithoutRecursion)
  {
    constexpr auto kDeep = uint32_t { 200000 };

    const auto read = ReadGltfDocument(ChainDocument(kDeep).dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(HasDiagnosticCode(
      read.parse.diagnostics, import_codes::kModelNodeDepthExceeded))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  NOLINT_TEST(GltfNodeDepthTest, NodeThatIsItsOwnDescendantIsRefused)
  {
    auto document = TriangleDocument();
    document["nodes"] = json::array({
      { { "name", "A" }, { "children", json::array({ 1 }) } },
      { { "name", "B" }, { "children", json::array({ 0 }) } },
      { { "name", "Root" } },
    });
    // cgltf itself refuses a scene root that has a parent.
    document["scenes"][0]["nodes"] = json::array({ 2 });

    const auto read = ReadGltfDocument(document.dump());

    EXPECT_FALSE(read.parse.success);
    EXPECT_TRUE(HasDiagnosticCode(
      read.parse.diagnostics, import_codes::kGltfValidationFailed))
      << DiagnosticSummary(read.parse.diagnostics);
  }

  //! The limit is one the recursive stages hold in a debug build.
  NOLINT_TEST_F(ModelUntrustedInputTest, GltfHierarchyAtTheLimitImports)
  {
    const auto document = ChainDocument(kMaxModelNodeDepth).dump();
    const auto run = ImportText({ .file_name = "deep.gltf", .text = document });

    EXPECT_TRUE(run.report.success)
      << DiagnosticSummary(run.report.diagnostics);
  }

  NOLINT_TEST_F(ModelUntrustedInputTest, FbxHierarchyAtTheLimitImports)
  {
    const auto text = FbxChainText(kMaxModelNodeDepth);
    const auto run = ImportText({ .file_name = "deep.fbx", .text = text });

    EXPECT_TRUE(run.report.success)
      << DiagnosticSummary(run.report.diagnostics);
  }

  NOLINT_TEST_F(ModelUntrustedInputTest, FbxHierarchyPastTheLimitIsRefused)
  {
    const auto text = FbxChainText(kMaxModelNodeDepth + 1U);
    const auto run = ImportText({ .file_name = "deep.fbx", .text = text });

    EXPECT_FALSE(run.report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      run.report.diagnostics, import_codes::kModelNodeDepthExceeded))
      << DiagnosticSummary(run.report.diagnostics);
  }

  NOLINT_TEST(ModelDependencyTest, NetworkPathsAreRefusedFromTheirTextAlone)
  {
    auto request = ImportRequest {};
    request.source_path = "H:/project/model.fbx";

    for (const auto* path : {
           R"(\\evil\share\t.png)",
           "//evil/share/t.png",
           R"(\\?\UNC\evil\share\t.png)",
           R"(\\.\pipe\t.png)",
         }) {
      EXPECT_TRUE(internal::RefusedModelDependency(request, path).has_value())
        << path;
    }
    EXPECT_FALSE(internal::RefusedModelDependency(request, "H:/other/t.png"));
    EXPECT_FALSE(internal::RefusedModelDependency(request, "../t.png"));
    EXPECT_FALSE(internal::RefusedModelDependency(request, {}));
  }

  NOLINT_TEST(ModelDependencyTest, ModelsOwnServerIsNotRefused)
  {
    auto request = ImportRequest {};
    request.source_path = R"(\\nas\project\model.fbx)";

    EXPECT_FALSE(
      internal::RefusedModelDependency(request, "\\\\nas\\project\\t.png"));
    EXPECT_TRUE(
      internal::RefusedModelDependency(request, "\\\\evil\\share\\t.png")
        .has_value());
  }

  NOLINT_TEST(
    ModelDependencyTest, ReportPathIsWholeWhenNoRelativePathLeadsThere)
  {
    EXPECT_EQ(
      internal::ModelDependencyReportPath("H:/m/tex/t.png", "H:/m/model.fbx"),
      "tex/t.png");
    EXPECT_EQ(
      internal::ModelDependencyReportPath("D:/tex/t.png", "H:/m/model.fbx"),
      "D:/tex/t.png");
  }

  NOLINT_TEST_F(
    ModelUntrustedInputTest, GltfTextureOutsideTheMountIsLookedForByName)
  {
    const auto root = MakeTempDir("mounted");
    const auto project = root / "project";
    std::filesystem::create_directories(project);
    WriteText(root / "secret" / "t.png", "not read");
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model.gltf";
    input.request.source_path = project / "model.gltf";
    input.request.source_mounts = ContentMountTable(project);
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(
      ParseBytes(adapter, TexturedDocument("../secret/t.png"), input).success);

    const auto prepared = adapter.PrepareTextures(input);

    ASSERT_EQ(prepared.sources.size(), 1U);
    EXPECT_TRUE(HasDiagnosticCode(
      prepared.diagnostics, import_codes::kSourceNonlocalDependency))
      << DiagnosticSummary(prepared.diagnostics);
    EXPECT_EQ(prepared.sources.front().source_path, project / "t.png");

    auto sink = TextureSink {};
    ASSERT_TRUE(
      adapter.BuildWorkItems(adapters::TextureWorkTag {}, sink, input).success);
    ASSERT_EQ(sink.items.size(), 1U);
    EXPECT_EQ(sink.items.front().source_path, project / "t.png");
    // The folders beside the model's are outside the mount, too.
    EXPECT_FALSE(sink.items.front().fallback_source_paths.empty());
    for (const auto& fallback : sink.items.front().fallback_source_paths) {
      EXPECT_FALSE(internal::RefusedModelDependency(input.request, fallback))
        << fallback.string();
    }
  }

  NOLINT_TEST_F(ModelUntrustedInputTest, GltfTextureInTheMountIsRead)
  {
    const auto project = MakeTempDir("mounted");
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model.gltf";
    input.request.source_path = project / "model.gltf";
    input.request.source_mounts = ContentMountTable(project);
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(
      ParseBytes(adapter, TexturedDocument("textures/t.png"), input).success);

    const auto prepared = adapter.PrepareTextures(input);

    ASSERT_EQ(prepared.sources.size(), 1U);
    EXPECT_TRUE(prepared.diagnostics.empty())
      << DiagnosticSummary(prepared.diagnostics);
    EXPECT_EQ(
      prepared.sources.front().source_path, project / "textures" / "t.png");
  }

  //! A file from elsewhere names a server: nothing may reach for it, even to
  //! ask whether it exists.
  NOLINT_TEST_F(ModelUntrustedInputTest, FbxTextureOnAServerIsLookedForByName)
  {
    const auto folder = MakeTempDir("fbx");
    auto text = ReadText(ModelPath("static_normal_triangle.fbx"));
    const std::string authored = "static_tangent_normal.png";
    for (auto at = text.find(authored); at != std::string::npos;
      at = text.find(authored, at)) {
      text.replace(at, authored.size(), "//evil/share/t.png");
    }
    const auto source = folder / "model.fbx";
    WriteText(source, text);
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model.fbx";
    input.request.source_path = source;
    auto adapter = adapters::FbxAdapter {};
    ASSERT_TRUE(adapter.Parse(source, input).success);

    const auto prepared = adapter.PrepareTextures(input);

    ASSERT_FALSE(prepared.sources.empty());
    EXPECT_TRUE(HasDiagnosticCode(
      prepared.diagnostics, import_codes::kSourceNonlocalDependency))
      << DiagnosticSummary(prepared.diagnostics);
    for (const auto& texture : prepared.sources) {
      EXPECT_EQ(texture.source_path, folder / "t.png");
    }
  }

  //! A document whose buffer lies outside the mount has no geometry to read, so
  //! it is refused; without mounts the file is the user's own.
  NOLINT_TEST_F(ModelUntrustedInputTest, GltfBufferOutsideTheMountIsRefused)
  {
    const auto root = MakeTempDir("mounted");
    const auto project = root / "project";
    WriteBytes(
      root / "secret" / "b.bin", std::as_bytes(std::span(kTrianglePositions)));
    auto document = TriangleDocument();
    document["buffers"][0]["uri"] = "../secret/b.bin";
    const auto source = project / "model.gltf";
    WriteText(source, document.dump());
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = "model.gltf";
    input.request.source_path = source;

    {
      auto adapter = adapters::GltfAdapter {};
      const auto parsed = adapter.Parse(source, input);
      EXPECT_TRUE(parsed.success) << DiagnosticSummary(parsed.diagnostics);
    }

    input.request.source_mounts = ContentMountTable(project);
    auto adapter = adapters::GltfAdapter {};
    const auto parsed = adapter.Parse(source, input);
    EXPECT_FALSE(parsed.success);
    EXPECT_TRUE(HasDiagnosticCode(
      parsed.diagnostics, import_codes::kSourceNonlocalDependency))
      << DiagnosticSummary(parsed.diagnostics);
  }

} // namespace
} // namespace oxygen::content::import::test
