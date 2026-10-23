//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/fbx/FbxAdapter.cpp,
//   Import/Internal/Pipelines/MeshBuildPipeline.cpp

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace oxygen::content::import;
namespace data = oxygen::data;

//! One cooked geometry: its LODs, default policy, surfaces and bounds.
struct CookedLodGeometry {
  uint32_t lod_count = 0;
  data::pak::geometry::LodPolicyRecord lod_policy;
  data::SurfaceInventory surfaces;
  glm::vec3 bounds_max {};
};

//! What an FBX import produced.
struct LodImport {
  bool success = false;
  std::vector<ImportDiagnostic> diagnostics;
  size_t node_count = 0;
  size_t renderable_count = 0;
  std::vector<CookedLodGeometry> geometries;
};

//! A unit square of two triangles, one on each of the node's two materials.
auto Square(const uint32_t id, const std::string& name) -> std::string
{
  return "    Geometry: " + std::to_string(id) + ", \"Geometry::" + name
    + "\", \"Mesh\" {\n"
      "        Vertices: *12 { a: 0,0,0,1,0,0,1,1,0,0,1,0 }\n"
      "        PolygonVertexIndex: *6 { a: 0,1,-3,0,2,-4 }\n"
      "        LayerElementMaterial: 0 {\n"
      "            Version: 101\n"
      "            Name: \"\"\n"
      "            MappingInformationType: \"ByPolygon\"\n"
      "            ReferenceInformationType: \"IndexToDirect\"\n"
      "            Materials: *2 { a: 0,1 }\n"
      "        }\n"
      "        Layer: 0 {\n"
      "            Version: 100\n"
      "            LayerElement:  {\n"
      "                Type: \"LayerElementMaterial\"\n"
      "                TypedIndex: 0\n"
      "            }\n"
      "        }\n"
      "    }\n";
}

//! A model node of a type ("Mesh" or "Null"), translated along FBX Y.
auto Model(const uint32_t id, const std::string& name, const std::string& type,
  const int y = 0) -> std::string
{
  return "    Model: " + std::to_string(id) + ", \"Model::" + name + "\", \""
    + type
    + "\" {\n"
      "        Version: 232\n"
      "        Properties70: {\n"
      "            P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,"
    + std::to_string(y)
    + ",0\n"
      "        }\n"
      "    }\n";
}

auto Connect(const uint32_t child, const uint32_t parent) -> std::string
{
  return "    C: \"OO\"," + std::to_string(child) + "," + std::to_string(parent)
    + "\n";
}

class FbxLodGroupsTest : public test::ModelImportTestBase {
protected:
  // Material ids shared by every scene.
  static constexpr uint32_t kPaint = 9001;
  static constexpr uint32_t kGlass = 9002;

  //! An FBX document with the fixture's settings and these objects.
  static auto Document(
    const std::string& objects, const std::string& connections) -> std::string
  {
    std::ifstream input(
      oxygen::cooker::test::ModelPath("static_scalar_triangle.fbx"));
    const auto fixture = std::string { std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>() };
    return fixture.substr(0, fixture.find("Objects: {")) + "Objects: {\n"
      + "    Material: " + std::to_string(kPaint)
      + ", \"Material::Paint\", \"\" { }\n" + "    Material: "
      + std::to_string(kGlass) + ", \"Material::Glass\", \"\" { }\n" + objects
      + "}\nConnections: {\n" + connections + "}\n";
  }

  auto Import(const std::string& source) -> LodImport
  {
    const auto root = MakeTempDir("fbx_lod_"
      + std::string(
        testing::UnitTest::GetInstance()->current_test_info()->name()));
    auto request = ImportRequest {};
    request.source_path = root / "model.fbx";
    request.asset_virtual_path
      = oxygen::cooker::test::ModelVirtualPath(request.source_path);
    request.cooked_root = root / "cooked";
    {
      std::ofstream output(request.source_path, std::ios::binary);
      output << source;
    }
    const auto imported = RunImport(std::move(request));
    auto result = LodImport {
      .success = imported.report.success,
      .diagnostics = imported.report.diagnostics,
      .node_count = 0,
      .renderable_count = 0,
      .geometries = {},
    };
    if (!result.success) {
      for (const auto& diagnostic : result.diagnostics) {
        ADD_FAILURE() << diagnostic.code << ": " << diagnostic.message;
      }
      return result;
    }
    const auto scene = LoadSceneReadback(imported.report);
    result.node_count = scene.nodes.size();
    result.renderable_count = scene.renderables.size();
    const auto inspection = LoadInspection(imported.report.cooked_root);
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type
        != static_cast<uint8_t>(data::AssetType::kGeometry)) {
        continue;
      }
      const auto path = imported.report.cooked_root / asset.descriptor_relpath;
      auto header = data::pak::geometry::GeometryAssetDesc {};
      {
        std::ifstream bytes(path, std::ios::binary);
        bytes.read(reinterpret_cast<char*>(&header), sizeof(header));
      }
      oxygen::serio::FileStream<> stream(path, std::ios::in);
      oxygen::serio::Reader reader(stream);
      const auto geometry = oxygen::content::loaders::LoadGeometryAsset(
        oxygen::content::LoaderContext {
          .current_asset_key = asset.key,
          .desc_reader = &reader,
          .work_offline = true,
          .parse_only = true,
        });
      result.geometries.push_back({
        .lod_count = header.lod_count,
        .lod_policy = header.default_lod_policy,
        .surfaces = geometry->Surfaces(),
        .bounds_max = geometry->BoundingBoxMax(),
      });
    }
    return result;
  }

  //! The LODs each surface binds a submesh in.
  static auto LodsOf(const data::Surface& surface) -> std::set<uint32_t>
  {
    auto lods = std::set<uint32_t> {};
    for (const auto& binding : surface.bindings) {
      lods.insert(binding.lod_index.get());
    }
    return lods;
  }

  static auto Count(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string& code) -> size_t
  {
    return static_cast<size_t>(
      std::ranges::count(diagnostics, code, &ImportDiagnostic::code));
  }
};

