//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/GeometryDescriptorImportJob.cpp

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "GeometryDescriptorImportJobTestSupport.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/BuiltinGeometry.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::base::CheckedAt;

namespace oxygen::content::import::test {

namespace {

  auto BuiltinSurfaceSlot(const std::string_view generator) -> std::string
  {
    const auto geometry = data::ResolveBuiltinGeometry(
      "asset:///Engine/Generated/BasicShapes/" + std::string(generator));
    if (!geometry || geometry->MaterialSlots().slots.size() != 1U) {
      throw std::runtime_error("Expected a single-surface builtin geometry");
    }
    return data::to_string(geometry->MaterialSlots().slots.front().slot_id);
  }

  auto MakeCapsuleDescriptor(
    const json& params, const float height, const float radius) -> json
  {
    const auto bounds = json {
      { "min", json::array({ -radius, -radius, -height * 0.5F }) },
      { "max", json::array({ radius, radius, height * 0.5F }) },
    };
    auto procedural
      = json { { "generator", "Capsule" }, { "mesh_name", "Capsule" } };
    if (!params.is_null()) {
      procedural.update({ { "params", params } });
    }
    return json {
      { "name", "Capsule" },
      { "bounds", bounds },
      {
        "lods",
        json::array({
          {
            { "name", "LOD0" },
            { "mesh_type", "procedural" },
            { "bounds", bounds },
            { "procedural", std::move(procedural) },
            {
              "submeshes",
              json::array({
                {
                  { "slot_id", BuiltinSurfaceSlot("Capsule") },
                  { "material_ref", "/.cooked/Materials/default.omat" },
                  { "views", json::array({ { { "view_ref", "__all__" } } }) },
                },
              }),
            },
          },
        }),
      },
    };
  }

} // namespace

NOLINT_TEST(GeometryDescriptorImportJobProceduralTest,
  ProceduralDescriptorImportsAndRemainsLoaderCompatible)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "procedural_loader_compatible";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "cube.procedural.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto descriptor_doc = json {
    { "name", "ProceduralCube" },
    { "bounds", MakeBounds() },
    {
      "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", MakeBounds() },
          {
            "procedural",
            {
              { "generator", "Cube" },
              { "mesh_name", "UnitCube" },
            },
          },
          {
            "submeshes",
            json::array({
              json {
                { "slot_id", BuiltinSurfaceSlot("Cube") },
                { "material_ref", "/.cooked/Materials/default.omat" },
                {
                  "views",
                  json::array({ json { { "view_ref", "__all__" } } }),
                },
              },
            }),
          },
        },
      }),
    },
  };
  WriteText(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  ASSERT_TRUE(report.success)
    << oxygen::cooker::test::DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value())
    << "Expected geometry_relpath to contain a value";

  const auto descriptor_bytes
    = ReadBytes(cooked_root / std::filesystem::path(*geometry_relpath));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobProceduralTest,
  ProceduralPlanePreservesDescriptorBoundsInCookedMetadata)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "procedural_plane_bounds_xy_z0";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "floor.procedural.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto floor_bounds = json {
    { "min", json::array({ -5.0F, -5.0F, 0.0F }) },
    { "max", json::array({ 5.0F, 5.0F, 0.0F }) },
  };
  const auto descriptor_doc = json {
    { "name", "FloorPlane" },
    { "bounds", floor_bounds },
    {
      "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", floor_bounds },
          {
            "procedural",
            {
              { "generator", "Plane" },
              { "mesh_name", "Floor" },
              {
                "params",
                {
                  { "x_segments", 10U },
                  { "z_segments", 10U },
                  { "size", 10.0F },
                },
              },
            },
          },
          {
            "submeshes",
            json::array({
              json {
                { "slot_id", BuiltinSurfaceSlot("Plane") },
                { "material_ref", "/.cooked/Materials/default.omat" },
                {
                  "views",
                  json::array({ json { { "view_ref", "__all__" } } }),
                },
              },
            }),
          },
        },
      }),
    },
  };
  WriteText(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  ASSERT_TRUE(report.success)
    << oxygen::cooker::test::DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value())
    << "Expected geometry_relpath to contain a value";

  const auto descriptor_bytes
    = ReadBytes(cooked_root / std::filesystem::path(*geometry_relpath));
  ASSERT_GE(
    descriptor_bytes.size(), sizeof(data::pak::geometry::GeometryAssetDesc));

  const auto geometry_desc
    = ReadStructAt<data::pak::geometry::GeometryAssetDesc>(
      descriptor_bytes, 0U);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_min[0], -5.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_min[1], -5.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_min[2], 0.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_max[0], 5.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_max[1], 5.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_max[2], 0.0F);
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobProceduralTest,
  ProceduralCapsuleParametersRoundTripThroughNativeLoader)
{
  struct Case {
    std::string_view name;
    json params;
    uint32_t hemisphere_segments;
    uint32_t radial_segments;
    float height;
    float radius;
  };
  const auto cases = std::vector<Case> {
    { "capsule_defaults", nullptr, 8U, 32U, 2.0F, 0.5F },
    {
      "capsule_custom",
      {
        { "hemisphere_segments", 4 },
        { "radial_segments", 16 },
        { "height", 3.0 },
        { "radius", 0.75 },
      },
      4U,
      16U,
      3.0F,
      0.75F,
    },
    { "capsule_sphere", { { "height", 1.0 } }, 8U, 32U, 1.0F, 0.5F },
  };
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    const ScopedTempDir temp;
    const auto root = temp.Path() / test_case.name;
    std::filesystem::create_directories(root);
    const auto cooked_root = root / ".cooked";
    const auto source_path = root / "Sources" / "capsule.geometry.json";
    WriteText(cooked_root / "Materials" / "default.omat", "placeholder");
    const auto doc = MakeCapsuleDescriptor(
      test_case.params, test_case.height, test_case.radius);
    WriteText(source_path, doc.dump(2));
    const auto report = SubmitAndWait(
      service, MakeGeometryRequest(source_path, cooked_root, doc));
    ASSERT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
    ASSERT_EQ(report.geometry_written, 1U);
    const auto output = FindOutputByExtension(report, ".ogeo");
    ASSERT_TRUE(output.has_value()) << "Expected output to contain a value";
    auto bytes = ReadBytes(cooked_root / *output);

    constexpr auto mesh_offset = sizeof(data::pak::geometry::GeometryAssetDesc);
    constexpr auto params_offset
      = mesh_offset + sizeof(data::pak::geometry::MeshDesc);
    ASSERT_GE(bytes.size(), params_offset + 16U);
    const auto mesh_desc
      = ReadStructAt<data::pak::geometry::MeshDesc>(bytes, mesh_offset);
    EXPECT_TRUE(mesh_desc.IsProcedural());
    EXPECT_STREQ(mesh_desc.name, "Capsule/Capsule");
    EXPECT_EQ(mesh_desc.info.procedural.params_size, 16U);
    EXPECT_EQ(ReadStructAt<uint32_t>(bytes, params_offset),
      test_case.hemisphere_segments);
    EXPECT_EQ(ReadStructAt<uint32_t>(bytes, params_offset + 4U),
      test_case.radial_segments);
    EXPECT_FLOAT_EQ(
      ReadStructAt<float>(bytes, params_offset + 8U), test_case.height);
    EXPECT_FLOAT_EQ(
      ReadStructAt<float>(bytes, params_offset + 12U), test_case.radius);

    auto stream = serio::MemoryStream(std::span<std::byte>(bytes));
    auto reader = serio::Reader(stream);
    auto context = content::LoaderContext {};
    context.desc_reader = &reader;
    context.parse_only = true;
    const auto geometry
      = content::loaders::LoadGeometryAsset(std::move(context));
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->LodCount(), 1U);
    const auto& mesh = geometry->MeshAt(oxygen::data::LodIndex {});
    ASSERT_NE(mesh, nullptr);
    ASSERT_GT(mesh->VertexCount(), 0U);
    ASSERT_GT(mesh->IndexCount(), 0U);
    const auto expected
      = data::MakeCapsuleMeshAsset(test_case.hemisphere_segments,
        test_case.radial_segments, test_case.height, test_case.radius);
    ASSERT_TRUE(expected.has_value()) << "Expected expected to contain a value";
    const auto loaded_vertices = mesh->Vertices();
    ASSERT_EQ(loaded_vertices.size(), expected->first.size());
    for (size_t vertex_index = 0; vertex_index < loaded_vertices.size();
      ++vertex_index) {
      const auto& actual_vertex = CheckedAt(loaded_vertices, vertex_index);
      const auto& expected_vertex = expected->first.at(vertex_index);
      EXPECT_EQ(actual_vertex.position, expected_vertex.position);
      EXPECT_EQ(actual_vertex.normal, expected_vertex.normal);
      EXPECT_EQ(actual_vertex.texcoord, expected_vertex.texcoord);
      EXPECT_EQ(actual_vertex.tangent, expected_vertex.tangent);
      EXPECT_EQ(actual_vertex.bitangent, expected_vertex.bitangent);
      EXPECT_EQ(actual_vertex.color, expected_vertex.color);
    }
    const auto loaded_indices = mesh->IndexBuffer().AsU32();
    ASSERT_EQ(loaded_indices.size(), expected->second.size());
    for (size_t index = 0; index < expected->second.size(); ++index) {
      EXPECT_EQ(CheckedAt(loaded_indices, index), expected->second.at(index));
    }
    const auto minimum = mesh->BoundingBoxMin();
    const auto maximum = mesh->BoundingBoxMax();
    EXPECT_FLOAT_EQ(minimum.x, -test_case.radius);
    EXPECT_FLOAT_EQ(minimum.y, -test_case.radius);
    EXPECT_FLOAT_EQ(minimum.z, -test_case.height * 0.5F);
    EXPECT_FLOAT_EQ(maximum.x, test_case.radius);
    EXPECT_FLOAT_EQ(maximum.y, test_case.radius);
    EXPECT_FLOAT_EQ(maximum.z, test_case.height * 0.5F);
    ASSERT_EQ(mesh->SubMeshes().size(), 1U);
    const auto views = mesh->SubMeshes().front().MeshViews();
    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views.front().VertexCount(), mesh->VertexCount());
    EXPECT_EQ(views.front().IndexCount(), mesh->IndexCount());
  }
}

