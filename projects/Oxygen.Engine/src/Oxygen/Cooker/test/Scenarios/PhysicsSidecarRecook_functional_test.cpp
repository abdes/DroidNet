//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/AsyncImportService.cpp,
//   Import/Internal/Jobs/PhysicsSidecarImportJob.cpp, Cooked/CookedContent.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  namespace phys = oxygen::data::pak::physics;
  using oxygen::cooker::test::SubmitAndWait;
  using oxygen::cooker::testing::GeometryBuilder;
  using oxygen::cooker::testing::LooseCookedRoot;
  using oxygen::cooker::testing::Materialize;

  constexpr auto kSceneName = std::string_view { "ComplexScene" };
  constexpr auto kSceneVirtualPath
    = std::string_view { "/Cooked/Scenes/ComplexScene.oscene" };
  constexpr auto kSidecarVirtualPath
    = std::string_view { "/Cooked/Scenes/ComplexScene.opscene" };
  constexpr auto kMaterialVirtualPath
    = std::string_view { "/Cooked/Physics/Materials/ground.opmat" };
  constexpr auto kShapeVirtualPath
    = std::string_view { "/Cooked/Physics/Shapes/chassis_compound.ocshape" };
  constexpr auto kGeometryVirtualPath
    = std::string_view { "/Cooked/Geometry/cloth.ogeo" };

  struct ParsedPhysicsSidecarFile final {
    phys::PhysicsSceneAssetDesc descriptor {};
    std::vector<phys::PhysicsComponentTableDesc> directory;
  };

  //! Where an array of records sits in a descriptor.
  struct ArrayRange final {
    size_t offset = 0;
    uint32_t count = 0;
    uint32_t entry_size = 0;
  };

  //! The records `range` holds, or none when it does not fit `bytes` or its
  //! records are not `T`s.
  template <typename T>
  auto ReadStructArrayAt(const std::vector<std::byte>& bytes,
    const ArrayRange& range) -> std::vector<T>
  {
    if (range.entry_size != sizeof(T)) {
      return {};
    }
    const auto byte_size = static_cast<size_t>(range.count) * sizeof(T);
    if (range.offset + byte_size > bytes.size()) {
      return {};
    }
    auto out = std::vector<T>(range.count);
    std::memcpy(out.data(),
      std::span(bytes).subspan(range.offset, byte_size).data(), byte_size);
    return out;
  }

  auto ParsePhysicsSidecarFile(const std::vector<std::byte>& bytes)
    -> std::optional<ParsedPhysicsSidecarFile>
  {
    const auto descriptor
      = oxygen::serio::CopyAt<phys::PhysicsSceneAssetDesc>(std::span { bytes });
    if (!descriptor.has_value()) {
      return std::nullopt;
    }
    auto parsed = ParsedPhysicsSidecarFile {
      .descriptor = *descriptor,
      .directory = {},
    };
    if (parsed.descriptor.component_table_count == 0U) {
      return parsed;
    }
    const auto directory_offset
      = static_cast<size_t>(parsed.descriptor.component_table_directory_offset);
    const auto directory_size
      = static_cast<size_t>(parsed.descriptor.component_table_count
        * sizeof(phys::PhysicsComponentTableDesc));
    if (directory_offset + directory_size > bytes.size()) {
      return std::nullopt;
    }
    parsed.directory = ReadStructArrayAt<phys::PhysicsComponentTableDesc>(bytes,
      {
        .offset = directory_offset,
        .count = parsed.descriptor.component_table_count,
        .entry_size = sizeof(phys::PhysicsComponentTableDesc),
      });
    if (parsed.directory.size() != parsed.descriptor.component_table_count) {
      return std::nullopt;
    }
    return parsed;
  }

  auto FindTable(const ParsedPhysicsSidecarFile& sidecar,
    const phys::PhysicsBindingType type)
    -> std::optional<phys::PhysicsComponentTableDesc>
  {
    for (const auto& table : sidecar.directory) {
      if (table.binding_type == type) {
        return table;
      }
    }
    return std::nullopt;
  }

  auto ParsePhysicsResourceTable(const std::filesystem::path& path)
    -> std::vector<phys::PhysicsResourceDesc>
  {
    const auto bytes = oxygen::cooker::test::ReadBytes(path);
    if (bytes.empty()
      || (bytes.size() % sizeof(phys::PhysicsResourceDesc) != 0U)) {
      return {};
    }
    const auto count
      = static_cast<uint32_t>(bytes.size() / sizeof(phys::PhysicsResourceDesc));
    return ReadStructArrayAt<phys::PhysicsResourceDesc>(bytes,
      {
        .offset = 0,
        .count = count,
        .entry_size = sizeof(phys::PhysicsResourceDesc),
      });
  }

  auto FindPhysicsResourceByAssetKey(
    const std::vector<phys::PhysicsResourceDesc>& table,
    const data::AssetKey& asset_key) -> const phys::PhysicsResourceDesc*
  {
    const auto it = std::ranges::find_if(
      table, [&](const phys::PhysicsResourceDesc& desc) -> bool {
        return desc.resource_asset_key == asset_key;
      });
    return it != table.end() ? &(*it) : nullptr;
  }

  auto OpenCooked(const std::filesystem::path& cooked_root)
    -> cooked::CookedContent
  {
    return cooked::CookedContent::Open(
      cooked::RealCookerStorage(), cooked_root);
  }

  //! The file of `layout_relpath` below `cooked_root`: a data file keeps its
  //! layout name, every other file is named by its content.
  auto CookedPath(const std::filesystem::path& cooked_root,
    const std::string_view layout_relpath) -> std::filesystem::path
  {
    if (layout_relpath.ends_with(".data")) {
      return cooked_root / std::filesystem::path(layout_relpath);
    }
    return oxygen::cooker::test::CookedFile(cooked_root, layout_relpath);
  }

  //! The digest of the descriptor of `key`, wherever the index of
  //! `cooked_root` places it.
  auto DescriptorDigest(const std::filesystem::path& cooked_root,
    const data::AssetKey& key) -> base::Sha256Digest
  {
    return base::ComputeSha256(OpenCooked(cooked_root).Descriptor(key));
  }

  //! The digest of the stub geometry's descriptor.
  auto StubGeometryDigest(const std::filesystem::path& cooked_root)
    -> base::Sha256Digest
  {
    return DescriptorDigest(
      cooked_root, data::AssetKey::FromVirtualPath(kGeometryVirtualPath));
  }

  auto FindInspectionAsset(const cooked::CookedContent& inspection,
    const data::AssetKey& key) -> std::optional<cooked::CookedAsset>
  {
    for (const auto& asset : inspection.Assets()) {
      if (asset.key == key) {
        return asset;
      }
    }
    return std::nullopt;
  }

  auto ComputeFileDigest(const std::filesystem::path& path)
    -> base::Sha256Digest
  {
    const auto bytes = oxygen::cooker::test::ReadBytes(path);
    if (bytes.empty()) {
      return {};
    }
    return base::ComputeSha256(
      std::span<const std::byte>(bytes.data(), bytes.size()));
  }

  auto HasOutputPath(const ImportReport& report, const std::string_view relpath)
    -> bool
  {
    return std::ranges::any_of(
      report.outputs, [&](const ImportOutputRecord& o) -> bool {
        return oxygen::cooker::test::LayoutPathOf(o.path) == relpath;
      });
  }

  auto MakeSceneDescriptorRequest(const std::filesystem::path& cooked_root,
    const std::string_view scene_name, const uint32_t node_count)
    -> ImportRequest
  {
    // A readable, deterministic id per node index.
    const auto node_id = [](const uint32_t index) -> std::string {
      const auto digits = std::to_string(index + 1U);
      return "00000000-0000-4000-8000-" + std::string(12U - digits.size(), '0')
        + digits;
    };
    auto descriptor = oxygen::cooker::test::SceneDescriptor {
      std::string { kSceneVirtualPath },
    };
    descriptor.Set("/name", scene_name);
    for (uint32_t i = 1; i < node_count; ++i) {
      descriptor.Append("/nodes",
        json {
          { "id", node_id(i) },
          { "name", "Node" + std::to_string(i) },
          { "parent", 0U },
        });
    }

    auto request = ImportRequest {};
    request.source_path = "inline://scene.oscene.json";
    request.job_name = std::string(scene_name);
    request.asset_virtual_path = std::string(kSceneVirtualPath);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = cooked_root;
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .descriptor_json = descriptor.Text(),
    };
    return request;
  }

  auto MakePhysicsMaterialRequest(const std::filesystem::path& cooked_root)
    -> ImportRequest
  {
    auto descriptor = oxygen::cooker::test::PhysicsMaterialDescriptor {
      std::string { kMaterialVirtualPath },
    };
    descriptor.Set("/static_friction", 0.95F)
      .Set("/dynamic_friction", 0.70F)
      .Set("/restitution", 0.05F)
      .Set("/density", 1800.0F);

    auto request = ImportRequest {};
    request.source_path = "inline://material.opmat.json";
    request.job_name = "ground.physics-material";
    request.asset_virtual_path = std::string(kMaterialVirtualPath);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = cooked_root;
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .descriptor_json = descriptor.Text(),
        };
    return request;
  }

  auto MakeCompoundShapeRequest(const std::filesystem::path& cooked_root)
    -> ImportRequest
  {
    auto descriptor = oxygen::cooker::test::CollisionShapeDescriptor {
      std::string { kShapeVirtualPath },
      kMaterialVirtualPath,
    };
    descriptor.Set("/name", "chassis_compound")
      .Set("/shape_type", "compound")
      .Remove("/half_extents")
      .Set("/children",
        json::array({
          json {
            { "shape_type", "box" },
            { "half_extents", json::array({ 1.0F, 0.4F, 2.0F }) },
            { "local_position", json::array({ 0.0F, 0.0F, 0.0F }) },
            { "local_rotation", json::array({ 0.0F, 0.0F, 0.0F, 1.0F }) },
            { "local_scale", json::array({ 1.0F, 1.0F, 1.0F }) },
          },
          json {
            { "shape_type", "sphere" },
            { "radius", 0.35F },
            { "local_position", json::array({ 0.0F, 0.6F, 1.1F }) },
            { "local_rotation", json::array({ 0.0F, 0.0F, 0.0F, 1.0F }) },
            { "local_scale", json::array({ 1.0F, 1.0F, 1.0F }) },
          },
        }));

    auto request = ImportRequest {};
    request.source_path = "inline://shape.ocshape.json";
    request.job_name = "chassis-compound";
    request.asset_virtual_path = std::string(kShapeVirtualPath);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = cooked_root;
    request.collision_shape_descriptor
      = ImportRequest::CollisionShapeDescriptorPayload {
          .descriptor_json = descriptor.Text(),
        };
    return request;
  }

  auto MakePhysicsSidecarRequest(const std::filesystem::path& cooked_root,
    const std::string_view target_scene_virtual_path,
    const oxygen::cooker::test::PhysicsSidecar& bindings_doc) -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    request.job_name = "complex-sidecar";
    request.cooked_root = cooked_root;
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = std::string(target_scene_virtual_path),
      .inline_bindings_json = bindings_doc.Text(),
    };
    return request;
  }

  auto BuildComplexSidecarBindings(const std::vector<uint32_t>& pinned_vertices,
    const std::vector<uint32_t>& kinematic_vertices)
    -> oxygen::cooker::test::PhysicsSidecar
  {
    auto sidecar = oxygen::cooker::test::PhysicsSidecar {
      std::string { kSidecarVirtualPath },
    };
    sidecar.AppendBinding("rigid_bodies",
      json {
        { "node_index", 1U },
        {
          "shape_ref",
          sidecar.Spell(kShapeVirtualPath),
        },
        {
          "material_ref",
          sidecar.Spell(kMaterialVirtualPath),
        },
        { "body_type", "dynamic" },
        { "motion_quality", "linear_cast" },
        { "mass", 1350.0F },
        {
          "backend",
          {
            { "target", "jolt" },
            { "num_velocity_steps_override", 2U },
            { "num_position_steps_override", 3U },
          },
        },
      });
    sidecar.AppendBinding("soft_bodies",
      json {
        { "node_index", 2U },
        {
          "source_mesh_ref",
          sidecar.Spell(kGeometryVirtualPath),
        },
        { "collision_layer", 2U },
        { "collision_mask", 0xFFFFFFFFU },
        { "edge_compliance", 0.0002F },
        { "shear_compliance", 0.0003F },
        { "bend_compliance", 0.0004F },
        { "volume_compliance", 0.0005F },
        { "pressure_coefficient", 0.05F },
        { "tether_mode", "euclidean" },
        { "tether_max_distance_multiplier", 1.2F },
        { "global_damping", 0.02F },
        { "restitution", 0.12F },
        { "friction", 0.34F },
        { "vertex_radius", 0.01F },
        { "solver_iteration_count", 6U },
        { "self_collision", true },
        { "pinned_vertices", pinned_vertices },
        { "kinematic_vertices", kinematic_vertices },
        {
          "backend",
          {
            { "target", "jolt" },
            { "velocity_iteration_count", 8U },
          },
        },
      });
    sidecar.AppendBinding("joints",
      json {
        { "node_index_a", 1U },
        { "node_index_b", "world" },
        { "constraint_type", "hinge" },
        { "constraint_space", "local" },
        { "local_frame_a_position", json::array({ 0.0F, 0.0F, 0.0F }) },
        {
          "local_frame_a_rotation",
          json::array({ 0.0F, 0.0F, 0.0F, 1.0F }),
        },
        { "local_frame_b_position", json::array({ 0.0F, 0.0F, 0.0F }) },
        {
          "local_frame_b_rotation",
          json::array({ 0.0F, 0.0F, 0.0F, 1.0F }),
        },
        {
          "limits_lower",
          json::array({ 0.0F, 0.0F, 0.0F, -0.1F, 0.0F, 0.0F }),
        },
        {
          "limits_upper",
          json::array({ 0.0F, 0.0F, 0.0F, 0.1F, 0.0F, 0.0F }),
        },
        {
          "spring_stiffnesses",
          json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
        },
        {
          "spring_dampings",
          json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
        },
        {
          "motor_max_forces",
          json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
        },
        {
          "motor_max_torques",
          json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
        },
        {
          "motor_drive_frequencies",
          json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
        },
        {
          "motor_damping_ratios",
          json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
        },
        { "priority", 7U },
        {
          "backend",
          {
            { "target", "jolt" },
            { "num_velocity_steps_override", 1U },
            { "num_position_steps_override", 2U },
          },
        },
      });
    sidecar.AppendBinding("vehicles",
      json {
        { "node_index", 3U },
        { "controller_type", "wheeled" },
        {
          "wheels",
          json::array({
            json {
              { "node_index", 8U },
              { "axle_index", 1U },
              { "side", "right" },
              {
                "backend",
                {
                  { "target", "jolt" },
                  { "wheel_castor", 0.80F },
                },
              },
            },
            json {
              { "node_index", 6U },
              { "axle_index", 0U },
              { "side", "right" },
              {
                "backend",
                {
                  { "target", "jolt" },
                  { "wheel_castor", 0.60F },
                },
              },
            },
            json {
              { "node_index", 7U },
              { "axle_index", 1U },
              { "side", "left" },
              {
                "backend",
                {
                  { "target", "jolt" },
                  { "wheel_castor", 0.70F },
                },
              },
            },
            json {
              { "node_index", 5U },
              { "axle_index", 0U },
              { "side", "left" },
              {
                "backend",
                {
                  { "target", "jolt" },
                  { "wheel_castor", 0.50F },
                },
              },
            },
          }),
        },
      });
    return sidecar;
  }

  //! Writes the cloth geometry `Geometry/cloth.ogeo` and its buffers into
  //! `cooked_root`.
  auto RegisterStubGeometryAsset(const std::filesystem::path& cooked_root)
    -> void
  {
    struct VertexPosition final {
      float x;
      float y;
      float z;
    };
    static_assert(sizeof(VertexPosition) == sizeof(float) * 3U);

    const auto vertices = std::array<VertexPosition, 7> {
      VertexPosition { .x = 0.0F, .y = 0.15F, .z = 0.0F },
      VertexPosition { .x = -0.12F, .y = -0.10F, .z = -0.12F },
      VertexPosition { .x = 0.12F, .y = -0.10F, .z = -0.12F },
      VertexPosition { .x = 0.0F, .y = -0.10F, .z = 0.14F },
      VertexPosition { .x = 0.0F, .y = 0.15F, .z = 0.0F },
      VertexPosition { .x = 0.06F, .y = 0.02F, .z = 0.08F },
      VertexPosition { .x = 0.0F, .y = -0.02F, .z = -0.04F },
    };
    // A tetrahedron wound outward, so the pressure the bindings author has a
    // positive rest volume. Vertex 4 is a seam copy of vertex 0 that one face
    // uses: the surface is closed only once the cook welds them, and a pin on
    // vertex 4 lands on the welded apex. No face uses vertices 5 and 6, which
    // the cook drops.
    const auto indices = std::array<uint32_t, 12> {
      0U,
      2U,
      1U, // top
      4U,
      3U,
      2U, // side
      0U,
      1U,
      3U, // side
      1U,
      2U,
      3U, // base
    };

    auto root = LooseCookedRoot { "Cooked" };
    auto vertex_row = data::pak::core::BufferResourceDesc {};
    vertex_row.usage_flags = 0x01U;
    vertex_row.element_stride = sizeof(VertexPosition);
    vertex_row.element_format = static_cast<uint8_t>(Format::kUnknown);
    auto index_row = data::pak::core::BufferResourceDesc {};
    index_row.usage_flags = 0x02U;
    index_row.element_stride = 0U;
    index_row.element_format = static_cast<uint8_t>(Format::kR32UInt);
    const auto vertex_buffer
      = root.AddBuffer(std::as_bytes(std::span(vertices)), vertex_row);
    const auto index_buffer
      = root.AddBuffer(std::as_bytes(std::span(indices)), index_row);

    auto geometry = GeometryBuilder { "cloth" };
    // The bounds of the tetrahedron's vertices.
    const auto lod = geometry.StandardLod(vertex_buffer, index_buffer,
      glm::vec3 { -0.12F, -0.10F, -0.12F }, glm::vec3 { 0.12F, 0.15F, 0.14F });
    geometry.Submesh(lod,
      data::SurfaceId::FromStableIdentity("PhysicsPhase3/cloth"), {},
      {
        {
          .first_index = 0U,
          .index_count = static_cast<uint32_t>(indices.size()),
          .first_vertex = 0U,
          .vertex_count = static_cast<uint32_t>(vertices.size()),
        },
      });
    root.Geometry("Geometry/cloth.ogeo", geometry);
    Materialize(root.Files(), cooked_root);
  }

  struct CookedSnapshot {
    base::Sha256Digest scene_digest {};
    base::Sha256Digest sidecar_digest {};
    base::Sha256Digest material_digest {};
    base::Sha256Digest shape_digest {};
    base::Sha256Digest geometry_digest {};
    base::Sha256Digest physics_table_digest {};
    base::Sha256Digest physics_data_digest {};
    std::vector<phys::PhysicsResourceDesc> physics_table;
    std::optional<cooked::CookedAsset> scene_asset;
    std::optional<cooked::CookedAsset> sidecar_asset;
    std::optional<cooked::CookedAsset> material_asset;
    std::optional<cooked::CookedAsset> shape_asset;
    std::optional<cooked::CookedAsset> geometry_asset;
  };

  //! Cooks geometry, scene, material and compound shape. Returns an error
  //! message, or an empty string on success.
  auto CookComplexDependencies(AsyncImportService& service,
    const std::filesystem::path& cooked_root) -> std::string_view
  {
    std::filesystem::create_directories(cooked_root);
    RegisterStubGeometryAsset(cooked_root);

    const auto scene_report = SubmitAndWait(
      service, MakeSceneDescriptorRequest(cooked_root, kSceneName, 10U));
    if (!scene_report.has_value() || !scene_report->success) {
      return "Scene descriptor cook failed";
    }
    const auto material_report
      = SubmitAndWait(service, MakePhysicsMaterialRequest(cooked_root));
    if (!material_report.has_value() || !material_report->success) {
      return "Physics material cook failed";
    }
    const auto shape_report
      = SubmitAndWait(service, MakeCompoundShapeRequest(cooked_root));
    if (!shape_report.has_value() || !shape_report->success) {
      return "Compound shape cook failed";
    }
    return {};
  }

  //! Cooks the complex sidecar. Returns an error message, or an empty string
  //! on success.
  auto CookComplexSidecar(AsyncImportService& service,
    const std::filesystem::path& cooked_root,
    const std::vector<uint32_t>& pinned_vertices,
    const std::vector<uint32_t>& kinematic_vertices,
    ImportReport* report_out = nullptr) -> std::string
  {
    const auto bindings
      = BuildComplexSidecarBindings(pinned_vertices, kinematic_vertices);
    auto report = SubmitAndWait(service,
      MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath, bindings));
    const auto success = report.has_value() && report->success;
    if (report_out != nullptr && report.has_value()) {
      *report_out = std::move(*report);
    }
    return success ? std::string {} : std::string { "Sidecar cook failed" };
  }

  //! Digests and inspection entries of every cooked file in the complex scene.
  auto TakeCookedSnapshot(const std::filesystem::path& cooked_root,
    CookedSnapshot& snapshot) -> std::string
  {
    const auto path
      = [&](const std::string_view relpath) -> std::filesystem::path {
      return CookedPath(cooked_root, relpath);
    };
    snapshot.scene_digest
      = ComputeFileDigest(path("Scenes/ComplexScene.oscene"));
    snapshot.sidecar_digest
      = ComputeFileDigest(path("Scenes/ComplexScene.opscene"));
    snapshot.material_digest
      = ComputeFileDigest(path("Physics/Materials/ground.opmat"));
    snapshot.shape_digest
      = ComputeFileDigest(path("Physics/Shapes/chassis_compound.ocshape"));
    snapshot.geometry_digest = StubGeometryDigest(cooked_root);
    snapshot.physics_table_digest
      = ComputeFileDigest(path("Physics/Resources/physics.table"));
    snapshot.physics_data_digest
      = ComputeFileDigest(path("Physics/Resources/physics.data"));
    snapshot.physics_table
      = ParsePhysicsResourceTable(path("Physics/Resources/physics.table"));

    const auto inspection = OpenCooked(cooked_root);
    snapshot.scene_asset = FindInspectionAsset(
      inspection, data::AssetKey::FromVirtualPath(kSceneVirtualPath));
    snapshot.sidecar_asset = FindInspectionAsset(
      inspection, data::AssetKey::FromVirtualPath(kSidecarVirtualPath));
    snapshot.material_asset = FindInspectionAsset(
      inspection, data::AssetKey::FromVirtualPath(kMaterialVirtualPath));
    snapshot.shape_asset = FindInspectionAsset(
      inspection, data::AssetKey::FromVirtualPath(kShapeVirtualPath));
    snapshot.geometry_asset = FindInspectionAsset(
      inspection, data::AssetKey::FromVirtualPath(kGeometryVirtualPath));
    if (!snapshot.scene_asset.has_value() || !snapshot.sidecar_asset.has_value()
      || !snapshot.material_asset.has_value()
      || !snapshot.shape_asset.has_value()
      || !snapshot.geometry_asset.has_value()) {
      return "Expected every cooked asset to be present in the inspection";
    }
    return {};
  }

  //! Cooks the complex physics scene once for the whole suite; every test
  //! reads the cooked files without modifying them.
  class PhysicsComplexSceneTest : public ::testing::Test {
  protected:
    struct ConstraintResources {
      phys::PhysicsResourceDesc soft {};
      phys::PhysicsResourceDesc joint {};
      phys::PhysicsResourceDesc vehicle {};
    };

    static auto SetUpTestSuite() -> void
    {
      temp_ = std::make_unique<oxygen::testing::ScopedTempDir>();
      try {
        Cook();
      } catch (const std::exception& error) {
        error_ = std::string("Suite setup threw: ") + error.what();
      }
    }

    static auto TearDownTestSuite() -> void
    {
      sidecar_bytes_.clear();
      parsed_sidecar_.reset();
      sidecar_report_ = {};
      error_.clear();
      temp_.reset();
    }

    static auto Cook() -> void
    {
      cooked_root_ = temp_->Path() / "complex_fixture";
      auto service = AsyncImportService(AsyncImportService::Config {
        .thread_pool_size = 2U,
      });
      [[maybe_unused]] auto stop_service
        = oxygen::Finally([&service] -> void { service.Stop(); });
      error_ = CookComplexDependencies(service, cooked_root_);
      if (!error_.empty()) {
        return;
      }
      error_ = CookComplexSidecar(
        service, cooked_root_, { 0U, 2U, 4U }, { 1U, 3U }, &sidecar_report_);
      if (!error_.empty()) {
        return;
      }
      sidecar_bytes_
        = oxygen::cooker::test::ReadBytes(Path("Scenes/ComplexScene.opscene"));
      parsed_sidecar_ = ParsePhysicsSidecarFile(sidecar_bytes_);
      if (!parsed_sidecar_.has_value()) {
        error_ = "Expected parsed_sidecar to contain a value";
      }
    }

    [[nodiscard]] static auto Path(const std::string_view relpath)
      -> std::filesystem::path
    {
      return CookedPath(cooked_root_, relpath);
    }

    template <typename Record>
    [[nodiscard]] static auto ReadFirstRecord(
      const phys::PhysicsBindingType type, size_t* record_offset = nullptr)
      -> std::optional<Record>
    {
      if (!parsed_sidecar_.has_value()) {
        return std::nullopt;
      }
      const auto table = FindTable(*parsed_sidecar_, type);
      if (!table.has_value()) {
        return std::nullopt;
      }
      const auto offset = static_cast<size_t>(table->table.offset);
      if (record_offset != nullptr) {
        *record_offset = offset;
      }
      const auto record = oxygen::serio::CopyAt<Record>(
        std::span { sidecar_bytes_ }, oxygen::serio::ByteOffset { offset });
      if (!record) {
        return std::nullopt;
      }
      return *record;
    }

    static auto LoadConstraintResources(ConstraintResources& resources) -> void
    {
      const auto soft_record = ReadFirstRecord<phys::SoftBodyBindingRecord>(
        phys::PhysicsBindingType::kSoftBody);
      const auto joint_record = ReadFirstRecord<phys::JointBindingRecord>(
        phys::PhysicsBindingType::kJoint);
      const auto vehicle_record = ReadFirstRecord<phys::VehicleBindingRecord>(
        phys::PhysicsBindingType::kVehicle);
      ASSERT_HAS_VALUE(soft_record);
      ASSERT_HAS_VALUE(joint_record);
      ASSERT_HAS_VALUE(vehicle_record);

      const auto physics_table
        = ParsePhysicsResourceTable(Path("Physics/Resources/physics.table"));
      ASSERT_FALSE(physics_table.empty());
      const auto* soft_resource = FindPhysicsResourceByAssetKey(
        physics_table, soft_record->topology_asset_key);
      const auto* joint_resource = FindPhysicsResourceByAssetKey(
        physics_table, joint_record->constraint_asset_key);
      const auto* vehicle_resource = FindPhysicsResourceByAssetKey(
        physics_table, vehicle_record->constraint_asset_key);
      ASSERT_NE(soft_resource, nullptr);
      ASSERT_NE(joint_resource, nullptr);
      ASSERT_NE(vehicle_resource, nullptr);
      resources.soft = *soft_resource;
      resources.joint = *joint_resource;
      resources.vehicle = *vehicle_resource;
    }

    static inline std::unique_ptr<oxygen::testing::ScopedTempDir> temp_;
    static inline std::string error_;
    static inline std::filesystem::path cooked_root_;
    static inline ImportReport sidecar_report_ {};
    static inline std::vector<std::byte> sidecar_bytes_;
    static inline std::optional<ParsedPhysicsSidecarFile> parsed_sidecar_;
  };

  //! Cooks the complex scene, then recooks the same sidecar bindings, once for
  //! the whole suite; every test compares the two read-only snapshots.
  class PhysicsRepeatRecookTest : public ::testing::Test {
  protected:
    static auto SetUpTestSuite() -> void
    {
      temp_ = std::make_unique<oxygen::testing::ScopedTempDir>();
      try {
        Cook();
      } catch (const std::exception& error) {
        error_ = std::string("Suite setup threw: ") + error.what();
      }
    }

    static auto TearDownTestSuite() -> void
    {
      before_ = {};
      after_ = {};
      error_.clear();
      temp_.reset();
    }

    static auto Cook() -> void
    {
      const auto cooked_root = temp_->Path() / "repeat_recook_hash_stability";
      auto service = AsyncImportService(AsyncImportService::Config {
        .thread_pool_size = 2U,
      });
      [[maybe_unused]] auto stop_service
        = oxygen::Finally([&service] -> void { service.Stop(); });
      error_ = CookComplexDependencies(service, cooked_root);
      if (!error_.empty()) {
        return;
      }
      error_
        = CookComplexSidecar(service, cooked_root, { 0U, 2U, 4U }, { 1U, 3U });
      if (!error_.empty()) {
        return;
      }
      error_ = TakeCookedSnapshot(cooked_root, before_);
      if (!error_.empty()) {
        return;
      }
      if (before_.physics_table.empty()) {
        error_ = "Expected a non-empty physics resource table";
        return;
      }
      error_
        = CookComplexSidecar(service, cooked_root, { 0U, 2U, 4U }, { 1U, 3U });
      if (!error_.empty()) {
        return;
      }
      error_ = TakeCookedSnapshot(cooked_root, after_);
    }

    static inline std::unique_ptr<oxygen::testing::ScopedTempDir> temp_;
    static inline std::string error_;
    static inline CookedSnapshot before_;
    static inline CookedSnapshot after_;
  };

} // namespace

