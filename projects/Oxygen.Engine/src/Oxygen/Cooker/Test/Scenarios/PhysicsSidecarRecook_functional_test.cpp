//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/AsyncImportService.cpp,
//   Import/Internal/Jobs/PhysicsSidecarImportJob.cpp, Loose/Inspection.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <latch>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  namespace phys = oxygen::data::pak::physics;

  constexpr auto kSceneName = std::string_view { "ComplexScene" };
  constexpr auto kSceneVirtualPath
    = std::string_view { "/.cooked/Scenes/ComplexScene.oscene" };
  constexpr auto kSidecarVirtualPath
    = std::string_view { "/.cooked/Scenes/ComplexScene.opscene" };
  constexpr auto kMaterialVirtualPath
    = std::string_view { "/.cooked/Physics/Materials/ground.opmat" };
  constexpr auto kShapeVirtualPath
    = std::string_view { "/.cooked/Physics/Shapes/chassis_compound.ocshape" };
  constexpr auto kGeometryVirtualPath
    = std::string_view { "/.cooked/Geometry/cloth.ogeo" };

  struct ParsedPhysicsSidecarFile final {
    phys::PhysicsSceneAssetDesc descriptor {};
    std::vector<phys::PhysicsComponentTableDesc> directory;
  };

  template <typename T>
  auto ReadStructAt(const std::vector<std::byte>& bytes, const size_t offset)
    -> std::optional<T>
  {
    if (offset + sizeof(T) > bytes.size()) {
      return std::nullopt;
    }
    auto value = T {};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
  }

  template <typename T>
  auto ReadStructArrayAt(const std::vector<std::byte>& bytes,
    const size_t offset, const uint32_t count, const uint32_t entry_size)
    -> std::vector<T>
  {
    if (entry_size != sizeof(T)) {
      return {};
    }
    const auto byte_size = static_cast<size_t>(count) * sizeof(T);
    if (offset + byte_size > bytes.size()) {
      return {};
    }
    auto out = std::vector<T>(count);
    std::memcpy(out.data(), bytes.data() + offset, byte_size);
    return out;
  }

  auto ReadUint32ArrayAt(const std::vector<std::byte>& bytes,
    const size_t offset, const uint32_t count) -> std::vector<uint32_t>
  {
    const auto byte_size = static_cast<size_t>(count) * sizeof(uint32_t);
    if (offset + byte_size > bytes.size()) {
      return {};
    }
    auto out = std::vector<uint32_t>(count);
    std::memcpy(out.data(), bytes.data() + offset, byte_size);
    return out;
  }

  auto ParsePhysicsSidecarFile(const std::vector<std::byte>& bytes)
    -> std::optional<ParsedPhysicsSidecarFile>
  {
    const auto descriptor = ReadStructAt<phys::PhysicsSceneAssetDesc>(bytes, 0);
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
      directory_offset, parsed.descriptor.component_table_count,
      sizeof(phys::PhysicsComponentTableDesc));
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
    return ReadStructArrayAt<phys::PhysicsResourceDesc>(
      bytes, 0, count, sizeof(phys::PhysicsResourceDesc));
  }

  auto FindPhysicsResourceByAssetKey(
    const std::vector<phys::PhysicsResourceDesc>& table,
    const data::AssetKey& asset_key) -> const phys::PhysicsResourceDesc*
  {
    const auto it = std::find_if(table.begin(), table.end(),
      [&](const phys::PhysicsResourceDesc& desc) -> bool {
        return desc.resource_asset_key == asset_key;
      });
    return it != table.end() ? &(*it) : nullptr;
  }

  auto FindInspectionAsset(const lc::Inspection& inspection,
    const data::AssetKey& key) -> std::optional<lc::Inspection::AssetEntry>
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
    return std::ranges::any_of(report.outputs,
      [&](const ImportOutputRecord& o) -> bool { return o.path == relpath; });
  }

  auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
    -> ImportReport
  {
    auto report = ImportReport {};
    std::latch done(1);
    const auto submitted = service.SubmitImport(
      std::move(request),
      [&report, &done](
        const ImportJobId /*job_id*/, const ImportReport& completed) -> void {
        report = completed;
        done.count_down();
      },
      nullptr);
    EXPECT_TRUE(submitted.has_value());
    done.wait();
    return report;
  }

  auto MakeSceneDescriptorRequest(const std::filesystem::path& cooked_root,
    const std::string_view scene_name, const uint32_t node_count)
    -> ImportRequest
  {
    auto nodes = json::array();
    nodes.push_back(json { { "name", "Root" } });
    for (uint32_t i = 1; i < node_count; ++i) {
      nodes.push_back(json {
        { "name", "Node" + std::to_string(i) },
        { "parent", 0U },
      });
    }
    const auto descriptor = json {
      { "version", data::pak::world::kSceneAssetVersion },
      { "name", scene_name },
      { "nodes", std::move(nodes) },
    };

    auto request = ImportRequest {};
    request.source_path = "inline://scene-descriptor";
    request.job_name = std::string(scene_name);
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .normalized_descriptor_json = descriptor.dump(),
    };
    return request;
  }

  auto MakePhysicsMaterialRequest(const std::filesystem::path& cooked_root)
    -> ImportRequest
  {
    const auto descriptor = json {
      { "name", "ground" },
      { "static_friction", 0.95F },
      { "dynamic_friction", 0.70F },
      { "restitution", 0.05F },
      { "density", 1800.0F },
      { "virtual_path", kMaterialVirtualPath },
    };

    auto request = ImportRequest {};
    request.source_path = "inline://physics-material";
    request.job_name = "ground.physics-material";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .normalized_descriptor_json = descriptor.dump(),
        };
    return request;
  }

  auto MakeCompoundShapeRequest(const std::filesystem::path& cooked_root)
    -> ImportRequest
  {
    const auto descriptor = json {
      { "name", "chassis_compound" },
      { "shape_type", "compound" },
      { "material_ref", kMaterialVirtualPath },
      { "virtual_path", kShapeVirtualPath },
      {
        "children",
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
        }),
      },
    };

    auto request = ImportRequest {};
    request.source_path = "inline://collision-shape";
    request.job_name = "chassis-compound";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.collision_shape_descriptor
      = ImportRequest::CollisionShapeDescriptorPayload {
          .normalized_descriptor_json = descriptor.dump(),
        };
    return request;
  }

  auto MakePhysicsSidecarRequest(const std::filesystem::path& cooked_root,
    const std::string_view target_scene_virtual_path, const json& bindings_doc)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    request.job_name = "complex-sidecar";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = std::string(target_scene_virtual_path),
      .inline_bindings_json = bindings_doc.dump(),
    };
    return request;
  }

  auto BuildComplexSidecarBindings(const std::vector<uint32_t>& pinned_vertices,
    const std::vector<uint32_t>& kinematic_vertices) -> json
  {
    return json {
      {
        "bindings",
        {
          {
            "rigid_bodies",
            json::array({
              json {
                { "node_index", 1U },
                { "shape_ref", kShapeVirtualPath },
                { "material_ref", kMaterialVirtualPath },
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
              },
            }),
          },
          {
            "soft_bodies",
            json::array({
              json {
                { "node_index", 2U },
                { "source_mesh_ref", kGeometryVirtualPath },
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
                    { "lra_stiffness_fraction", 0.9F },
                    { "skinned_constraint_enable", true },
                  },
                },
              },
            }),
          },
          {
            "joints",
            json::array({
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
                  json::array({ -0.1F, -0.1F, -0.1F, -0.1F, -0.1F, -0.1F }),
                },
                {
                  "limits_upper",
                  json::array({ 0.1F, 0.1F, 0.1F, 0.1F, 0.1F, 0.1F }),
                },
                {
                  "spring_stiffnesses",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
                },
                {
                  "spring_damping_ratios",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
                },
                {
                  "motor_modes",
                  json::array({ "off", "off", "off", "off", "off", "off" }),
                },
                {
                  "motor_target_velocities",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }),
                },
                {
                  "motor_target_positions",
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
                { "break_force", 1250.0F },
                { "break_torque", 2500.0F },
                { "collide_connected", false },
                { "priority", 7U },
                {
                  "backend",
                  {
                    { "target", "jolt" },
                    { "num_velocity_steps_override", 1U },
                    { "num_position_steps_override", 2U },
                  },
                },
              },
            }),
          },
          {
            "vehicles",
            json::array({
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
              },
            }),
          },
        },
      },
    };
  }

  auto RegisterStubGeometryAsset(const std::filesystem::path& cooked_root,
    const std::string_view virtual_path, const std::string_view relpath) -> void
  {
    const auto key = data::AssetKey::FromVirtualPath(virtual_path);
    namespace core = data::pak::core;
    namespace geometry = data::pak::geometry;

    struct VertexPosition final {
      float x;
      float y;
      float z;
    };
    static_assert(sizeof(VertexPosition) == sizeof(float) * 3U);

    const auto vertices = std::array<VertexPosition, 7> {
      VertexPosition { 0.0F, 0.15F, 0.0F },
      VertexPosition { -0.12F, -0.10F, -0.12F },
      VertexPosition { 0.12F, -0.10F, -0.12F },
      VertexPosition { 0.0F, -0.10F, 0.14F },
      VertexPosition { -0.06F, 0.02F, 0.08F },
      VertexPosition { 0.06F, 0.02F, 0.08F },
      VertexPosition { 0.0F, -0.02F, -0.04F },
    };
    const auto indices = std::array<uint32_t, 12> {
      0U,
      1U,
      2U, // top
      0U,
      2U,
      3U, // side
      0U,
      3U,
      1U, // side
      1U,
      3U,
      2U, // base
    };

    auto descriptor = geometry::GeometryAssetDesc {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    descriptor.header.version = geometry::kGeometryAssetVersion;
    std::memcpy(descriptor.header.name, "cloth", 5U);
    descriptor.lod_count = 1U;
    descriptor.bounding_box_min[0] = -0.12F;
    descriptor.bounding_box_min[1] = -0.10F;
    descriptor.bounding_box_min[2] = -0.12F;
    descriptor.bounding_box_max[0] = 0.12F;
    descriptor.bounding_box_max[1] = 0.15F;
    descriptor.bounding_box_max[2] = 0.14F;

    auto mesh = geometry::MeshDesc {};
    std::memcpy(mesh.name, "cloth_lod0", 10U);
    mesh.mesh_type = static_cast<uint8_t>(data::MeshType::kStandard);
    mesh.submesh_count = 1U;
    mesh.mesh_view_count = 1U;
    mesh.info.standard.vertex_buffer = data::ResourceReferenceIndex { 0U };
    mesh.info.standard.index_buffer = data::ResourceReferenceIndex { 1U };
    mesh.info.standard.bounding_box_min[0] = descriptor.bounding_box_min[0];
    mesh.info.standard.bounding_box_min[1] = descriptor.bounding_box_min[1];
    mesh.info.standard.bounding_box_min[2] = descriptor.bounding_box_min[2];
    mesh.info.standard.bounding_box_max[0] = descriptor.bounding_box_max[0];
    mesh.info.standard.bounding_box_max[1] = descriptor.bounding_box_max[1];
    mesh.info.standard.bounding_box_max[2] = descriptor.bounding_box_max[2];

    auto submesh = geometry::SubMeshDesc {};
    submesh.slot_id
      = data::MaterialSlotId::FromStableIdentity("PhysicsPhase3/cloth");
    std::memcpy(submesh.name, "cloth_submesh", 13U);
    submesh.mesh_view_count = 1U;
    submesh.bounding_box_min[0] = descriptor.bounding_box_min[0];
    submesh.bounding_box_min[1] = descriptor.bounding_box_min[1];
    submesh.bounding_box_min[2] = descriptor.bounding_box_min[2];
    submesh.bounding_box_max[0] = descriptor.bounding_box_max[0];
    submesh.bounding_box_max[1] = descriptor.bounding_box_max[1];
    submesh.bounding_box_max[2] = descriptor.bounding_box_max[2];

    auto view = geometry::MeshViewDesc {};
    view.first_index = 0U;
    view.index_count = static_cast<uint32_t>(indices.size());
    view.first_vertex = 0U;
    view.vertex_count = static_cast<uint32_t>(vertices.size());

    auto descriptor_bytes = std::vector<std::byte> {};
    descriptor_bytes.reserve(
      sizeof(descriptor) + sizeof(mesh) + sizeof(submesh) + sizeof(view));
    const auto append_pod
      = [&descriptor_bytes]<typename T>(const T& pod) -> auto {
      static_assert(std::is_trivially_copyable_v<T>);
      const auto* bytes = reinterpret_cast<const std::byte*>(&pod);
      descriptor_bytes.insert(descriptor_bytes.end(), bytes, bytes + sizeof(T));
    };
    append_pod(descriptor);
    append_pod(mesh);
    append_pod(submesh);
    append_pod(view);

    auto table_entries = std::array<core::BufferResourceDesc, 3> {};
    table_entries.at(1).data_offset = 0U;
    table_entries.at(1).size_bytes
      = static_cast<uint32_t>(vertices.size() * sizeof(VertexPosition));
    table_entries.at(1).usage_flags = 0x01U;
    table_entries.at(1).element_stride = sizeof(VertexPosition);
    table_entries.at(1).element_format = static_cast<uint8_t>(Format::kUnknown);

    table_entries.at(2).data_offset = table_entries.at(1).size_bytes;
    table_entries.at(2).size_bytes
      = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
    table_entries.at(2).usage_flags = 0x02U;
    table_entries.at(2).element_stride = 0U;
    table_entries.at(2).element_format = static_cast<uint8_t>(Format::kR32UInt);

    auto buffer_data = std::vector<std::byte> {};
    buffer_data.reserve(static_cast<size_t>(table_entries.at(1).size_bytes)
      + static_cast<size_t>(table_entries.at(2).size_bytes));
    const auto* vertex_bytes
      = reinterpret_cast<const std::byte*>(vertices.data());
    buffer_data.insert(buffer_data.end(), vertex_bytes,
      vertex_bytes + table_entries.at(1).size_bytes);
    const auto* index_bytes
      = reinterpret_cast<const std::byte*>(indices.data());
    buffer_data.insert(buffer_data.end(), index_bytes,
      index_bytes + table_entries.at(2).size_bytes);

    auto table_bytes = std::vector<std::byte> {};
    table_bytes.reserve(sizeof(table_entries));
    const auto* table_raw
      = reinterpret_cast<const std::byte*>(table_entries.data());
    table_bytes.insert(
      table_bytes.end(), table_raw, table_raw + sizeof(table_entries));

    auto writer = LooseCookedWriter(cooked_root);
    writer.WriteAssetDescriptor(key, data::AssetType::kGeometry, virtual_path,
      relpath, std::span<const std::byte>(descriptor_bytes),
      data::AssetReferences::Create(
        {
          { .kind = data::ResourceKind::kBuffer,
            .index = oxygen::ResourceIndexT { 1U } },
          { .kind = data::ResourceKind::kBuffer,
            .index = oxygen::ResourceIndexT { 2U } },
        },
        {})
        .value());
    (void)writer.Finish();

    const auto layout = LooseCookedLayout {};
    const auto table_path
      = cooked_root / std::filesystem::path(layout.BuffersTableRelPath());
    const auto data_path
      = cooked_root / std::filesystem::path(layout.BuffersDataRelPath());
    oxygen::cooker::test::WriteBytes(table_path, table_bytes);
    oxygen::cooker::test::WriteBytes(data_path, buffer_data);
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
    std::optional<lc::Inspection::AssetEntry> scene_asset;
    std::optional<lc::Inspection::AssetEntry> sidecar_asset;
    std::optional<lc::Inspection::AssetEntry> material_asset;
    std::optional<lc::Inspection::AssetEntry> shape_asset;
    std::optional<lc::Inspection::AssetEntry> geometry_asset;
  };

  //! Cooks geometry, scene, material and compound shape. Returns an error
  //! message, or an empty string on success.
  auto CookComplexDependencies(AsyncImportService& service,
    const std::filesystem::path& cooked_root) -> std::string
  {
    std::filesystem::create_directories(cooked_root);
    RegisterStubGeometryAsset(
      cooked_root, kGeometryVirtualPath, "Geometry/cloth.ogeo");

    if (!SubmitAndWait(
          service, MakeSceneDescriptorRequest(cooked_root, kSceneName, 10U))
          .success) {
      return "Scene descriptor cook failed";
    }
    if (!SubmitAndWait(service, MakePhysicsMaterialRequest(cooked_root))
          .success) {
      return "Physics material cook failed";
    }
    if (!SubmitAndWait(service, MakeCompoundShapeRequest(cooked_root))
          .success) {
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
    const auto success = report.success;
    if (report_out != nullptr) {
      *report_out = std::move(report);
    }
    return success ? std::string {} : std::string { "Sidecar cook failed" };
  }

  //! Digests and inspection entries of every cooked file in the complex scene.
  auto TakeCookedSnapshot(const std::filesystem::path& cooked_root,
    CookedSnapshot& snapshot) -> std::string
  {
    const auto path = [&](const std::string_view relpath) {
      return cooked_root / std::filesystem::path(relpath);
    };
    snapshot.scene_digest
      = ComputeFileDigest(path("Scenes/ComplexScene.oscene"));
    snapshot.sidecar_digest
      = ComputeFileDigest(path("Scenes/ComplexScene.opscene"));
    snapshot.material_digest
      = ComputeFileDigest(path("Physics/Materials/ground.opmat"));
    snapshot.shape_digest
      = ComputeFileDigest(path("Physics/Shapes/chassis_compound.ocshape"));
    snapshot.geometry_digest = ComputeFileDigest(path("Geometry/cloth.ogeo"));
    snapshot.physics_table_digest
      = ComputeFileDigest(path("Physics/Resources/physics.table"));
    snapshot.physics_data_digest
      = ComputeFileDigest(path("Physics/Resources/physics.data"));
    snapshot.physics_table
      = ParsePhysicsResourceTable(path("Physics/Resources/physics.table"));

    auto inspection = lc::Inspection {};
    inspection.LoadFromRoot(cooked_root);
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
      temp_ = std::make_unique<oxygen::cooker::test::ScopedTempDir>();
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
      return cooked_root_ / std::filesystem::path(relpath);
    }

    template <typename Record>
    [[nodiscard]] static auto ReadFirstRecord(
      const phys::PhysicsBindingType type, size_t* record_offset = nullptr)
      -> std::optional<Record>
    {
      const auto table = FindTable(*parsed_sidecar_, type);
      if (!table.has_value()) {
        return std::nullopt;
      }
      const auto offset = static_cast<size_t>(table->table.offset);
      if (record_offset != nullptr) {
        *record_offset = offset;
      }
      return ReadStructAt<Record>(sidecar_bytes_, offset);
    }

    static auto LoadConstraintResources(ConstraintResources& resources) -> void
    {
      const auto soft_record = ReadFirstRecord<phys::SoftBodyBindingRecord>(
        phys::PhysicsBindingType::kSoftBody);
      const auto joint_record = ReadFirstRecord<phys::JointBindingRecord>(
        phys::PhysicsBindingType::kJoint);
      const auto vehicle_record = ReadFirstRecord<phys::VehicleBindingRecord>(
        phys::PhysicsBindingType::kVehicle);
      ASSERT_TRUE(soft_record.has_value());
      ASSERT_TRUE(joint_record.has_value());
      ASSERT_TRUE(vehicle_record.has_value());

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

    static inline std::unique_ptr<oxygen::cooker::test::ScopedTempDir> temp_;
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
      temp_ = std::make_unique<oxygen::cooker::test::ScopedTempDir>();
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

    static inline std::unique_ptr<oxygen::cooker::test::ScopedTempDir> temp_;
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
  const auto shape_desc
    = ReadStructAt<phys::CollisionShapeAssetDesc>(shape_bytes, 0);
  ASSERT_TRUE(shape_desc.has_value())
    << "Expected shape_desc to contain a value";
  EXPECT_EQ(shape_desc->shape_type, phys::ShapeType::kCompound);
  EXPECT_EQ(shape_desc->shape_params.compound.child_count, 2U);

  const auto child0 = ReadStructAt<phys::CompoundShapeChildDesc>(
    shape_bytes, shape_desc->shape_params.compound.child_byte_offset);
  const auto child1 = ReadStructAt<phys::CompoundShapeChildDesc>(shape_bytes,
    static_cast<size_t>(shape_desc->shape_params.compound.child_byte_offset)
      + sizeof(phys::CompoundShapeChildDesc));
  ASSERT_TRUE(child0.has_value()) << "Expected child0 to contain a value";
  ASSERT_TRUE(child1.has_value()) << "Expected child1 to contain a value";
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
  const auto scene_hash = base::ComputeSha256(
    std::span<const std::byte>(scene_bytes.data(), scene_bytes.size()));

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
  ASSERT_TRUE(rigid_record.has_value())
    << "Expected rigid_record to contain a value";
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
  ASSERT_TRUE(soft_record.has_value())
    << "Expected soft_record to contain a value";
  EXPECT_EQ(soft_record->collision_layer, 2U);
  EXPECT_EQ(soft_record->collision_mask, 0xFFFFFFFFU);
  EXPECT_EQ(soft_record->pinned_vertex_count, 3U);
  EXPECT_EQ(soft_record->kinematic_vertex_count, 2U);
  EXPECT_EQ(soft_record->pinned_vertex_byte_offset,
    static_cast<uint32_t>(sizeof(phys::SoftBodyBindingRecord)));
  EXPECT_EQ(soft_record->kinematic_vertex_byte_offset,
    static_cast<uint32_t>(
      sizeof(phys::SoftBodyBindingRecord) + 3U * sizeof(uint32_t)));

  const auto pinned_vertices = ReadUint32ArrayAt(sidecar_bytes_,
    soft_record_offset + soft_record->pinned_vertex_byte_offset,
    soft_record->pinned_vertex_count);
  const auto kinematic_vertices = ReadUint32ArrayAt(sidecar_bytes_,
    soft_record_offset + soft_record->kinematic_vertex_byte_offset,
    soft_record->kinematic_vertex_count);
  EXPECT_EQ(pinned_vertices, (std::vector<uint32_t> { 0U, 2U, 4U }));
  EXPECT_EQ(kinematic_vertices, (std::vector<uint32_t> { 1U, 3U }));
  EXPECT_FALSE(soft_record->topology_asset_key.IsNil());
}

NOLINT_TEST_F(PhysicsComplexSceneTest, JointRecordReferencesConstraintAsset)
{
  ASSERT_TRUE(error_.empty()) << error_;

  const auto joint_record = ReadFirstRecord<phys::JointBindingRecord>(
    phys::PhysicsBindingType::kJoint);
  ASSERT_TRUE(joint_record.has_value())
    << "Expected joint_record to contain a value";
  EXPECT_FALSE(joint_record->constraint_asset_key.IsNil());
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, VehicleRecordDescribesWheelSliceAndConstraintAsset)
{
  ASSERT_TRUE(error_.empty()) << error_;

  const auto vehicle_record = ReadFirstRecord<phys::VehicleBindingRecord>(
    phys::PhysicsBindingType::kVehicle);
  ASSERT_TRUE(vehicle_record.has_value())
    << "Expected vehicle_record to contain a value";
  EXPECT_EQ(vehicle_record->wheel_slice_offset, 0U);
  EXPECT_EQ(vehicle_record->wheel_slice_count, 4U);
  EXPECT_FALSE(vehicle_record->constraint_asset_key.IsNil());
}

NOLINT_TEST_F(
  PhysicsComplexSceneTest, VehicleWheelRecordsPreserveNodeAxleSideAndCastor)
{
  ASSERT_TRUE(error_.empty()) << error_;

  const auto wheel_table
    = FindTable(*parsed_sidecar_, phys::PhysicsBindingType::kVehicleWheel);
  ASSERT_TRUE(wheel_table.has_value())
    << "Expected wheel_table to contain a value";
  const auto wheel_records = ReadStructArrayAt<phys::VehicleWheelBindingRecord>(
    sidecar_bytes_, static_cast<size_t>(wheel_table->table.offset),
    wheel_table->table.count, wheel_table->table.entry_size);
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
  const oxygen::cooker::test::ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "incremental_recook_stability";
  std::filesystem::create_directories(cooked_root);

  RegisterStubGeometryAsset(
    cooked_root, kGeometryVirtualPath, "Geometry/cloth.ogeo");

  ASSERT_TRUE(SubmitAndWait(
    service, MakeSceneDescriptorRequest(cooked_root, kSceneName, 10U))
      .success);
  ASSERT_TRUE(
    SubmitAndWait(service, MakePhysicsMaterialRequest(cooked_root)).success);
  ASSERT_TRUE(
    SubmitAndWait(service, MakeCompoundShapeRequest(cooked_root)).success);

  const auto first_bindings
    = BuildComplexSidecarBindings({ 0U, 2U, 4U }, { 1U, 3U });
  ASSERT_TRUE(SubmitAndWait(service,
    MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath, first_bindings))
      .success);

  const auto scene_path
    = cooked_root / std::filesystem::path("Scenes/ComplexScene.oscene");
  const auto sidecar_path
    = cooked_root / std::filesystem::path("Scenes/ComplexScene.opscene");
  const auto material_path
    = cooked_root / std::filesystem::path("Physics/Materials/ground.opmat");
  const auto shape_path = cooked_root
    / std::filesystem::path("Physics/Shapes/chassis_compound.ocshape");
  const auto geometry_path
    = cooked_root / std::filesystem::path("Geometry/cloth.ogeo");

  const auto scene_digest_before = ComputeFileDigest(scene_path);
  const auto sidecar_digest_before = ComputeFileDigest(sidecar_path);
  const auto material_digest_before = ComputeFileDigest(material_path);
  const auto shape_digest_before = ComputeFileDigest(shape_path);
  const auto geometry_digest_before = ComputeFileDigest(geometry_path);

  auto before_inspection = lc::Inspection {};
  before_inspection.LoadFromRoot(cooked_root);
  const auto scene_asset_before = FindInspectionAsset(
    before_inspection, data::AssetKey::FromVirtualPath(kSceneVirtualPath));
  const auto material_asset_before = FindInspectionAsset(
    before_inspection, data::AssetKey::FromVirtualPath(kMaterialVirtualPath));
  const auto shape_asset_before = FindInspectionAsset(
    before_inspection, data::AssetKey::FromVirtualPath(kShapeVirtualPath));
  const auto geometry_asset_before = FindInspectionAsset(
    before_inspection, data::AssetKey::FromVirtualPath(kGeometryVirtualPath));
  ASSERT_TRUE(scene_asset_before.has_value())
    << "Expected scene_asset_before to contain a value";
  ASSERT_TRUE(material_asset_before.has_value())
    << "Expected material_asset_before to contain a value";
  ASSERT_TRUE(shape_asset_before.has_value())
    << "Expected shape_asset_before to contain a value";
  ASSERT_TRUE(geometry_asset_before.has_value())
    << "Expected geometry_asset_before to contain a value";

  const auto second_bindings
    = BuildComplexSidecarBindings({ 1U, 4U, 6U }, { 2U, 3U });
  ASSERT_TRUE(SubmitAndWait(service,
    MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath, second_bindings))
      .success);

  const auto scene_digest_after = ComputeFileDigest(scene_path);
  const auto sidecar_digest_after = ComputeFileDigest(sidecar_path);
  const auto material_digest_after = ComputeFileDigest(material_path);
  const auto shape_digest_after = ComputeFileDigest(shape_path);
  const auto geometry_digest_after = ComputeFileDigest(geometry_path);

  EXPECT_EQ(scene_digest_before, scene_digest_after);
  EXPECT_EQ(material_digest_before, material_digest_after);
  EXPECT_EQ(shape_digest_before, shape_digest_after);
  EXPECT_EQ(geometry_digest_before, geometry_digest_after);
  EXPECT_NE(sidecar_digest_before, sidecar_digest_after);

  auto after_inspection = lc::Inspection {};
  after_inspection.LoadFromRoot(cooked_root);
  const auto scene_asset_after = FindInspectionAsset(
    after_inspection, data::AssetKey::FromVirtualPath(kSceneVirtualPath));
  const auto material_asset_after = FindInspectionAsset(
    after_inspection, data::AssetKey::FromVirtualPath(kMaterialVirtualPath));
  const auto shape_asset_after = FindInspectionAsset(
    after_inspection, data::AssetKey::FromVirtualPath(kShapeVirtualPath));
  const auto geometry_asset_after = FindInspectionAsset(
    after_inspection, data::AssetKey::FromVirtualPath(kGeometryVirtualPath));
  ASSERT_TRUE(scene_asset_after.has_value())
    << "Expected scene_asset_after to contain a value";
  ASSERT_TRUE(material_asset_after.has_value())
    << "Expected material_asset_after to contain a value";
  ASSERT_TRUE(shape_asset_after.has_value())
    << "Expected shape_asset_after to contain a value";
  ASSERT_TRUE(geometry_asset_after.has_value())
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

NOLINT_TEST(PhysicsPhase3ClosureTest,
  SidecarCookFailsWhenBindingBackendDoesNotMatchRequestedImportBackend)
{
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  const oxygen::cooker::test::ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "backend_mismatch_hard_fail";
  std::filesystem::create_directories(cooked_root);

  RegisterStubGeometryAsset(
    cooked_root, kGeometryVirtualPath, "Geometry/cloth.ogeo");

  ASSERT_TRUE(SubmitAndWait(
    service, MakeSceneDescriptorRequest(cooked_root, kSceneName, 10U))
      .success);
  ASSERT_TRUE(
    SubmitAndWait(service, MakePhysicsMaterialRequest(cooked_root)).success);
  ASSERT_TRUE(
    SubmitAndWait(service, MakeCompoundShapeRequest(cooked_root)).success);

  auto request = MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath,
    BuildComplexSidecarBindings({ 0U, 2U, 4U }, { 1U, 3U }));
  request.options.physics.backend = core::meta::physics::PhysicsBackend::kPhysX;
  const auto report = SubmitAndWait(service, std::move(request));
  EXPECT_FALSE(report.success);

  const auto has_backend_mismatch = std::ranges::any_of(
    report.diagnostics, [](const auto& diagnostic) -> auto {
      return diagnostic.code == "physics.sidecar.backend_mismatch";
    });
  EXPECT_TRUE(has_backend_mismatch);

  service.Stop();
}

} // namespace oxygen::content::import::test
