//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <latch>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

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

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [code](const ImportDiagnostic& diagnostic) {
        return diagnostic.code == code;
      });
  }

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path()
      / "oxygen_collision_shape_descriptor_import_job";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
  }

  auto ReadBinaryFile(const std::filesystem::path& path)
    -> std::vector<std::byte>
  {
    auto in = std::ifstream(path, std::ios::binary);
    EXPECT_TRUE(in.is_open());
    if (!in.is_open()) {
      return {};
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    auto bytes = std::vector<std::byte>(size);
    in.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    EXPECT_TRUE(in.good() || in.eof());
    return bytes;
  }

  auto ReadCollisionShapeAssetDesc(const std::vector<std::byte>& bytes)
    -> std::optional<phys::CollisionShapeAssetDesc>
  {
    if (bytes.empty()) {
      return std::nullopt;
    }
    auto mutable_bytes = bytes;
    auto stream = serio::MemoryStream(
      std::span<std::byte>(mutable_bytes.data(), mutable_bytes.size()));
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
    auto child = phys::CompoundShapeChildDesc {};
    std::memcpy(&child, bytes.data() + offset, sizeof(child));
    return child;
  }

  auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
    -> ImportReport
  {
    auto report = ImportReport {};
    std::latch done(1);
    const auto submitted = service.SubmitImport(
      std::move(request),
      [&report, &done](
        const ImportJobId /*job_id*/, const ImportReport& completed) {
        report = completed;
        done.count_down();
      },
      nullptr);
    EXPECT_TRUE(submitted.has_value());
    done.wait();
    return report;
  }

  auto AssetTypeForKey(const std::filesystem::path& cooked_root,
    const data::AssetKey& key) -> std::optional<data::AssetType>
  {
    auto inspection = lc::Inspection {};
    try {
      inspection.LoadFromRoot(cooked_root);
    } catch (...) {
      return std::nullopt;
    }
    for (const auto& asset : inspection.Assets()) {
      if (asset.key == key) {
        return static_cast<data::AssetType>(asset.asset_type);
      }
    }
    return std::nullopt;
  }

  auto MakeMaterialRequest(const std::filesystem::path& source_root,
    const std::filesystem::path& cooked_root) -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = source_root / "ground.material.json";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .normalized_descriptor_json = R"({
            "name": "ground",
            "static_friction": 0.95,
            "dynamic_friction": 0.70,
            "restitution": 0.05,
            "density": 1800.0,
            "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
          })",
        };
    return request;
  }

  auto MakeCollisionShapeRequest(const std::filesystem::path& source_root,
    const std::filesystem::path& cooked_root, const json& descriptor_doc)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = source_root / "shape.shape.json";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.collision_shape_descriptor
      = ImportRequest::CollisionShapeDescriptorPayload {
          .normalized_descriptor_json = descriptor_doc.dump(),
        };
    return request;
  }

  NOLINT_TEST(CollisionShapeDescriptorImportJobTest,
    PrimitiveShapeImportsSuccessfullyAndEmitsOcshape)
  {
    const auto cooked_root = MakeTempCookedRoot("primitive_shape_success");
    const auto source_root = cooked_root.parent_path() / "source_data";

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto material_report
      = SubmitAndWait(service, MakeMaterialRequest(source_root, cooked_root));
    ASSERT_TRUE(material_report.success);

    const auto shape_descriptor = json {
      { "name", "floor_box" },
      { "shape_type", "box" },
      { "material_ref", "/.cooked/Physics/Materials/ground.opmat" },
      { "half_extents", json::array({ 25.0F, 0.5F, 25.0F }) },
      { "virtual_path", "/.cooked/Physics/Shapes/floor_box.ocshape" },
    };

    const auto report = SubmitAndWait(service,
      MakeCollisionShapeRequest(source_root, cooked_root, shape_descriptor));
    EXPECT_TRUE(report.success);

    constexpr auto kRelPath
      = std::string_view { "Physics/Shapes/floor_box.ocshape" };
    const auto has_output = [&](const std::string_view relpath) {
      return std::ranges::any_of(
        report.outputs, [&](const ImportOutputRecord& output) {
          return output.path == relpath;
        });
    };
    EXPECT_TRUE(has_output(kRelPath));

    const auto full_path = cooked_root / std::filesystem::path { kRelPath };
    ASSERT_TRUE(std::filesystem::exists(full_path));
    const auto bytes = ReadBinaryFile(full_path);
    const auto descriptor = ReadCollisionShapeAssetDesc(bytes);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->header.asset_type,
      static_cast<uint8_t>(data::AssetType::kCollisionShape));
    EXPECT_EQ(descriptor->header.version, phys::kCollisionShapeAssetVersion);
    EXPECT_EQ(descriptor->shape_type, phys::ShapeType::kBox);
    const auto material_asset_type
      = AssetTypeForKey(cooked_root, descriptor->material_asset_key);
    ASSERT_TRUE(material_asset_type.has_value());
    EXPECT_EQ(*material_asset_type, data::AssetType::kPhysicsMaterial);
    EXPECT_EQ(descriptor->shape_params.box.half_extents[0], 25.0F);
    EXPECT_EQ(descriptor->shape_params.box.half_extents[1], 0.5F);
    EXPECT_EQ(descriptor->shape_params.box.half_extents[2], 25.0F);
  }

  NOLINT_TEST(
    CollisionShapeDescriptorImportJobTest, TopLevelUnknownFieldRejectedBySchema)
  {
    const auto cooked_root
      = MakeTempCookedRoot("unknown_field_rejected");
    const auto source_root = cooked_root.parent_path() / "source_data";

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto material_report
      = SubmitAndWait(service, MakeMaterialRequest(source_root, cooked_root));
    ASSERT_TRUE(material_report.success);

    const auto shape_descriptor = json {
      { "name", "convex_hull_shape" },
      { "shape_type", "convex_hull" },
      { "material_ref", "/.cooked/Physics/Materials/ground.opmat" },
      { "unexpected_field", "/.cooked/Physics/Resources/shape_payload.opres" },
      { "virtual_path", "/.cooked/Physics/Shapes/hull.ocshape" },
    };

    const auto report = SubmitAndWait(service,
      MakeCollisionShapeRequest(source_root, cooked_root, shape_descriptor));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "physics.shape.schema_validation_failed"));
  }

  NOLINT_TEST(CollisionShapeDescriptorImportJobTest,
    PayloadBackedShapeEmitsInlineCookedPayload)
  {
    const auto cooked_root
      = MakeTempCookedRoot("payload_shape_inline_emission");
    const auto source_root = cooked_root.parent_path() / "source_data";

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto material_report
      = SubmitAndWait(service, MakeMaterialRequest(source_root, cooked_root));
    ASSERT_TRUE(material_report.success);

    const auto shape_descriptor = json {
      { "name", "convex_hull_inline_payload" },
      { "shape_type", "convex_hull" },
      { "material_ref", "/.cooked/Physics/Materials/ground.opmat" },
      { "virtual_path", "/.cooked/Physics/Shapes/hull_inline.ocshape" },
    };

    const auto report = SubmitAndWait(service,
      MakeCollisionShapeRequest(source_root, cooked_root, shape_descriptor));
    EXPECT_TRUE(report.success);

    constexpr auto kShapeRelPath
      = std::string_view { "Physics/Shapes/hull_inline.ocshape" };
    const auto shape_path
      = cooked_root / std::filesystem::path { kShapeRelPath };
    ASSERT_TRUE(std::filesystem::exists(shape_path));
    const auto bytes = ReadBinaryFile(shape_path);
    const auto descriptor = ReadCollisionShapeAssetDesc(bytes);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->shape_type, phys::ShapeType::kConvexHull);
    EXPECT_EQ(descriptor->cooked_shape_ref.payload_type,
      phys::ShapePayloadType::kConvex);
    EXPECT_NE(descriptor->cooked_shape_ref.resource_index,
      data::pak::core::kNoResourceIndex);
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path("Physics/Resources/physics.table")));
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path("Physics/Resources/physics.data")));
  }

  NOLINT_TEST(CollisionShapeDescriptorImportJobTest,
    CompoundShapeSerializesTrailingChildDescriptors)
  {
    const auto cooked_root = MakeTempCookedRoot("compound_children");
    const auto source_root = cooked_root.parent_path() / "source_data";

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto material_report
      = SubmitAndWait(service, MakeMaterialRequest(source_root, cooked_root));
    ASSERT_TRUE(material_report.success);

    const auto shape_descriptor = json {
      { "name", "compound_shape" },
      { "shape_type", "compound" },
      { "material_ref", "/.cooked/Physics/Materials/ground.opmat" },
      { "virtual_path", "/.cooked/Physics/Shapes/compound_shape.ocshape" },
      { "children",
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
        }) },
    };

    const auto report = SubmitAndWait(service,
      MakeCollisionShapeRequest(source_root, cooked_root, shape_descriptor));
    EXPECT_TRUE(report.success);

    const auto shape_path = cooked_root
      / std::filesystem::path("Physics/Shapes/compound_shape.ocshape");
    ASSERT_TRUE(std::filesystem::exists(shape_path));
    const auto bytes = ReadBinaryFile(shape_path);
    const auto descriptor = ReadCollisionShapeAssetDesc(bytes);
    ASSERT_TRUE(descriptor.has_value());
    ASSERT_EQ(descriptor->shape_type, phys::ShapeType::kCompound);
    EXPECT_EQ(descriptor->shape_params.compound.child_count, 2U);
    EXPECT_EQ(descriptor->shape_params.compound.child_byte_offset,
      static_cast<uint32_t>(sizeof(phys::CollisionShapeAssetDesc)));

    const auto child_base = static_cast<size_t>(
      descriptor->shape_params.compound.child_byte_offset);
    const auto child0 = ReadCompoundChildDesc(bytes, child_base);
    const auto child1 = ReadCompoundChildDesc(
      bytes, child_base + sizeof(phys::CompoundShapeChildDesc));
    ASSERT_TRUE(child0.has_value());
    ASSERT_TRUE(child1.has_value());
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

  NOLINT_TEST(CollisionShapeDescriptorImportJobTest,
    CompoundNonAnalyticChildRejectedBySchema)
  {
    const auto cooked_root
      = MakeTempCookedRoot("compound_child_payload_missing");
    const auto source_root = cooked_root.parent_path() / "source_data";

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto material_report
      = SubmitAndWait(service, MakeMaterialRequest(source_root, cooked_root));
    ASSERT_TRUE(material_report.success);

    const auto shape_descriptor = json {
      { "name", "bad_compound_child_payload" },
      { "shape_type", "compound" },
      { "material_ref", "/.cooked/Physics/Materials/ground.opmat" },
      { "virtual_path", "/.cooked/Physics/Shapes/bad_compound.ocshape" },
      { "children",
        json::array({ json {
          { "shape_type", "convex_hull" },
          { "local_position", json::array({ 0.0F, 0.0F, 0.0F }) },
          { "local_rotation", json::array({ 0.0F, 0.0F, 0.0F, 1.0F }) },
          { "local_scale", json::array({ 1.0F, 1.0F, 1.0F }) },
        } }) },
    };

    const auto report = SubmitAndWait(service,
      MakeCollisionShapeRequest(source_root, cooked_root, shape_descriptor));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "physics.shape.schema_validation_failed"));
  }

} // namespace

} // namespace oxygen::content::import::test
