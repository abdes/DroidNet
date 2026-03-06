//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
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

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/PakFormat.h>

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

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root
      = std::filesystem::temp_directory_path() / "oxygen_physics_phase3";
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
    if (!in.is_open()) {
      return {};
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    auto bytes = std::vector<std::byte>(size);
    in.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(size));
    if (!(in.good() || in.eof())) {
      return {};
    }
    return bytes;
  }

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
    const auto bytes = ReadBinaryFile(path);
    if (bytes.empty()
      || (bytes.size() % sizeof(phys::PhysicsResourceDesc) != 0U)) {
      return {};
    }
    const auto count
      = static_cast<uint32_t>(bytes.size() / sizeof(phys::PhysicsResourceDesc));
    return ReadStructArrayAt<phys::PhysicsResourceDesc>(
      bytes, 0, count, sizeof(phys::PhysicsResourceDesc));
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
    const auto bytes = ReadBinaryFile(path);
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
      [&](const ImportOutputRecord& o) { return o.path == relpath; });
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
      { "children",
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
        }) },
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
      { "bindings",
        {
          { "rigid_bodies",
            json::array({
              json {
                { "node_index", 1U },
                { "shape_ref", kShapeVirtualPath },
                { "material_ref", kMaterialVirtualPath },
                { "body_type", "dynamic" },
                { "motion_quality", "linear_cast" },
                { "mass", 1350.0F },
                { "backend",
                  {
                    { "target", "jolt" },
                    { "num_velocity_steps_override", 2U },
                    { "num_position_steps_override", 3U },
                  } },
              },
            }) },
          { "soft_bodies",
            json::array({
              json {
                { "node_index", 2U },
                { "source_mesh_ref", kGeometryVirtualPath },
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
                { "backend",
                  {
                    { "target", "jolt" },
                    { "velocity_iteration_count", 8U },
                    { "lra_stiffness_fraction", 0.9F },
                    { "skinned_constraint_enable", true },
                  } },
              },
            }) },
          { "joints",
            json::array({
              json {
                { "node_index_a", 1U },
                { "node_index_b", "world" },
                { "constraint_type", "hinge" },
                { "constraint_space", "local" },
                { "local_frame_a_position", json::array({ 0.0F, 0.0F, 0.0F }) },
                { "local_frame_a_rotation",
                  json::array({ 0.0F, 0.0F, 0.0F, 1.0F }) },
                { "local_frame_b_position", json::array({ 0.0F, 0.0F, 0.0F }) },
                { "local_frame_b_rotation",
                  json::array({ 0.0F, 0.0F, 0.0F, 1.0F }) },
                { "limits_lower",
                  json::array({ -0.1F, -0.1F, -0.1F, -0.1F, -0.1F, -0.1F }) },
                { "limits_upper",
                  json::array({ 0.1F, 0.1F, 0.1F, 0.1F, 0.1F, 0.1F }) },
                { "spring_stiffnesses",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }) },
                { "spring_damping_ratios",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }) },
                { "motor_modes",
                  json::array({ "off", "off", "off", "off", "off", "off" }) },
                { "motor_target_velocities",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }) },
                { "motor_target_positions",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }) },
                { "motor_max_forces",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }) },
                { "motor_max_torques",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }) },
                { "motor_drive_frequencies",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }) },
                { "motor_damping_ratios",
                  json::array({ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F }) },
                { "break_force", 1250.0F },
                { "break_torque", 2500.0F },
                { "collide_connected", false },
                { "priority", 7U },
                { "backend",
                  {
                    { "target", "jolt" },
                    { "num_velocity_steps_override", 1U },
                    { "num_position_steps_override", 2U },
                  } },
              },
            }) },
          { "vehicles",
            json::array({
              json {
                { "node_index", 3U },
                { "controller_type", "wheeled" },
                { "wheels",
                  json::array({
                    json {
                      { "node_index", 8U },
                      { "axle_index", 1U },
                      { "side", "right" },
                      { "backend",
                        {
                          { "target", "jolt" },
                          { "wheel_castor", 0.80F },
                        } },
                    },
                    json {
                      { "node_index", 6U },
                      { "axle_index", 0U },
                      { "side", "right" },
                      { "backend",
                        {
                          { "target", "jolt" },
                          { "wheel_castor", 0.60F },
                        } },
                    },
                    json {
                      { "node_index", 7U },
                      { "axle_index", 1U },
                      { "side", "left" },
                      { "backend",
                        {
                          { "target", "jolt" },
                          { "wheel_castor", 0.70F },
                        } },
                    },
                    json {
                      { "node_index", 5U },
                      { "axle_index", 0U },
                      { "side", "left" },
                      { "backend",
                        {
                          { "target", "jolt" },
                          { "wheel_castor", 0.50F },
                        } },
                    },
                  }) },
              },
            }) },
        } },
    };
  }

  auto RegisterStubGeometryAsset(const std::filesystem::path& cooked_root,
    const std::string_view virtual_path, const std::string_view relpath) -> void
  {
    const auto key = data::AssetKey::FromVirtualPath(virtual_path);
    const auto descriptor_bytes = std::array<std::byte, 4> { std::byte { 0x47 },
      std::byte { 0x45 }, std::byte { 0x4f }, std::byte { 0x21 } };

    auto writer = LooseCookedWriter(cooked_root);
    writer.WriteAssetDescriptor(key, data::AssetType::kGeometry, virtual_path,
      relpath, std::span<const std::byte>(descriptor_bytes));
    (void)writer.Finish();
  }

} // namespace