NOLINT_TEST_F(
  PhysicsComplexSceneTest, SidecarCookEmitsSidecarAndPhysicsResources)
{
  ASSERT_TRUE(error_.empty()) << error_;
  EXPECT_TRUE(HasOutputPath(sidecar_report_, "Scenes/ComplexScene.opscene"));
  EXPECT_TRUE(
    HasOutputPath(sidecar_report_, "Physics/Resources/physics.table"));
  EXPECT_TRUE(HasOutputPath(sidecar_report_, "Physics/Resources/physics.data"));
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, CompoundShapeDescriptorStoresBoxAndSphereChildren)
{
  ASSERT_TRUE(error_.empty()) << error_;

  const auto shape_bytes = oxygen::cooker::test::ReadBytes(
    Path("Physics/Shapes/chassis_compound.ocshape"));
  const auto shape_desc = oxygen::serio::CopyAt<phys::CollisionShapeAssetDesc>(
    std::span { shape_bytes });
  ASSERT_HAS_VALUE(shape_desc) << "Expected shape_desc to contain a value";
  EXPECT_EQ(shape_desc->shape_type, phys::ShapeType::kCompound);
  EXPECT_EQ(shape_desc->shape_params.compound.child_count, 2U);

  const auto child0 = oxygen::serio::CopyAt<phys::CompoundShapeChildDesc>(
    std::span { shape_bytes },
    oxygen::serio::ByteOffset {
      shape_desc->shape_params.compound.child_byte_offset });
  const auto child1 = oxygen::serio::CopyAt<phys::CompoundShapeChildDesc>(
    std::span { shape_bytes },
    oxygen::serio::ByteOffset {
      static_cast<size_t>(shape_desc->shape_params.compound.child_byte_offset)
      + sizeof(phys::CompoundShapeChildDesc) });
  ASSERT_HAS_VALUE(child0) << "Expected child0 to contain a value";
  ASSERT_HAS_VALUE(child1) << "Expected child1 to contain a value";
  EXPECT_EQ(
    static_cast<phys::ShapeType>(child0->shape_type), phys::ShapeType::kBox);
  EXPECT_EQ(
    static_cast<phys::ShapeType>(child1->shape_type), phys::ShapeType::kSphere);
}