NOLINT_TEST(GeometryDescriptorImportJobProceduralTest,
  ProceduralCapsuleRejectsUnrepresentableDimensionsWithoutOutput)
{
  const auto cases = std::vector<json> {
    { { "height", 0.75 }, { "radius", 0.5 } },
    { { "radius", 1.0e-50 } },
    { { "height", 1.0e10 }, { "radius", 0.5 } },
  };
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  for (size_t index = 0; index < cases.size(); ++index) {
    SCOPED_TRACE(cases.at(index).dump());
    const ScopedTempDir temp;
    const auto root
      = temp.Path() / ("capsule_invalid_" + std::to_string(index));
    std::filesystem::create_directories(root);
    const auto cooked_root = root / ".cooked";
    const auto source_path = root / "Sources" / "capsule.geometry.json";
    WriteText(cooked_root / "Materials" / "default.omat", "placeholder");
    const auto doc = MakeCapsuleDescriptor(cases.at(index), 2.0F, 0.5F);
    WriteText(source_path, doc.dump(2));
    const auto report = SubmitAndWait(
      service, MakeGeometryRequest(source_path, cooked_root, doc));
    EXPECT_FALSE(report.success);
    EXPECT_EQ(report.geometry_written, 0U);
    EXPECT_FALSE(FindOutputByExtension(report, ".ogeo").has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "geometry.procedural.generation_failed"))
      << DiagnosticSummary(report.diagnostics);
  }
}