NOLINT_TEST(PhysicsPhase3ClosureTest,
  ComplexSceneFixtureValidatesComponentDirectoryWheelAndTrailingArrays)
{
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  const auto cooked_root = MakeTempCookedRoot("complex_fixture");

  RegisterStubGeometryAsset(
    cooked_root, kGeometryVirtualPath, "Geometry/cloth.ogeo");

  const auto scene_report = SubmitAndWait(
    service, MakeSceneDescriptorRequest(cooked_root, kSceneName, 10U));
  ASSERT_TRUE(scene_report.success);
  ASSERT_TRUE(
    SubmitAndWait(service, MakePhysicsMaterialRequest(cooked_root)).success);
  ASSERT_TRUE(
    SubmitAndWait(service, MakeCompoundShapeRequest(cooked_root)).success);

  const auto bindings = BuildComplexSidecarBindings({ 0U, 2U, 4U }, { 1U, 3U });
  const auto sidecar_report = SubmitAndWait(service,
    MakePhysicsSidecarRequest(cooked_root, kSceneVirtualPath, bindings));
  ASSERT_TRUE(sidecar_report.success);
  EXPECT_TRUE(HasOutputPath(sidecar_report, "Scenes/ComplexScene.opscene"));
  EXPECT_TRUE(HasOutputPath(sidecar_report, "Physics/Resources/physics.table"));
  EXPECT_TRUE(HasOutputPath(sidecar_report, "Physics/Resources/physics.data"));

  const auto shape_bytes = ReadBinaryFile(cooked_root
    / std::filesystem::path("Physics/Shapes/chassis_compound.ocshape"));
  const auto shape_desc
    = ReadStructAt<phys::CollisionShapeAssetDesc>(shape_bytes, 0);
  ASSERT_TRUE(shape_desc.has_value());
  EXPECT_EQ(shape_desc->shape_type, phys::ShapeType::kCompound);
  EXPECT_EQ(shape_desc->shape_params.compound.child_count, 2U);

  const auto child0 = ReadStructAt<phys::CompoundShapeChildDesc>(
    shape_bytes, shape_desc->shape_params.compound.child_byte_offset);
  const auto child1 = ReadStructAt<phys::CompoundShapeChildDesc>(shape_bytes,
    static_cast<size_t>(shape_desc->shape_params.compound.child_byte_offset)
      + sizeof(phys::CompoundShapeChildDesc));
  ASSERT_TRUE(child0.has_value());
  ASSERT_TRUE(child1.has_value());
  EXPECT_EQ(
    static_cast<phys::ShapeType>(child0->shape_type), phys::ShapeType::kBox);
  EXPECT_EQ(
    static_cast<phys::ShapeType>(child1->shape_type), phys::ShapeType::kSphere);

  const auto scene_bytes = ReadBinaryFile(
    cooked_root / std::filesystem::path("Scenes/ComplexScene.oscene"));
  ASSERT_FALSE(scene_bytes.empty());
  const auto scene_hash = base::ComputeSha256(
    std::span<const std::byte>(scene_bytes.data(), scene_bytes.size()));

  const auto sidecar_bytes = ReadBinaryFile(
    cooked_root / std::filesystem::path("Scenes/ComplexScene.opscene"));
  const auto parsed_sidecar = ParsePhysicsSidecarFile(sidecar_bytes);
  ASSERT_TRUE(parsed_sidecar.has_value());
  EXPECT_EQ(parsed_sidecar->descriptor.target_scene_key,
    data::AssetKey::FromVirtualPath(kSceneVirtualPath));
  EXPECT_EQ(parsed_sidecar->descriptor.target_node_count, 10U);
  EXPECT_TRUE(std::equal(scene_hash.begin(), scene_hash.end(),
    std::begin(parsed_sidecar->descriptor.target_scene_content_hash)));
  EXPECT_EQ(parsed_sidecar->descriptor.component_table_count,
    static_cast<uint32_t>(parsed_sidecar->directory.size()));

  for (size_t i = 1; i < parsed_sidecar->directory.size(); ++i) {
    EXPECT_LT(
      static_cast<uint32_t>(parsed_sidecar->directory[i - 1].binding_type),
      static_cast<uint32_t>(parsed_sidecar->directory[i].binding_type));
  }

  const auto rigid_table
    = FindTable(*parsed_sidecar, phys::PhysicsBindingType::kRigidBody);
  const auto soft_table
    = FindTable(*parsed_sidecar, phys::PhysicsBindingType::kSoftBody);
  const auto joint_table
    = FindTable(*parsed_sidecar, phys::PhysicsBindingType::kJoint);
  const auto vehicle_table
    = FindTable(*parsed_sidecar, phys::PhysicsBindingType::kVehicle);
  const auto wheel_table
    = FindTable(*parsed_sidecar, phys::PhysicsBindingType::kVehicleWheel);
  ASSERT_TRUE(rigid_table.has_value());
  ASSERT_TRUE(soft_table.has_value());
  ASSERT_TRUE(joint_table.has_value());
  ASSERT_TRUE(vehicle_table.has_value());
  ASSERT_TRUE(wheel_table.has_value());

  const auto rigid_record = ReadStructAt<phys::RigidBodyBindingRecord>(
    sidecar_bytes, static_cast<size_t>(rigid_table->table.offset));
  ASSERT_TRUE(rigid_record.has_value());
  EXPECT_EQ(rigid_record->shape_asset_key,
    data::AssetKey::FromVirtualPath(kShapeVirtualPath));
  EXPECT_EQ(rigid_record->material_asset_key,
    data::AssetKey::FromVirtualPath(kMaterialVirtualPath));

  const auto soft_record_offset = static_cast<size_t>(soft_table->table.offset);
  const auto soft_record = ReadStructAt<phys::SoftBodyBindingRecord>(
    sidecar_bytes, soft_record_offset);
  ASSERT_TRUE(soft_record.has_value());
  EXPECT_EQ(soft_record->pinned_vertex_count, 3U);
  EXPECT_EQ(soft_record->kinematic_vertex_count, 2U);
  EXPECT_EQ(soft_record->pinned_vertex_byte_offset,
    static_cast<uint32_t>(sizeof(phys::SoftBodyBindingRecord)));
  EXPECT_EQ(soft_record->kinematic_vertex_byte_offset,
    static_cast<uint32_t>(
      sizeof(phys::SoftBodyBindingRecord) + 3U * sizeof(uint32_t)));

  const auto pinned_vertices = ReadUint32ArrayAt(sidecar_bytes,
    soft_record_offset + soft_record->pinned_vertex_byte_offset,
    soft_record->pinned_vertex_count);
  const auto kinematic_vertices = ReadUint32ArrayAt(sidecar_bytes,
    soft_record_offset + soft_record->kinematic_vertex_byte_offset,
    soft_record->kinematic_vertex_count);
  EXPECT_EQ(pinned_vertices, (std::vector<uint32_t> { 0U, 2U, 4U }));
  EXPECT_EQ(kinematic_vertices, (std::vector<uint32_t> { 1U, 3U }));
  EXPECT_NE(
    soft_record->topology_resource_index, data::pak::core::kNoResourceIndex);

  const auto joint_record = ReadStructAt<phys::JointBindingRecord>(
    sidecar_bytes, static_cast<size_t>(joint_table->table.offset));
  ASSERT_TRUE(joint_record.has_value());
  EXPECT_NE(
    joint_record->constraint_resource_index, data::pak::core::kNoResourceIndex);

  const auto vehicle_record = ReadStructAt<phys::VehicleBindingRecord>(
    sidecar_bytes, static_cast<size_t>(vehicle_table->table.offset));
  ASSERT_TRUE(vehicle_record.has_value());
  EXPECT_EQ(vehicle_record->wheel_slice_offset, 0U);
  EXPECT_EQ(vehicle_record->wheel_slice_count, 4U);
  EXPECT_NE(vehicle_record->constraint_resource_index,
    data::pak::core::kNoResourceIndex);

  const auto wheel_records = ReadStructArrayAt<phys::VehicleWheelBindingRecord>(
    sidecar_bytes, static_cast<size_t>(wheel_table->table.offset),
    wheel_table->table.count, wheel_table->table.entry_size);
  ASSERT_EQ(wheel_records.size(), 4U);
  EXPECT_EQ(wheel_records[0].wheel_node_index, 5U);
  EXPECT_EQ(wheel_records[0].axle_index, 0U);
  EXPECT_EQ(wheel_records[0].side, phys::VehicleWheelSide::kLeft);
  EXPECT_FLOAT_EQ(wheel_records[0].backend_scalars.jolt.wheel_castor, 0.50F);
  EXPECT_EQ(wheel_records[1].wheel_node_index, 6U);
  EXPECT_EQ(wheel_records[1].axle_index, 0U);
  EXPECT_EQ(wheel_records[1].side, phys::VehicleWheelSide::kRight);
  EXPECT_FLOAT_EQ(wheel_records[1].backend_scalars.jolt.wheel_castor, 0.60F);
  EXPECT_EQ(wheel_records[2].wheel_node_index, 7U);
  EXPECT_EQ(wheel_records[2].axle_index, 1U);
  EXPECT_EQ(wheel_records[2].side, phys::VehicleWheelSide::kLeft);
  EXPECT_FLOAT_EQ(wheel_records[2].backend_scalars.jolt.wheel_castor, 0.70F);
  EXPECT_EQ(wheel_records[3].wheel_node_index, 8U);
  EXPECT_EQ(wheel_records[3].axle_index, 1U);
  EXPECT_EQ(wheel_records[3].side, phys::VehicleWheelSide::kRight);
  EXPECT_FLOAT_EQ(wheel_records[3].backend_scalars.jolt.wheel_castor, 0.80F);

  const auto physics_table = ParsePhysicsResourceTable(
    cooked_root / std::filesystem::path("Physics/Resources/physics.table"));
  ASSERT_FALSE(physics_table.empty());
  const auto soft_index
    = static_cast<uint32_t>(soft_record->topology_resource_index);
  const auto joint_index
    = static_cast<uint32_t>(joint_record->constraint_resource_index);
  const auto vehicle_index
    = static_cast<uint32_t>(vehicle_record->constraint_resource_index);
  ASSERT_LT(soft_index, physics_table.size());
  ASSERT_LT(joint_index, physics_table.size());
  ASSERT_LT(vehicle_index, physics_table.size());
  EXPECT_EQ(physics_table[soft_index].format,
    phys::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary);
  EXPECT_EQ(physics_table[joint_index].format,
    phys::PhysicsResourceFormat::kJoltConstraintBinary);
  EXPECT_EQ(physics_table[vehicle_index].format,
    phys::PhysicsResourceFormat::kJoltVehicleConstraintBinary);

  service.Stop();
}

NOLINT_TEST(PhysicsPhase3ClosureTest,
  IncrementalRecookUpdatesOnlySidecarAndKeepsUnchangedAssetsStable)
{
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  const auto cooked_root = MakeTempCookedRoot("incremental_recook_stability");

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
  ASSERT_TRUE(scene_asset_before.has_value());
  ASSERT_TRUE(material_asset_before.has_value());
  ASSERT_TRUE(shape_asset_before.has_value());
  ASSERT_TRUE(geometry_asset_before.has_value());

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
  ASSERT_TRUE(scene_asset_after.has_value());
  ASSERT_TRUE(material_asset_after.has_value());
  ASSERT_TRUE(shape_asset_after.has_value());
  ASSERT_TRUE(geometry_asset_after.has_value());

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
  ASSERT_TRUE(sidecar_entry.has_value());
  const auto sidecar_entry_count
    = std::ranges::count_if(after_inspection.Assets(),
      [&](const auto& asset) { return asset.key == sidecar_key; });
  EXPECT_EQ(sidecar_entry_count, 1);

  service.Stop();
}

} // namespace oxygen::content::import::test
