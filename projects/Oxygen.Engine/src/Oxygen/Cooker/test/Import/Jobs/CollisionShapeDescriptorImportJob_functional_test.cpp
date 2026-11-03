//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/CollisionShapeDescriptorImportJob.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio {

auto Load(AnyReader& reader, data::pak::physics::CollisionShapeAssetDesc& value)
  -> Result<void>
{
  const auto align_result
    = reader.AlignTo(alignof(data::pak::physics::CollisionShapeAssetDesc));
  if (!align_result.has_value()) {
    return align_result;
  }
  return reader.ReadBlobInto(std::as_writable_bytes(
    std::span<data::pak::physics::CollisionShapeAssetDesc, 1>(&value, 1)));
}

} // namespace oxygen::serio

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  namespace phys = data::pak::physics;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;

  using oxygen::cooker::test::HasDiagnosticCode;

  auto ReadCollisionShapeAssetDesc(const std::vector<std::byte>& bytes)
    -> std::optional<phys::CollisionShapeAssetDesc>
  {
    if (bytes.empty()) {
      return std::nullopt;
    }
    auto stream = serio::ReadOnlyMemoryStream(std::span(bytes));
    auto reader = serio::Reader(stream);
    auto descriptor = reader.Read<phys::CollisionShapeAssetDesc>();
    if (!descriptor.has_value()) {
      return std::nullopt;
    }
    return descriptor.value();
  }

  auto ReadCompoundChildDesc(const std::vector<std::byte>& bytes,
    const size_t offset) -> std::optional<phys::CompoundShapeChildDesc>
  {
    if (offset + sizeof(phys::CompoundShapeChildDesc) > bytes.size()) {
      return std::nullopt;
    }
    const auto child = serio::CopyAt<phys::CompoundShapeChildDesc>(
      std::span { bytes }, serio::ByteOffset { offset });
    if (!child) {
      return std::nullopt;
    }
    return *child;
  }

  auto AssetTypeForKey(const std::filesystem::path& cooked_root,
    const data::AssetKey& key) -> std::optional<data::AssetType>
  {
    try {
      const auto content
        = cooked::CookedContent::Open(cooked::RealCookerStorage(), cooked_root);
      if (const auto* const asset = content.FindAsset(key)) {
        return asset->type;
      }
    } catch (...) {
      return std::nullopt;
    }
    return std::nullopt;
  }

  //! The source and cooked roots a scenario imports between.
  struct ScenarioRoots final {
    std::filesystem::path source;
    std::filesystem::path cooked;
  };

  auto MakeMaterialRequest(const ScenarioRoots& roots) -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = roots.source / "ground.material.json";
    request.cooked_root = roots.cooked;
    request.asset_virtual_path = "/.cooked/Physics/Materials/ground.opmat";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    auto material = oxygen::cooker::test::PhysicsMaterialDescriptor {
      request.asset_virtual_path,
    };
    material.Set("/static_friction", 0.95)
      .Set("/dynamic_friction", 0.70)
      .Set("/restitution", 0.05)
      .Set("/density", 1800.0);
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .normalized_descriptor_json = material.Text(),
        };
    return request;
  }

  constexpr auto kGroundMaterial = "/.cooked/Physics/Materials/ground.opmat";

  //! A box shape document named `name`, at `identity`, of the ground material.
  auto MakeShape(const std::string& identity, const std::string_view name)
    -> oxygen::cooker::test::CollisionShapeDescriptor
  {
    auto shape = oxygen::cooker::test::CollisionShapeDescriptor {
      identity,
      kGroundMaterial,
    };
    shape.Set("/name", name);
    return shape;
  }

  auto MakeCollisionShapeRequest(const ScenarioRoots& roots,
    const oxygen::cooker::test::CollisionShapeDescriptor& shape)
    -> ImportRequest
  {
    const auto& descriptor_doc = shape.Json();
    const auto& asset_virtual_path = shape.Identity();
    auto request = ImportRequest {};
    request.source_path = roots.source / "shape.shape.json";
    request.cooked_root = roots.cooked;
    request.asset_virtual_path = asset_virtual_path;
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.collision_shape_descriptor
      = ImportRequest::CollisionShapeDescriptorPayload {
          .normalized_descriptor_json = descriptor_doc.dump(),
        };
    return request;
  }

  class CollisionShapeDescriptorImportJobTest : public ImportServiceTest { };

  NOLINT_TEST_F(CollisionShapeDescriptorImportJobTest,
    PrimitiveShapeImportsSuccessfullyAndEmitsOcshape)
  {
    const auto cooked_root = TempDir() / "primitive_shape_success";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";

    const auto material_report = Import(
      MakeMaterialRequest({ .source = source_root, .cooked = cooked_root }));
    ASSERT_HAS_VALUE(material_report);
    ASSERT_TRUE(material_report->success);

    auto shape
      = MakeShape("/.cooked/Physics/Shapes/floor_box.ocshape", "floor_box");
    shape.Set("/half_extents", { 25.0F, 0.5F, 25.0F });

    const auto report = Import(MakeCollisionShapeRequest(
      { .source = source_root, .cooked = cooked_root }, shape));
    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);

    constexpr auto kRelPath
      = std::string_view { "Physics/Shapes/floor_box.ocshape" };
    const auto has_output = [&](const std::string_view relpath) -> bool {
      return std::ranges::any_of(
        report->outputs, [&](const ImportOutputRecord& output) -> bool {
          return oxygen::cooker::test::LayoutPathOf(output.path) == relpath;
        });
    };
    EXPECT_TRUE(has_output(kRelPath));

    ASSERT_TRUE(oxygen::cooker::test::HasCookedFile(cooked_root, kRelPath));
    const auto bytes
      = ReadBytes(oxygen::cooker::test::CookedFile(cooked_root, kRelPath));
    const auto descriptor = ReadCollisionShapeAssetDesc(bytes);
    ASSERT_HAS_VALUE(descriptor);
    EXPECT_EQ(descriptor->header.asset_type,
      static_cast<uint8_t>(data::AssetType::kCollisionShape));
    EXPECT_EQ(descriptor->header.version, phys::kCollisionShapeAssetVersion);
    EXPECT_EQ(descriptor->shape_type, phys::ShapeType::kBox);
    const auto material_asset_type
      = AssetTypeForKey(cooked_root, descriptor->material_asset_key);
    ASSERT_HAS_VALUE(material_asset_type);
    EXPECT_EQ(*material_asset_type, data::AssetType::kPhysicsMaterial);
    EXPECT_EQ(descriptor->shape_params.box.half_extents[0], 25.0F);
    EXPECT_EQ(descriptor->shape_params.box.half_extents[1], 0.5F);
    EXPECT_EQ(descriptor->shape_params.box.half_extents[2], 25.0F);
  }

  NOLINT_TEST_F(
    CollisionShapeDescriptorImportJobTest, TopLevelUnknownFieldRejectedBySchema)
  {
    const auto cooked_root = TempDir() / "unknown_field_rejected";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";

    const auto material_report = Import(
      MakeMaterialRequest({ .source = source_root, .cooked = cooked_root }));
    ASSERT_HAS_VALUE(material_report);
    ASSERT_TRUE(material_report->success);

    auto shape
      = MakeShape("/.cooked/Physics/Shapes/hull.ocshape", "convex_hull_shape");
    shape.Set("/shape_type", "convex_hull")
      .Remove("/half_extents")
      .Set(
        "/unexpected_field", "/.cooked/Physics/Resources/shape_payload.opres");

    const auto report = Import(MakeCollisionShapeRequest(
      { .source = source_root, .cooked = cooked_root }, shape));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, "physics.shape.schema_validation_failed"));
  }

  NOLINT_TEST_F(CollisionShapeDescriptorImportJobTest,
    PayloadBackedShapeEmitsInlineCookedPayload)
  {
    const auto cooked_root = TempDir() / "payload_shape_inline_emission";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";

    const auto material_report = Import(
      MakeMaterialRequest({ .source = source_root, .cooked = cooked_root }));
    ASSERT_HAS_VALUE(material_report);
    ASSERT_TRUE(material_report->success);

    auto shape = MakeShape("/.cooked/Physics/Shapes/hull_inline.ocshape",
      "convex_hull_inline_payload");
    shape.Set("/shape_type", "convex_hull").Remove("/half_extents");

    const auto report = Import(MakeCollisionShapeRequest(
      { .source = source_root, .cooked = cooked_root }, shape));
    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);

    constexpr auto kShapeRelPath
      = std::string_view { "Physics/Shapes/hull_inline.ocshape" };
    ASSERT_TRUE(
      oxygen::cooker::test::HasCookedFile(cooked_root, kShapeRelPath));
    const auto bytes
      = ReadBytes(oxygen::cooker::test::CookedFile(cooked_root, kShapeRelPath));
    const auto descriptor = ReadCollisionShapeAssetDesc(bytes);
    ASSERT_HAS_VALUE(descriptor);
    EXPECT_EQ(descriptor->shape_type, phys::ShapeType::kConvexHull);
    EXPECT_EQ(descriptor->cooked_shape_ref.payload_type,
      phys::ShapePayloadType::kConvex);
    EXPECT_FALSE(descriptor->cooked_shape_ref.payload_asset_key.IsNil());
    EXPECT_TRUE(oxygen::cooker::test::HasCookedFile(
      cooked_root, "Physics/Resources/physics.table"));
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path("Physics/Resources/physics.data")));
  }

  NOLINT_TEST_F(CollisionShapeDescriptorImportJobTest,
    CompoundShapeSerializesTrailingChildDescriptors)
  {
    const auto cooked_root = TempDir() / "compound_children";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";

    const auto material_report = Import(
      MakeMaterialRequest({ .source = source_root, .cooked = cooked_root }));
    ASSERT_HAS_VALUE(material_report);
    ASSERT_TRUE(material_report->success);

    auto shape = MakeShape(
      "/.cooked/Physics/Shapes/compound_shape.ocshape", "compound_shape");
    shape.Set("/shape_type", "compound")
      .Remove("/half_extents")
      .Set("/children",
        json::array({
          json {
            { "shape_type", "sphere" },
            { "radius", 0.5F },
            { "local_position", json::array({ 1.0F, 2.0F, 3.0F }) },
            { "local_rotation", json::array({ 0.0F, 0.0F, 0.0F, 1.0F }) },
            { "local_scale", json::array({ 1.0F, 1.0F, 1.0F }) },
          },
          json {
            { "shape_type", "box" },
            { "half_extents", json::array({ 0.25F, 0.5F, 0.75F }) },
            { "local_position", json::array({ -1.0F, 0.0F, 2.0F }) },
            { "local_rotation", json::array({ 0.0F, 0.0F, 0.0F, 1.0F }) },
            { "local_scale", json::array({ 0.9F, 0.9F, 0.9F }) },
          },
        }));

    const auto report = Import(MakeCollisionShapeRequest(
      { .source = source_root, .cooked = cooked_root }, shape));
    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);

    constexpr auto kShapeRelPath
      = std::string_view { "Physics/Shapes/compound_shape.ocshape" };
    ASSERT_TRUE(
      oxygen::cooker::test::HasCookedFile(cooked_root, kShapeRelPath));
    const auto bytes
      = ReadBytes(oxygen::cooker::test::CookedFile(cooked_root, kShapeRelPath));
    const auto descriptor = ReadCollisionShapeAssetDesc(bytes);
    ASSERT_HAS_VALUE(descriptor);
    ASSERT_EQ(descriptor->shape_type, phys::ShapeType::kCompound);
    EXPECT_EQ(descriptor->shape_params.compound.child_count, 2U);
    EXPECT_EQ(descriptor->shape_params.compound.child_byte_offset,
      static_cast<uint32_t>(sizeof(phys::CollisionShapeAssetDesc)));

    const auto child_base = static_cast<size_t>(
      descriptor->shape_params.compound.child_byte_offset);
    const auto child0 = ReadCompoundChildDesc(bytes, child_base);
    const auto child1 = ReadCompoundChildDesc(
      bytes, child_base + sizeof(phys::CompoundShapeChildDesc));
    ASSERT_HAS_VALUE(child0);
    ASSERT_HAS_VALUE(child1);
    EXPECT_EQ(
      child0->shape_type, static_cast<uint32_t>(phys::ShapeType::kSphere));
    EXPECT_FLOAT_EQ(child0->radius, 0.5F);
    EXPECT_FLOAT_EQ(child0->local_position[0], 1.0F);
    EXPECT_FLOAT_EQ(child0->local_position[1], 2.0F);
    EXPECT_FLOAT_EQ(child0->local_position[2], 3.0F);
    EXPECT_EQ(child1->shape_type, static_cast<uint32_t>(phys::ShapeType::kBox));
    EXPECT_FLOAT_EQ(child1->half_extents[0], 0.25F);
    EXPECT_FLOAT_EQ(child1->half_extents[1], 0.5F);
    EXPECT_FLOAT_EQ(child1->half_extents[2], 0.75F);
  }

  NOLINT_TEST_F(CollisionShapeDescriptorImportJobTest,
    CompoundNonAnalyticChildRejectedBySchema)
  {
    const auto cooked_root = TempDir() / "compound_child_payload_missing";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";

    const auto material_report = Import(
      MakeMaterialRequest({ .source = source_root, .cooked = cooked_root }));
    ASSERT_HAS_VALUE(material_report);
    ASSERT_TRUE(material_report->success);

    auto shape = MakeShape("/.cooked/Physics/Shapes/bad_compound.ocshape",
      "bad_compound_child_payload");
    shape.Set("/shape_type", "compound")
      .Remove("/half_extents")
      .Set("/children",
        json::array({
          json {
            { "shape_type", "convex_hull" },
            { "local_position", json::array({ 0.0F, 0.0F, 0.0F }) },
            { "local_rotation", json::array({ 0.0F, 0.0F, 0.0F, 1.0F }) },
            { "local_scale", json::array({ 1.0F, 1.0F, 1.0F }) },
          },
        }));

    const auto report = Import(MakeCollisionShapeRequest(
      { .source = source_root, .cooked = cooked_root }, shape));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, "physics.shape.schema_validation_failed"));
  }

  NOLINT_TEST_F(CollisionShapeDescriptorImportJobTest,
    DocumentStatingAVirtualPathIsRefusedBySchema)
  {
    const auto cooked_root = TempDir() / "stated_virtual_path";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";

    const auto material_report = Import(
      MakeMaterialRequest({ .source = source_root, .cooked = cooked_root }));
    ASSERT_HAS_VALUE(material_report);
    ASSERT_TRUE(material_report->success);

    auto shape
      = MakeShape("/.cooked/Physics/Shapes/floor_box.ocshape", "floor_box");
    shape.Set("/half_extents", { 25.0F, 0.5F, 25.0F })
      .Set("/virtual_path", "/.cooked/Physics/Shapes/other.ocshape");

    const auto report = Import(MakeCollisionShapeRequest(
      { .source = source_root, .cooked = cooked_root }, shape));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, "physics.shape.schema_validation_failed"));
  }

} // namespace

} // namespace oxygen::content::import::test