NOLINT_TEST_F(PhysicsComplexSceneTest,
  SidecarDescriptorTargetsCookedSceneByKeyNodeCountAndHash)
{
  ASSERT_TRUE(error_.empty()) << error_;

  const auto scene_bytes
    = oxygen::cooker::test::ReadBytes(Path("Scenes/ComplexScene.oscene"));
  ASSERT_FALSE(scene_bytes.empty());
  const auto scene_hash = data::ComputeDescriptorHash(scene_bytes);
  if (!parsed_sidecar_.has_value()) {
    FAIL() << "Expected the sidecar to be parsed";
  }

  EXPECT_EQ(parsed_sidecar_->descriptor.target_scene_key,
    data::AssetKey::FromVirtualPath(kSceneVirtualPath));
  EXPECT_EQ(parsed_sidecar_->descriptor.target_node_count, 10U);
  EXPECT_TRUE(std::equal(scene_hash.begin(), scene_hash.end(),
    std::begin(parsed_sidecar_->descriptor.target_scene_content_hash)));
  EXPECT_EQ(parsed_sidecar_->descriptor.component_table_count,
    static_cast<uint32_t>(parsed_sidecar_->directory.size()));
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, SidecarComponentDirectoryIsSortedByBindingType)
{
  ASSERT_TRUE(error_.empty()) << error_;
  if (!parsed_sidecar_.has_value()) {
    FAIL() << "Expected the sidecar to be parsed";
  }

  for (size_t i = 1; i < parsed_sidecar_->directory.size(); ++i) {
    EXPECT_LT(
      static_cast<uint32_t>(parsed_sidecar_->directory.at(i - 1).binding_type),
      static_cast<uint32_t>(parsed_sidecar_->directory.at(i).binding_type));
  }
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, RigidBodyRecordReferencesShapeAndMaterialAssets)
{
  ASSERT_TRUE(error_.empty()) << error_;

  const auto rigid_record = ReadFirstRecord<phys::RigidBodyBindingRecord>(
    phys::PhysicsBindingType::kRigidBody);
  ASSERT_HAS_VALUE(rigid_record) << "Expected rigid_record to contain a value";
  EXPECT_EQ(rigid_record->shape_asset_key,
    data::AssetKey::FromVirtualPath(kShapeVirtualPath));
  EXPECT_EQ(rigid_record->material_asset_key,
    data::AssetKey::FromVirtualPath(kMaterialVirtualPath));
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, SoftBodyRecordStoresPinnedAndKinematicVertexArrays)
{
  ASSERT_TRUE(error_.empty()) << error_;

  auto soft_record_offset = size_t { 0U };
  const auto soft_record = ReadFirstRecord<phys::SoftBodyBindingRecord>(
    phys::PhysicsBindingType::kSoftBody, &soft_record_offset);
  ASSERT_HAS_VALUE(soft_record) << "Expected soft_record to contain a value";
  EXPECT_EQ(soft_record->collision_layer, 2U);
  EXPECT_EQ(soft_record->collision_mask, 0xFFFFFFFFU);
  EXPECT_EQ(soft_record->pinned_vertex_count, 3U);
  EXPECT_EQ(soft_record->kinematic_vertex_count, 2U);
  EXPECT_EQ(soft_record->pinned_vertex_byte_offset,
    static_cast<uint32_t>(sizeof(phys::SoftBodyBindingRecord)));
  EXPECT_EQ(soft_record->kinematic_vertex_byte_offset,
    static_cast<uint32_t>(
      sizeof(phys::SoftBodyBindingRecord) + (3U * sizeof(uint32_t))));

  const auto pinned_vertices = ReadStructArrayAt<uint32_t>(sidecar_bytes_,
    {
      .offset = soft_record_offset + soft_record->pinned_vertex_byte_offset,
      .count = soft_record->pinned_vertex_count,
      .entry_size = sizeof(uint32_t),
    });
  const auto kinematic_vertices = ReadStructArrayAt<uint32_t>(sidecar_bytes_,
    {
      .offset = soft_record_offset + soft_record->kinematic_vertex_byte_offset,
      .count = soft_record->kinematic_vertex_count,
      .entry_size = sizeof(uint32_t),
    });
  EXPECT_EQ(pinned_vertices, (std::vector<uint32_t> { 0U, 2U, 4U }));
  EXPECT_EQ(kinematic_vertices, (std::vector<uint32_t> { 1U, 3U }));
  EXPECT_FALSE(soft_record->topology_asset_key.IsNil());
}

