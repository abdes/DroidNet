//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/CollisionShapeDescriptorImportJob.cpp,
//   Import/Internal/JoltShapeCook.cpp,
//   Import/Internal/Utils/CookedMeshSurface.cpp

#include <Jolt/Jolt.h> // IWYU pragma: keep

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Geometry/AABox.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>
#include <Jolt/RegisterTypes.h>
#include <glm/common.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Shape.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  using oxygen::cooker::test::CollisionShapeDescriptor;
  using oxygen::cooker::test::FindPhysicsResource;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadCollisionShape;
  using oxygen::cooker::testing::CookedPak;
  using oxygen::cooker::testing::LooseCookedRoot;
  using oxygen::cooker::testing::Materialize;
  namespace phys = data::pak::physics;
  namespace backend = oxygen::physics::jolt;
  namespace lc = oxygen::content::lc;

  constexpr auto kLibraryMaterial = "/Library/Physics/Stone.opmat";
  constexpr auto kLibraryGeometry = "/Library/Meshes/Cube.ogeo";
  constexpr auto kShapePath = "/Cooked/Physics/Shapes/Rock.ocshape";

  //! A cooked context root, the "Library" mount, holding a physics material
  //! and the procedural unit cube geometry; loose and packed.
  struct LibraryRoots final {
    std::filesystem::path loose;
    std::filesystem::path pak;
  };

  class CollisionShapeCookingTest : public ImportServiceTest {
  protected:
    static auto SetUpTestSuite() -> void
    {
      // The restored payloads go through the runtime shape builder, which
      // the test executable's own Jolt copy runs.
      static std::once_flag jolt_once;
      std::call_once(jolt_once, [] -> void {
        JPH::RegisterDefaultAllocator();
        // Lives for the whole test executable, as the registered types do.
        static auto factory = JPH::Factory {};
        JPH::Factory::sInstance = &factory;
        JPH::RegisterTypes();
      });
    }

    auto MakeLibrary() -> LibraryRoots
    {
      auto library = LooseCookedRoot { "Library" };
      static_cast<void>(library.PhysicsMaterial("Physics/Stone.opmat"));
      static_cast<void>(library.Geometry("Meshes/Cube.ogeo"));
      // Decoys: a geometry where a material is expected and the reverse.
      static_cast<void>(library.Geometry("Physics/Decoy.opmat"));
      static_cast<void>(library.PhysicsMaterial("Meshes/Decoy.ogeo"));
      const auto roots = LibraryRoots {
        .loose = TempDir() / "library",
        .pak = TempDir() / "library.pak",
      };
      Materialize(library.Files(), roots.loose);
      Materialize(CookedPak { library }.Encode(), roots.pak);
      return roots;
    }

    //! Imports `shape` into a fresh cooked root named `name` that reads
    //! `context`.
    auto ImportShape(const CollisionShapeDescriptor& shape,
      const std::filesystem::path& context, const std::string& name)
      -> std::pair<std::optional<ImportReport>, std::filesystem::path>
    {
      const auto cooked_root = TempDir() / name;
      std::filesystem::create_directories(cooked_root);
      auto request = ImportRequest {};
      request.source_path = TempDir() / "source" / "rock.shape.json";
      request.cooked_root = cooked_root;
      request.asset_virtual_path = shape.Identity();
      request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(request.source_path);
      request.cooked_context_roots = { context };
      request.collision_shape_descriptor
        = ImportRequest::CollisionShapeDescriptorPayload {
            .descriptor_json = shape.Text(),
          };
      auto report = Import(std::move(request));
      return {
        report.has_value() ? std::optional(std::move(*report)) : std::nullopt,
        cooked_root,
      };
    }
  };

  auto Shape(const std::string_view type) -> CollisionShapeDescriptor
  {
    auto shape = CollisionShapeDescriptor { kShapePath, kLibraryMaterial };
    shape.Remove("/half_extents").Set("/shape_type", std::string(type));
    return shape;
  }

  auto Open(const std::filesystem::path& root) -> cooked::CookedContent
  {
    return cooked::CookedContent::Open(cooked::RealCookerStorage(), root);
  }

  //! The cooked payload of the shape at `kShapePath`, as the runtime takes it.
  auto RuntimePayload(const cooked::CookedContent& content,
    const data::AssetKey& payload_key, const phys::ShapePayloadType type)
    -> physics::CookedShapePayload
  {
    const auto resource = FindPhysicsResource(content, payload_key);
    if (!resource.has_value()) {
      ADD_FAILURE() << "the cooked root holds no such physics resource";
      return {};
    }
    EXPECT_EQ(resource->format, phys::PhysicsResourceFormat::kJoltShapeBinary);
    auto payload = physics::CookedShapePayload {
      .payload_type = static_cast<physics::ShapePayloadType>(type),
    };
    std::ranges::transform(resource->payload, std::back_inserter(payload.data),
      [](const std::byte b) -> uint8_t { return static_cast<uint8_t>(b); });
    return payload;
  }

  //! The procedural unit cube the library's geometry draws.
  auto Cube() -> std::pair<std::vector<data::Vertex>, std::vector<uint32_t>>
  {
    auto cube = data::GenerateMeshBuffers("Cube/Mesh", {});
    if (!cube.has_value()) {
      throw std::logic_error("the product generates no Cube/Mesh");
    }
    return std::move(*cube);
  }

  //! The bounds of the procedural unit cube.
  auto CubeBounds() -> std::pair<Vec3, Vec3>
  {
    auto low = Vec3 { 1.0e9F };
    auto high = Vec3 { -1.0e9F };
    for (const auto& vertex : Cube().first) {
      low = glm::min(low, vertex.position);
      high = glm::max(high, vertex.position);
    }
    return { low, high };
  }

  auto ExpectNear(const JPH::Vec3 actual, const Vec3& expected) -> void
  {
    constexpr auto kTolerance = 1.0e-4F;
    EXPECT_NEAR(actual.GetX(), expected.x, kTolerance);
    EXPECT_NEAR(actual.GetY(), expected.y, kTolerance);
    EXPECT_NEAR(actual.GetZ(), expected.z, kTolerance);
  }

  //! A convex hull of an engine geometry, with an engine material, cooks from
  //! a loose or packed context root into a hull the runtime restores, and the
  //! shape records what it requires of the engine mount.
  NOLINT_TEST_F(CollisionShapeCookingTest, ConvexHullCooksFromAMountedGeometry)
  {
    const auto library = MakeLibrary();
    auto shape = Shape("convex_hull");
    shape.Set("/geometry_ref", kLibraryGeometry).Set("/convex_radius", 0.0);

    for (const auto& context : { library.loose, library.pak }) {
      const auto [report, cooked_root]
        = ImportShape(shape, context, "hull_" + context.filename().string());

      ASSERT_HAS_VALUE(report);
      ASSERT_TRUE(report->success)
        << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
      const auto content = Open(cooked_root);
      const auto cooked = ReadCollisionShape(
        content, data::AssetKey::FromVirtualPath(kShapePath));
      ASSERT_EQ(cooked.descriptor.shape_type, phys::ShapeType::kConvexHull);
      EXPECT_EQ(cooked.descriptor.material_asset_key,
        data::AssetKey::FromVirtualPath(kLibraryMaterial));
      const auto built = backend::MakeShape(physics::ConvexHullShape {
        .cooked_payload = RuntimePayload(content,
          cooked.descriptor.cooked_shape_ref.payload_asset_key,
          phys::ShapePayloadType::kConvex),
      });
      ASSERT_FALSE(built.has_error());
      EXPECT_EQ(built.value()->GetSubType(), JPH::EShapeSubType::ConvexHull);
      const auto [low, high] = CubeBounds();
      // Jolt reports bounds around the center of mass.
      const auto bounds = built.value()->GetLocalBounds();
      const auto com = built.value()->GetCenterOfMass();
      ExpectNear(bounds.mMin + com, low);
      ExpectNear(bounds.mMax + com, high);

      const auto index = lc::LooseCookedIndex::LoadFromRoot(cooked_root);
      const auto& requirements = index.Requirements();
      EXPECT_TRUE(std::ranges::any_of(requirements,
        [](const lc::AssetRequirement& required) -> bool {
          return required.requirement.mount == "Library"
            && required.requirement.key
            == data::AssetKey::FromVirtualPath(kLibraryMaterial);
        }))
        << context;
    }
  }

  //! A triangle mesh keeps every triangle of the geometry.
  NOLINT_TEST_F(CollisionShapeCookingTest, TriangleMeshCooksEveryTriangle)
  {
    const auto library = MakeLibrary();
    auto shape = Shape("triangle_mesh");
    shape.Set("/geometry_ref", kLibraryGeometry);

    const auto [report, cooked_root]
      = ImportShape(shape, library.loose, "mesh");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success)
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
    const auto content = Open(cooked_root);
    const auto cooked = ReadCollisionShape(
      content, data::AssetKey::FromVirtualPath(kShapePath));
    const auto built = backend::MakeShape(physics::TriangleMeshShape {
      .cooked_payload = RuntimePayload(content,
        cooked.descriptor.cooked_shape_ref.payload_asset_key,
        phys::ShapePayloadType::kMesh),
    });
    ASSERT_FALSE(built.has_error());
    EXPECT_EQ(built.value()->GetType(), JPH::EShapeType::Mesh);
    EXPECT_EQ(
      built.value()->GetStats().mNumTriangles, Cube().second.size() / 3U);
  }

  //! A height field's sample (row, column) is at Oxygen
  //! (column * spacing.x, row * spacing.y) with its height along +Z, and a
  //! hole has no collision.
  NOLINT_TEST_F(CollisionShapeCookingTest, HeightFieldPlacesRowsAlongY)
  {
    const auto library = MakeLibrary();
    auto shape = Shape("height_field");
    // Row 0 is flat at 0, row 1 at 2 and row 2 at 4; the last sample is a
    // hole.
    shape.Set("/sample_count", 3)
      .Set("/heights", { 0.0, 0.0, 0.0, 2.0, 2.0, 2.0, 4.0, 4.0, nullptr })
      .Set("/sample_spacing", { 1.0, 2.0 })
      .Set("/bits_per_sample", 8);

    const auto [report, cooked_root]
      = ImportShape(shape, library.loose, "height_field");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success)
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
    const auto content = Open(cooked_root);
    const auto cooked = ReadCollisionShape(
      content, data::AssetKey::FromVirtualPath(kShapePath));
    const auto built = backend::MakeShape(physics::HeightFieldShape {
      .cooked_payload = RuntimePayload(content,
        cooked.descriptor.cooked_shape_ref.payload_asset_key,
        phys::ShapePayloadType::kHeightField),
    });
    ASSERT_FALSE(built.has_error());
    const auto height_at
      = [&](const float x, const float y) -> std::optional<float> {
      constexpr auto kTop = 100.0F;
      // Jolt casts around the center of mass.
      const auto ray = JPH::RayCast {
        JPH::Vec3(x, y, kTop) - built.value()->GetCenterOfMass(),
        JPH::Vec3(0.0F, 0.0F, -2.0F * kTop),
      };
      auto hit = JPH::RayCastResult {};
      if (!built.value()->CastRay(ray, JPH::SubShapeIDCreator {}, hit)) {
        return std::nullopt;
      }
      return kTop - (hit.mFraction * 2.0F * kTop);
    };
    constexpr auto kTolerance = 1.0e-2F;
    // Row 1 is at Oxygen y = 1 * spacing.y = 2.
    const auto row_one = height_at(0.5F, 2.0F);
    ASSERT_HAS_VALUE(row_one);
    EXPECT_NEAR(*row_one, 2.0F, kTolerance);
    const auto between = height_at(0.25F, 3.0F);
    ASSERT_HAS_VALUE(between);
    EXPECT_NEAR(*between, 3.0F, kTolerance);
    // The quad next to the hole at row 2, column 2 has no collision.
    EXPECT_FALSE(height_at(1.75F, 3.75F).has_value());
  }

  //! A compound's convex hull child carries its own cooked payload.
  NOLINT_TEST_F(CollisionShapeCookingTest, CompoundChildCarriesItsPayload)
  {
    const auto library = MakeLibrary();
    auto shape = Shape("compound");
    const auto placement = json {
      { "local_position", { 0.0, 0.0, 1.0 } },
      { "local_rotation", { 0.0, 0.0, 0.0, 1.0 } },
      { "local_scale", { 1.0, 1.0, 1.0 } },
    };
    auto hull = placement;
    hull["shape_type"] = "convex_hull";
    hull["geometry_ref"] = kLibraryGeometry;
    auto cone = placement;
    cone["shape_type"] = "cone";
    cone["radius"] = 0.5;
    cone["half_height"] = 1.0;
    shape.Append("/children", hull).Append("/children", cone);

    const auto [report, cooked_root]
      = ImportShape(shape, library.loose, "compound");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success)
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
    const auto content = Open(cooked_root);
    const auto cooked = ReadCollisionShape(
      content, data::AssetKey::FromVirtualPath(kShapePath));
    ASSERT_EQ(cooked.children.size(), 2U);
    EXPECT_FALSE(cooked.children[0].payload_asset_key.IsNil());
    EXPECT_TRUE(cooked.children[1].payload_asset_key.IsNil());
    EXPECT_TRUE(
      FindPhysicsResource(content, cooked.children[0].payload_asset_key)
        .has_value());
  }

  //! A reference that names an asset of the wrong type is refused, with no
  //! fallback to another asset; nothing is published.
  NOLINT_TEST_F(CollisionShapeCookingTest, RefusesAReferenceOfTheWrongType)
  {
    const auto library = MakeLibrary();
    auto wrong_geometry = Shape("convex_hull");
    wrong_geometry.Set("/geometry_ref", "/Library/Meshes/Decoy.ogeo");
    auto wrong_material = Shape("triangle_mesh");
    wrong_material.Set("/geometry_ref", kLibraryGeometry)
      .Set("/material_ref", "/Library/Physics/Decoy.opmat");
    auto missing = Shape("triangle_mesh");
    missing.Set("/geometry_ref", "/Library/Meshes/Missing.ogeo");

    const auto [geometry_report, geometry_root]
      = ImportShape(wrong_geometry, library.loose, "wrong_geometry");
    const auto [material_report, material_root]
      = ImportShape(wrong_material, library.loose, "wrong_material");
    const auto [missing_report, missing_root]
      = ImportShape(missing, library.loose, "missing_geometry");

    ASSERT_HAS_VALUE(geometry_report);
    EXPECT_FALSE(geometry_report->success);
    EXPECT_TRUE(HasDiagnosticCode(geometry_report->diagnostics,
      import_codes::kPhysicsShapeGeometryRefNotGeometry));
    ASSERT_HAS_VALUE(material_report);
    EXPECT_FALSE(material_report->success);
    EXPECT_TRUE(HasDiagnosticCode(material_report->diagnostics,
      import_codes::kPhysicsShapeMaterialRefNotPhysicsMaterial));
    // The Library mount is only a cooked context of the job: an asset no
    // mounted root lists is in no mount the job knows.
    ASSERT_HAS_VALUE(missing_report);
    EXPECT_FALSE(missing_report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      missing_report->diagnostics, import_codes::kReferenceUnmounted));
    for (const auto& root : { geometry_root, material_root, missing_root }) {
      EXPECT_FALSE(oxygen::cooker::test::HasCookedFile(
        root, "Physics/Shapes/Rock.ocshape"));
    }
  }

} // namespace

} // namespace oxygen::content::import::test