NOLINT_TEST(GeometryDescriptorImportJobProceduralTest,
  ProceduralIcoSphereParametersRoundTripThroughNativeLoader)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "procedural_icosphere_loader_compatible";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path
    = source_dir / "icosphere.procedural.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto descriptor_doc = json {
    { "name", "ProceduralIcoSphere" },
    { "bounds", MakeBounds() },
    {
      "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", MakeBounds() },
          {
            "procedural",
            {
              { "generator", "IcoSphere" },
              { "mesh_name", "SoftBall" },
              { "params", { { "subdivision_level", 2 } } },
            },
          },
          {
            "submeshes",
            json::array({
              json {
                { "slot_id", BuiltinSurfaceSlot("IcoSphere") },
                { "material_ref", "/.cooked/Materials/default.omat" },
                {
                  "views",
                  json::array({ json { { "view_ref", "__all__" } } }),
                },
              },
            }),
          },
        },
      }),
    },
  };
  WriteText(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  ASSERT_TRUE(report.success)
    << oxygen::cooker::test::DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value())
    << "Expected geometry_relpath to contain a value";

  auto descriptor_bytes
    = ReadBytes(cooked_root / std::filesystem::path(*geometry_relpath));
  constexpr auto mesh_offset = sizeof(data::pak::geometry::GeometryAssetDesc);
  constexpr auto params_offset
    = mesh_offset + sizeof(data::pak::geometry::MeshDesc);
  ASSERT_GE(descriptor_bytes.size(), params_offset + sizeof(uint32_t));
  const auto mesh_desc = ReadStructAt<data::pak::geometry::MeshDesc>(
    descriptor_bytes, mesh_offset);
  EXPECT_TRUE(mesh_desc.IsProcedural());
  EXPECT_STREQ(mesh_desc.name, "IcoSphere/SoftBall");
  EXPECT_EQ(mesh_desc.info.procedural.params_size, sizeof(uint32_t));
  EXPECT_EQ(ReadStructAt<uint32_t>(descriptor_bytes, params_offset), 2U);

  auto stream = serio::MemoryStream(std::span<std::byte>(descriptor_bytes));
  auto reader = serio::Reader(stream);
  auto context = content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;
  const auto geometry = content::loaders::LoadGeometryAsset(std::move(context));
  ASSERT_NE(geometry, nullptr);
  ASSERT_EQ(geometry->LodCount(), 1U);
  const auto& mesh = geometry->MeshAt(oxygen::data::LodIndex {});
  ASSERT_NE(mesh, nullptr);
  const auto expected = data::MakeIcoSphereMeshAsset(2);
  ASSERT_TRUE(expected.has_value()) << "Expected expected to contain a value";
  const auto vertices = mesh->Vertices();
  ASSERT_EQ(vertices.size(), expected->first.size());
  for (size_t index = 0; index < vertices.size(); ++index) {
    EXPECT_EQ(
      CheckedAt(vertices, index).position, expected->first.at(index).position);
    EXPECT_EQ(
      CheckedAt(vertices, index).normal, expected->first.at(index).normal);
  }
  const auto indices = mesh->IndexBuffer().AsU32();
  ASSERT_EQ(indices.size(), expected->second.size());
  for (size_t index = 0; index < indices.size(); ++index) {
    EXPECT_EQ(CheckedAt(indices, index), expected->second.at(index));
  }
}

NOLINT_TEST(GeometryDescriptorImportJobProceduralTest,
  ProceduralSubdividedCubeImportsAndRemainsLoaderCompatible)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "procedural_subdivided_cube_loader";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path
    = source_dir / "subdivided_cube.procedural.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto descriptor_doc = json {
    { "name", "ProceduralSubdividedCube" },
    { "bounds", MakeBounds() },
    {
      "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", MakeBounds() },
          {
            "procedural",
            {
              { "generator", "SubdividedCube" },
              { "mesh_name", "JellyCube" },
              { "params", { { "segments", 8 } } },
            },
          },
          {
            "submeshes",
            json::array({
              json {
                { "slot_id", BuiltinSurfaceSlot("SubdividedCube") },
                { "material_ref", "/.cooked/Materials/default.omat" },
                {
                  "views",
                  json::array({ json { { "view_ref", "__all__" } } }),
                },
              },
            }),
          },
        },
      }),
    },
  };
  WriteText(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  ASSERT_TRUE(report.success)
    << oxygen::cooker::test::DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value())
    << "Expected geometry_relpath to contain a value";

  const auto descriptor_bytes
    = ReadBytes(cooked_root / std::filesystem::path(*geometry_relpath));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

} // namespace oxygen::content::import::test