NOLINT_TEST_F(PhysicsComplexSceneTest, JointRecordReferencesConstraintAsset)
{
  ASSERT_TRUE(error_.empty()) << error_;

  const auto joint_record = ReadFirstRecord<phys::JointBindingRecord>(
    phys::PhysicsBindingType::kJoint);
  ASSERT_HAS_VALUE(joint_record) << "Expected joint_record to contain a value";
  EXPECT_FALSE(joint_record->constraint_asset_key.IsNil());
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, VehicleRecordDescribesWheelSliceAndConstraintAsset)
{
  ASSERT_TRUE(error_.empty()) << error_;

  const auto vehicle_record = ReadFirstRecord<phys::VehicleBindingRecord>(
    phys::PhysicsBindingType::kVehicle);
  ASSERT_HAS_VALUE(vehicle_record)
    << "Expected vehicle_record to contain a value";
  EXPECT_EQ(vehicle_record->wheel_slice_offset, 0U);
  EXPECT_EQ(vehicle_record->wheel_slice_count, 4U);
  EXPECT_FALSE(vehicle_record->constraint_asset_key.IsNil());
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, VehicleWheelRecordsPreserveNodeAxleSideAndCastor)
{
  ASSERT_TRUE(error_.empty()) << error_;
  if (!parsed_sidecar_.has_value()) {
    FAIL() << "Expected the sidecar to be parsed";
  }

  const auto wheel_table
    = FindTable(*parsed_sidecar_, phys::PhysicsBindingType::kVehicleWheel);
  ASSERT_HAS_VALUE(wheel_table) << "Expected wheel_table to contain a value";
  const auto wheel_records
    = ReadStructArrayAt<phys::VehicleWheelBindingRecord>(sidecar_bytes_,
      {
        .offset = static_cast<size_t>(wheel_table->table.offset),
        .count = wheel_table->table.count,
        .entry_size = wheel_table->table.entry_size,
      });
  ASSERT_EQ(wheel_records.size(), 4U);
  EXPECT_EQ(wheel_records.at(0).wheel_node_index, 5U);
  EXPECT_EQ(wheel_records.at(0).axle_index, 0U);
  EXPECT_EQ(wheel_records.at(0).side, phys::VehicleWheelSide::kLeft);
  EXPECT_FLOAT_EQ(wheel_records.at(0).backend_scalars.jolt.wheel_castor, 0.50F);
  EXPECT_EQ(wheel_records.at(1).wheel_node_index, 6U);
  EXPECT_EQ(wheel_records.at(1).axle_index, 0U);
  EXPECT_EQ(wheel_records.at(1).side, phys::VehicleWheelSide::kRight);
  EXPECT_FLOAT_EQ(wheel_records.at(1).backend_scalars.jolt.wheel_castor, 0.60F);
  EXPECT_EQ(wheel_records.at(2).wheel_node_index, 7U);
  EXPECT_EQ(wheel_records.at(2).axle_index, 1U);
  EXPECT_EQ(wheel_records.at(2).side, phys::VehicleWheelSide::kLeft);
  EXPECT_FLOAT_EQ(wheel_records.at(2).backend_scalars.jolt.wheel_castor, 0.70F);
  EXPECT_EQ(wheel_records.at(3).wheel_node_index, 8U);
  EXPECT_EQ(wheel_records.at(3).axle_index, 1U);
  EXPECT_EQ(wheel_records.at(3).side, phys::VehicleWheelSide::kRight);
  EXPECT_FLOAT_EQ(wheel_records.at(3).backend_scalars.jolt.wheel_castor, 0.80F);
}