/*!
 An FBX LodGroup imports as one geometry with a LOD per child, shown by the
 group's node: the children import as no nodes, each material is one surface
 bound in every LOD although LOD1 lists them in the other order, and each
 level's transform is baked into its LOD.
*/
NOLINT_TEST_F(FbxLodGroupsTest, LodGroupIsOneGeometryWithALodPerChild)
{
  // Arrange
  const auto source = Document(Square(2001, "Lod0") + Square(2002, "Lod1")
      + Model(3000, "Car", "Null") + Model(3001, "Car_LOD0", "Mesh")
      + Model(3002, "Car_LOD1", "Mesh", 5)
      + "    NodeAttribute: 3010, \"NodeAttribute::Car\", \"LodGroup\" {\n"
        "        TypeFlags: \"Null\"\n"
        "    }\n",
    Connect(3000, 0) + Connect(3010, 3000) + Connect(3001, 3000)
      + Connect(3002, 3000) + Connect(2001, 3001) + Connect(kPaint, 3001)
      + Connect(kGlass, 3001) + Connect(2002, 3002) + Connect(kGlass, 3002)
      + Connect(kPaint, 3002));

  // Act
  const auto imported = Import(source);

  // Assert
  ASSERT_TRUE(imported.success);
  ASSERT_EQ(imported.geometries.size(), 1U);
  const auto& geometry = imported.geometries.front();
  EXPECT_EQ(geometry.lod_count, 2U);
  ASSERT_EQ(geometry.surfaces.surfaces.size(), 2U);
  for (const auto& surface : geometry.surfaces.surfaces) {
    EXPECT_EQ(LodsOf(surface), (std::set<uint32_t> { 0, 1 }))
      << surface.display_name;
  }
  EXPECT_EQ(geometry.lod_policy.type,
    data::pak::geometry::LodPolicyType::kScreenSpaceError)
    << "an imported geometry with several LODs switches them by default";
  EXPECT_GE(geometry.bounds_max.z, 5.0F)
    << "LOD1 keeps its level's translation";
  EXPECT_EQ(imported.renderable_count, 1U);
  EXPECT_EQ(imported.node_count, 2U) << "the root and the group's node";
  EXPECT_EQ(Count(imported.diagnostics, "fbx.lod.level_skipped"), 0U);
}

//! Sibling meshes named `<base>_LOD<n>` import as one geometry shown by the
//! `_LOD0` node.
NOLINT_TEST_F(FbxLodGroupsTest, NamedSiblingsAreOneGeometryShownByLod0)
{
  // Arrange
  const auto source = Document(Square(2001, "Rock0") + Square(2002, "Rock1")
      + Square(2003, "Rock2") + Model(3001, "Rock_LOD0", "Mesh")
      + Model(3002, "Rock_lod1", "Mesh") + Model(3003, "Rock_LOD2", "Mesh"),
    Connect(3001, 0) + Connect(3002, 0) + Connect(3003, 0) + Connect(2001, 3001)
      + Connect(kPaint, 3001) + Connect(kGlass, 3001) + Connect(2002, 3002)
      + Connect(kPaint, 3002) + Connect(kGlass, 3002) + Connect(2003, 3003)
      + Connect(kPaint, 3003) + Connect(kGlass, 3003));

  // Act
  const auto imported = Import(source);

  // Assert
  ASSERT_TRUE(imported.success);
  ASSERT_EQ(imported.geometries.size(), 1U);
  EXPECT_EQ(imported.geometries.front().lod_count, 3U);
  EXPECT_EQ(imported.renderable_count, 1U);
  EXPECT_EQ(imported.node_count, 2U) << "the root and Rock_LOD0";
}

//! A group keeps its levels up to the first unusable one and reports each
//! level it leaves out.
NOLINT_TEST_F(FbxLodGroupsTest, LevelsAfterAnUnusableOneAreReportedAndSkipped)
{
  // Arrange
  const auto source = Document(Square(2001, "Lod0") + Square(2003, "Lod2")
      + Model(3000, "Car", "Null") + Model(3001, "Car_LOD0", "Mesh")
      + Model(3002, "Car_LOD1", "Null") + Model(3003, "Car_LOD2", "Mesh")
      + "    NodeAttribute: 3010, \"NodeAttribute::Car\", \"LodGroup\" {\n"
        "        TypeFlags: \"Null\"\n"
        "    }\n",
    Connect(3000, 0) + Connect(3010, 3000) + Connect(3001, 3000)
      + Connect(3002, 3000) + Connect(3003, 3000) + Connect(2001, 3001)
      + Connect(kPaint, 3001) + Connect(kGlass, 3001) + Connect(2003, 3003)
      + Connect(kPaint, 3003) + Connect(kGlass, 3003));

  // Act
  const auto imported = Import(source);

  // Assert
  ASSERT_TRUE(imported.success);
  ASSERT_EQ(imported.geometries.size(), 1U);
  EXPECT_EQ(imported.geometries.front().lod_count, 1U);
  EXPECT_EQ(Count(imported.diagnostics, "fbx.lod.level_skipped"), 2U);
  EXPECT_EQ(imported.renderable_count, 1U);
  EXPECT_EQ(imported.node_count, 2U);
}

} // namespace