NOLINT_TEST_F(PhysicsComplexSceneTest, ConstraintResourcesUseJoltBinaryFormats)
{
  ASSERT_TRUE(error_.empty()) << error_;
  ConstraintResources resources;
  ASSERT_NO_FATAL_FAILURE(LoadConstraintResources(resources));

  EXPECT_EQ(resources.soft.format,
    phys::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary);
  EXPECT_EQ(
    resources.joint.format, phys::PhysicsResourceFormat::kJoltConstraintBinary);
  EXPECT_EQ(resources.vehicle.format,
    phys::PhysicsResourceFormat::kJoltVehicleConstraintBinary);
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, ConstraintResourcesAreNotLegacyAuthoredPayloads)
{
  ASSERT_TRUE(error_.empty()) << error_;
  ConstraintResources resources;
  ASSERT_NO_FATAL_FAILURE(LoadConstraintResources(resources));

  const auto physics_data
    = oxygen::cooker::test::ReadBytes(Path("Physics/Resources/physics.data"));
  ASSERT_FALSE(physics_data.empty());
  const auto is_legacy_authored_magic
    = [&](const phys::PhysicsResourceDesc& resource) -> bool {
    if (resource.size_bytes < 4U) {
      return false;
    }
    const auto offset = static_cast<size_t>(resource.data_offset);
    const auto size = static_cast<size_t>(resource.size_bytes);
    if (offset > physics_data.size() || physics_data.size() - offset < size) {
      return false;
    }
    return physics_data.at(offset + 0U) == std::byte { 'O' }
    && physics_data.at(offset + 1U) == std::byte { 'P' }
    && physics_data.at(offset + 2U) == std::byte { 'H' }
    && physics_data.at(offset + 3U) == std::byte { 'B' };
  };
  EXPECT_FALSE(is_legacy_authored_magic(resources.soft));
  EXPECT_FALSE(is_legacy_authored_magic(resources.joint));
  EXPECT_FALSE(is_legacy_authored_magic(resources.vehicle));
}

NOLINT_TEST(PhysicsPhase3ClosureTest,
  IncrementalRecookUpdatesOnlySidecarAndKeepsUnchangedAssetsStable)
{
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  const oxygen::testing::ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "incremental_recook_stability";
  std::filesystem::create_directories(cooked_root);

  RegisterStubGeometryAsset(cooked_root);

  const auto scene_report = SubmitAndWait(
    service, MakeSceneDescriptorRequest(cooked_root, kSceneName, 10U));
  ASSERT_HAS_VALUE(scene_report);
  ASSERT_TRUE(scene_report->success);
  const auto material_report
    = SubmitAndWait(service, MakePhysicsMaterialRequest(cooked_root));
  ASSERT_HAS_VALUE(material_report);
  ASSERT_TRUE(material_report->success);
  const auto shape_report
    = SubmitAndWait(service, MakeCompoundShapeRequest(cooked_root));
  ASSERT_HAS_VALUE(shape_report);
  ASSERT_TRUE(shape_report->success);

  const auto first_bindings
    = BuildComplexSidecarBindings({ 0U, 2U, 4U }, { 1U, 3U });
  const auto first_sidecar_report = SubmitAndWait(service,
    MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath, first_bindings));
  ASSERT_HAS_VALUE(first_sidecar_report);
  ASSERT_TRUE(first_sidecar_report->success);

  // The digests are those of the descriptors the index lists: a changed
  // descriptor is a new file, and the old one stays while the inspection
  // below holds its commit.
  const auto digest_of
    = [&](const std::string_view virtual_path) -> base::Sha256Digest {
    return DescriptorDigest(
      cooked_root, data::AssetKey::FromVirtualPath(virtual_path));
  };
  const auto scene_digest_before = digest_of(kSceneVirtualPath);
  const auto sidecar_digest_before = digest_of(kSidecarVirtualPath);
  const auto material_digest_before = digest_of(kMaterialVirtualPath);
  const auto shape_digest_before = digest_of(kShapeVirtualPath);
  const auto geometry_digest_before = StubGeometryDigest(cooked_root);

  const auto before_inspection = OpenCooked(cooked_root);
  const auto scene_asset_before = FindInspectionAsset(
    before_inspection, data::AssetKey::FromVirtualPath(kSceneVirtualPath));
  const auto material_asset_before = FindInspectionAsset(
    before_inspection, data::AssetKey::FromVirtualPath(kMaterialVirtualPath));
  const auto shape_asset_before = FindInspectionAsset(
    before_inspection, data::AssetKey::FromVirtualPath(kShapeVirtualPath));
  const auto geometry_asset_before = FindInspectionAsset(
    before_inspection, data::AssetKey::FromVirtualPath(kGeometryVirtualPath));
  ASSERT_HAS_VALUE(scene_asset_before)
    << "Expected scene_asset_before to contain a value";
  ASSERT_HAS_VALUE(material_asset_before)
    << "Expected material_asset_before to contain a value";
  ASSERT_HAS_VALUE(shape_asset_before)
    << "Expected shape_asset_before to contain a value";
  ASSERT_HAS_VALUE(geometry_asset_before)
    << "Expected geometry_asset_before to contain a value";

  const auto second_bindings
    = BuildComplexSidecarBindings({ 1U, 4U }, { 2U, 3U });
  const auto second_sidecar_report = SubmitAndWait(service,
    MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath, second_bindings));
  ASSERT_HAS_VALUE(second_sidecar_report);
  ASSERT_TRUE(second_sidecar_report->success);

  const auto scene_digest_after = digest_of(kSceneVirtualPath);
  const auto sidecar_digest_after = digest_of(kSidecarVirtualPath);
  const auto material_digest_after = digest_of(kMaterialVirtualPath);
  const auto shape_digest_after = digest_of(kShapeVirtualPath);
  const auto geometry_digest_after = StubGeometryDigest(cooked_root);

  EXPECT_EQ(scene_digest_before, scene_digest_after);
  EXPECT_EQ(material_digest_before, material_digest_after);
  EXPECT_EQ(shape_digest_before, shape_digest_after);
  EXPECT_EQ(geometry_digest_before, geometry_digest_after);
  EXPECT_NE(sidecar_digest_before, sidecar_digest_after);

  const auto after_inspection = OpenCooked(cooked_root);
  const auto scene_asset_after = FindInspectionAsset(
    after_inspection, data::AssetKey::FromVirtualPath(kSceneVirtualPath));
  const auto material_asset_after = FindInspectionAsset(
    after_inspection, data::AssetKey::FromVirtualPath(kMaterialVirtualPath));
  const auto shape_asset_after = FindInspectionAsset(
    after_inspection, data::AssetKey::FromVirtualPath(kShapeVirtualPath));
  const auto geometry_asset_after = FindInspectionAsset(
    after_inspection, data::AssetKey::FromVirtualPath(kGeometryVirtualPath));
  ASSERT_HAS_VALUE(scene_asset_after)
    << "Expected scene_asset_after to contain a value";
  ASSERT_HAS_VALUE(material_asset_after)
    << "Expected material_asset_after to contain a value";
  ASSERT_HAS_VALUE(shape_asset_after)
    << "Expected shape_asset_after to contain a value";
  ASSERT_HAS_VALUE(geometry_asset_after)
    << "Expected geometry_asset_after to contain a value";

  EXPECT_EQ(scene_asset_before->descriptor_sha256,
    scene_asset_after->descriptor_sha256);
  EXPECT_EQ(material_asset_before->descriptor_sha256,
    material_asset_after->descriptor_sha256);
  EXPECT_EQ(shape_asset_before->descriptor_sha256,
    shape_asset_after->descriptor_sha256);
  EXPECT_EQ(geometry_asset_before->descriptor_sha256,
    geometry_asset_after->descriptor_sha256);

  const auto sidecar_key = data::AssetKey::FromVirtualPath(kSidecarVirtualPath);
  const auto sidecar_entry = FindInspectionAsset(after_inspection, sidecar_key);
  if (!sidecar_entry.has_value()) {
    FAIL() << "Expected sidecar_entry to contain a value";
  }
  const auto sidecar_entry_count
    = std::ranges::count_if(after_inspection.Assets(),
      [&](const auto& asset) -> auto { return asset.key == sidecar_key; });
  EXPECT_EQ(sidecar_entry_count, 1);

  service.Stop();
}

NOLINT_TEST_F(
  PhysicsRepeatRecookTest, RepeatUnchangedRecookKeepsCookedFileDigestsStable)
{
  ASSERT_TRUE(error_.empty()) << error_;
  const auto& before = before_;
  const auto& after = after_;

  EXPECT_EQ(before.scene_digest, after.scene_digest);
  EXPECT_EQ(before.sidecar_digest, after.sidecar_digest);
  EXPECT_EQ(before.material_digest, after.material_digest);
  EXPECT_EQ(before.shape_digest, after.shape_digest);
  EXPECT_EQ(before.geometry_digest, after.geometry_digest);
  EXPECT_EQ(before.physics_table_digest, after.physics_table_digest);
  EXPECT_EQ(before.physics_data_digest, after.physics_data_digest);
}

NOLINT_TEST_F(PhysicsRepeatRecookTest,
  RepeatUnchangedRecookKeepsPhysicsResourceEntriesStable)
{
  ASSERT_TRUE(error_.empty()) << error_;
  const auto& before = before_;
  const auto& after = after_;

  ASSERT_EQ(before.physics_table.size(), after.physics_table.size());
  for (size_t i = 0; i < before.physics_table.size(); ++i) {
    const auto& before_entry = before.physics_table.at(i);
    const auto& after_entry = after.physics_table.at(i);
    EXPECT_EQ(before_entry.resource_asset_key, after_entry.resource_asset_key);
    EXPECT_EQ(before_entry.format, after_entry.format);
    EXPECT_EQ(before_entry.size_bytes, after_entry.size_bytes);
    EXPECT_TRUE(std::equal(std::begin(before_entry.content_hash),
      std::end(before_entry.content_hash),
      std::begin(after_entry.content_hash)));
  }
}

NOLINT_TEST_F(PhysicsRepeatRecookTest,
  RepeatUnchangedRecookKeepsInspectionDescriptorHashesStable)
{
  ASSERT_TRUE(error_.empty()) << error_;
  const auto& before = before_;
  const auto& after = after_;
  if (!before.scene_asset.has_value() || !before.sidecar_asset.has_value()
    || !before.material_asset.has_value() || !before.shape_asset.has_value()
    || !before.geometry_asset.has_value() || !after.scene_asset.has_value()
    || !after.sidecar_asset.has_value() || !after.material_asset.has_value()
    || !after.shape_asset.has_value() || !after.geometry_asset.has_value()) {
    FAIL() << "Expected every cooked asset in both inspection snapshots";
  }

  EXPECT_EQ(before.scene_asset->descriptor_sha256,
    after.scene_asset->descriptor_sha256);
  EXPECT_EQ(before.sidecar_asset->descriptor_sha256,
    after.sidecar_asset->descriptor_sha256);
  EXPECT_EQ(before.material_asset->descriptor_sha256,
    after.material_asset->descriptor_sha256);
  EXPECT_EQ(before.shape_asset->descriptor_sha256,
    after.shape_asset->descriptor_sha256);
  EXPECT_EQ(before.geometry_asset->descriptor_sha256,
    after.geometry_asset->descriptor_sha256);
}

NOLINT_TEST(PhysicsPhase3ClosureTest, SidecarCookFailsForAPhysxImport)
{
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  const oxygen::testing::ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "physx_import_refused";
  std::filesystem::create_directories(cooked_root);

  RegisterStubGeometryAsset(cooked_root);

  const auto scene_report = SubmitAndWait(
    service, MakeSceneDescriptorRequest(cooked_root, kSceneName, 10U));
  ASSERT_HAS_VALUE(scene_report);
  ASSERT_TRUE(scene_report->success);
  const auto material_report
    = SubmitAndWait(service, MakePhysicsMaterialRequest(cooked_root));
  ASSERT_HAS_VALUE(material_report);
  ASSERT_TRUE(material_report->success);
  const auto shape_report
    = SubmitAndWait(service, MakeCompoundShapeRequest(cooked_root));
  ASSERT_HAS_VALUE(shape_report);
  ASSERT_TRUE(shape_report->success);

  auto request = MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath,
    BuildComplexSidecarBindings({ 0U, 2U, 4U }, { 1U, 3U }));
  request.options.physics.backend = core::meta::physics::PhysicsBackend::kPhysX;
  const auto report = SubmitAndWait(service, std::move(request));
  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);

  // Only Jolt has a runtime backend (abdes/DroidNet#133).
  const auto has_backend_unsupported = std::ranges::any_of(
    report->diagnostics, [](const auto& diagnostic) -> auto {
      return diagnostic.code == import_codes::kPhysicsSidecarBackendUnsupported;
    });
  EXPECT_TRUE(has_backend_unsupported);

  service.Stop();
}

namespace {

  //! Cooks the complex sidecar with `pinned` source vertices and returns the
  //! diagnostic codes its report carries.
  auto CookComplexSidecarCodes(const std::string_view temp_name,
    const std::vector<uint32_t>& pinned) -> std::vector<std::string>
  {
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });
    const oxygen::testing::ScopedTempDir temp;
    const auto cooked_root = temp.Path() / temp_name;
    std::filesystem::create_directories(cooked_root);

    RegisterStubGeometryAsset(cooked_root);
    const auto scene_report = SubmitAndWait(
      service, MakeSceneDescriptorRequest(cooked_root, kSceneName, 10U));
    const auto material_report
      = SubmitAndWait(service, MakePhysicsMaterialRequest(cooked_root));
    const auto shape_report
      = SubmitAndWait(service, MakeCompoundShapeRequest(cooked_root));
    if (!scene_report.has_value() || !scene_report->success
      || !material_report.has_value() || !material_report->success
      || !shape_report.has_value() || !shape_report->success) {
      ADD_FAILURE() << "The complex scene's inputs must cook";
      return {};
    }

    const auto report = SubmitAndWait(service,
      MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath,
        BuildComplexSidecarBindings(pinned, { 1U, 3U })));
    auto codes = std::vector<std::string> {};
    if (report.has_value()) {
      for (const auto& diagnostic : report->diagnostics) {
        codes.push_back(diagnostic.code);
      }
    }
    // A binding that fails to cook leaves the sidecar and every backend
    // resource of the other bindings unpublished.
    if (report.has_value() && !report->success) {
      EXPECT_FALSE(oxygen::cooker::test::HasCookedFile(
        cooked_root, "Scenes/ComplexScene.opscene"));
      EXPECT_EQ(OpenCooked(cooked_root)
                  .ResourceTable(cooked::TableKind::kPhysics)
                  .RowCount(),
        0U);
    }
    service.Stop();
    return codes;
  }

} // namespace

//! A pin names a source mesh vertex; one no face uses is not on the surface.
NOLINT_TEST(
  PhysicsPhase3ClosureTest, SoftBodyPinOnAVertexNoFaceUsesFailsTheCook)
{
  const auto codes = CookComplexSidecarCodes("pin_not_on_surface", { 0U, 5U });
  EXPECT_NE(std::ranges::find(codes,
              std::string(import_codes::kPhysicsSidecarSoftBodyPinInvalid)),
    codes.end());
}

} // namespace oxygen::content::import::test
