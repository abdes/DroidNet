//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: the rules the physics sidecar reader enforces on a descriptor: its
// header, its binding directory, the nodes its bindings name and the order of
// its tables, the wheel slices of its vehicles and the vertex lists of its
// soft bodies. The reader and the loader refuse alike.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace physics = oxygen::data::pak::physics;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::PhysicsSidecarBuilder;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneNode;
using oxygen::cooker::testing::SidecarRefusal;
using oxygen::data::AssetKey;
using oxygen::data::CookedDefect;
using physics::PhysicsBindingType;
using physics::VehicleWheelSide;

auto Key(const std::string_view path) -> AssetKey
{
  return AssetKey::FromVirtualPath(path);
}

//! A scene of `kNodes` root nodes, and the bindings of its sidecar.
class PhysicsSidecarStructureTest : public ::testing::Test {
protected:
  static constexpr uint32_t kNodes = 4U;

  PhysicsSidecarStructureTest()
  {
    for (auto index = uint32_t { 0U }; index < kNodes; ++index) {
      nodes_.push_back(scene_.Node("Node" + std::to_string(index)));
    }
  }

  [[nodiscard]] auto Node(const uint32_t index) const -> SceneNode
  {
    return nodes_.at(index);
  }

  [[nodiscard]] auto Bindings() -> PhysicsSidecarBuilder& { return bindings_; }

  //! The sidecar of the bindings, as a cook writes it.
  [[nodiscard]] auto Bytes() const -> std::vector<std::byte>
  {
    return bindings_.Bytes(Key("/Game/Main.oscene"), scene_.Bytes(), "Main");
  }

  //! The sidecar of the bindings, as the reader admits it.
  [[nodiscard]] auto Asset() const -> oxygen::data::PhysicsSceneAsset
  {
    return { Key("/Game/Main.opscene"), Bytes() };
  }

private:
  SceneBuilder scene_ { "Main" };
  std::vector<SceneNode> nodes_;
  PhysicsSidecarBuilder bindings_;
};

//! A vehicle on `chassis` with the wheels `left` and `right` on its first
//! axle.
auto AddVehicle(PhysicsSidecarBuilder& bindings, const SceneNode chassis,
  const SceneNode left, const SceneNode right) -> void
{
  bindings.Vehicle(chassis, Key("/Game/Car.opres"))
    .Wheel(chassis, { .node = right, .side = VehicleWheelSide::kRight })
    .Wheel(chassis, { .node = left, .side = VehicleWheelSide::kLeft });
}

//! The reader admits a sidecar of every binding type, with the vertex lists
//! of its soft bodies and the wheels of its vehicles.
NOLINT_TEST_F(PhysicsSidecarStructureTest, ReadsEveryBindingOfASidecar)
{
  // Arrange
  Bindings()
    .RigidBody(Node(1U), Key("/Game/Box.ocshape"), Key("/Game/Wood.opmat"))
    .Collider(Node(0U), Key("/Game/Box.ocshape"), Key("/Game/Wood.opmat"))
    .Character(Node(2U), Key("/Game/Capsule.ocshape"))
    .SoftBody(Node(1U), Key("/Game/Cloth.opres"), {},
      { .pinned = { 0U, 1U }, .kinematic = { 2U } })
    .SoftBody(Node(0U), Key("/Game/Flag.opres"), {}, { .pinned = { 3U } })
    .Joint(Node(1U), Node(2U), Key("/Game/Hinge.opres"))
    .Joint(Node(0U), std::nullopt, Key("/Game/Anchor.opres"))
    .Aggregate(Node(3U));
  AddVehicle(Bindings(), Node(0U), Node(1U), Node(2U));

  // Act
  const auto refusal = SidecarRefusal(Bytes());
  const auto asset = Asset();

  // Assert
  EXPECT_EQ(refusal, std::nullopt);
  const auto soft_bodies = asset.GetBindings<physics::SoftBodyBindingRecord>();
  ASSERT_EQ(soft_bodies.size(), 2U);
  EXPECT_EQ(soft_bodies[1].topology_asset_key, Key("/Game/Cloth.opres"));
  EXPECT_EQ(asset.GetSoftBodyVertices(0U).pinned, std::vector { 3U });
  EXPECT_EQ(asset.GetSoftBodyVertices(1U).pinned, (std::vector { 0U, 1U }));
  EXPECT_EQ(asset.GetSoftBodyVertices(1U).kinematic, std::vector { 2U });
  const auto* body
    = asset.FindBinding<physics::RigidBodyBindingRecord>(Node(1U).Index());
  ASSERT_NE(body, nullptr);
  EXPECT_EQ(body->shape_asset_key, Key("/Game/Box.ocshape"));
  const auto wheels = asset.GetBindings<physics::VehicleWheelBindingRecord>();
  ASSERT_EQ(wheels.size(), 2U);
  EXPECT_EQ(wheels[0].side, VehicleWheelSide::kLeft);
  EXPECT_EQ(wheels[1].wheel_node_index, Node(2U).Index());
}

//! A descriptor that is not a current physics sidecar of a scene, cooked for
//! a backend, is refused.
NOLINT_TEST_F(PhysicsSidecarStructureTest, RefusesAHeaderItCannotRead)
{
  const auto valid = Bytes();
  auto small = valid;
  small.resize(sizeof(physics::PhysicsSceneAssetDesc) - 1U);
  auto typed = valid;
  DescriptorDefects::SetAssetType(typed, oxygen::data::AssetType::kScene);
  auto versioned = valid;
  DescriptorDefects::SetVersion(versioned, 0xFFU);
  auto unbacked = valid;
  DescriptorDefects::SetSidecarBackend(
    unbacked, physics::PhysicsBackend::kNone);
  auto orphan = valid;
  DescriptorDefects::SetSidecarScene(orphan, AssetKey {});

  EXPECT_EQ(SidecarRefusal(valid), std::nullopt);
  EXPECT_EQ(SidecarRefusal(small), CookedDefect::kTooSmall);
  EXPECT_EQ(SidecarRefusal(typed), CookedDefect::kWrongMagic);
  EXPECT_EQ(SidecarRefusal(versioned), CookedDefect::kUnsupportedVersion);
  EXPECT_EQ(SidecarRefusal(unbacked), CookedDefect::kInvalidValue);
  EXPECT_EQ(SidecarRefusal(orphan), CookedDefect::kMissingPart);
}

//! The directory names each binding type the format defines at most once,
//! and each table holds that type's records.
NOLINT_TEST_F(PhysicsSidecarStructureTest, RefusesADirectoryItCannotRead)
{
  // Arrange
  Bindings()
    .RigidBody(Node(0U), Key("/Game/Box.ocshape"), Key("/Game/Wood.opmat"))
    .Collider(Node(1U), Key("/Game/Box.ocshape"), Key("/Game/Wood.opmat"));
  const auto valid = Bytes();
  auto unknown = valid;
  DescriptorDefects::SetBindingTableType(
    unknown, PhysicsBindingType::kCollider, 0xFFFFU);
  auto repeated = valid;
  DescriptorDefects::SetBindingTableType(repeated,
    PhysicsBindingType::kCollider,
    static_cast<uint32_t>(PhysicsBindingType::kRigidBody));
  auto resized = valid;
  DescriptorDefects::SetBindingEntrySize(resized, PhysicsBindingType::kCollider,
    sizeof(physics::ColliderBindingRecord) + 1U);

  // Act and assert
  EXPECT_EQ(SidecarRefusal(unknown), CookedDefect::kInvalidValue);
  EXPECT_EQ(SidecarRefusal(repeated), CookedDefect::kDuplicate);
  EXPECT_EQ(SidecarRefusal(resized), CookedDefect::kWrongRowSize);
}

//! A binding names nodes of its scene, the world aside for a joint's second
//! node.
NOLINT_TEST_F(PhysicsSidecarStructureTest, RefusesANodeItsSceneDoesNotHold)
{
  // Arrange
  Bindings()
    .RigidBody(Node(0U), Key("/Game/Box.ocshape"), Key("/Game/Wood.opmat"))
    .Joint(Node(0U), Node(1U), Key("/Game/Hinge.opres"));
  AddVehicle(Bindings(), Node(1U), Node(2U), Node(3U));
  const auto valid = Bytes();
  auto body_bytes = valid;
  auto body
    = DescriptorDefects::BindingAt<physics::RigidBodyBindingRecord>(valid, 0U);
  body.node_index = kNodes;
  DescriptorDefects::SetBinding(body_bytes, 0U, body);
  auto joint_bytes = valid;
  auto joint
    = DescriptorDefects::BindingAt<physics::JointBindingRecord>(valid, 0U);
  joint.node_index_b = kNodes;
  DescriptorDefects::SetBinding(joint_bytes, 0U, joint);
  auto wheel_bytes = valid;
  auto wheel = DescriptorDefects::BindingAt<physics::VehicleWheelBindingRecord>(
    valid, 0U);
  wheel.wheel_node_index = kNodes;
  DescriptorDefects::SetBinding(wheel_bytes, 0U, wheel);

  // Act and assert
  EXPECT_EQ(SidecarRefusal(valid), std::nullopt);
  EXPECT_EQ(SidecarRefusal(body_bytes), CookedDefect::kInvalidReference);
  EXPECT_EQ(SidecarRefusal(joint_bytes), CookedDefect::kInvalidReference);
  EXPECT_EQ(SidecarRefusal(wheel_bytes), CookedDefect::kInvalidReference);
}

//! A node table holds one binding per node in node order, and the joint table
//! is in the order of the nodes it binds, which lookups rely on.
NOLINT_TEST_F(PhysicsSidecarStructureTest, RefusesTablesOutOfNodeOrder)
{
  // Arrange
  Bindings()
    .RigidBody(Node(1U), Key("/Game/Box.ocshape"), Key("/Game/Wood.opmat"))
    .RigidBody(Node(2U), Key("/Game/Box.ocshape"), Key("/Game/Wood.opmat"))
    .Joint(Node(0U), Node(1U), Key("/Game/Hinge.opres"))
    .Joint(Node(1U), Node(2U), Key("/Game/Hinge.opres"));
  const auto valid = Bytes();
  const auto with_body = [&valid](const uint32_t index,
                           const uint32_t node) -> std::vector<std::byte> {
    auto bytes = valid;
    auto body = DescriptorDefects::BindingAt<physics::RigidBodyBindingRecord>(
      valid, index);
    body.node_index = node;
    DescriptorDefects::SetBinding(bytes, index, body);
    return bytes;
  };
  auto joint_bytes = valid;
  auto joint
    = DescriptorDefects::BindingAt<physics::JointBindingRecord>(valid, 0U);
  joint.node_index_a = 2U;
  DescriptorDefects::SetBinding(joint_bytes, 0U, joint);

  // Act and assert
  EXPECT_EQ(SidecarRefusal(with_body(0U, 3U)), CookedDefect::kNotCanonical);
  EXPECT_EQ(SidecarRefusal(with_body(1U, 1U)), CookedDefect::kDuplicate);
  EXPECT_EQ(SidecarRefusal(joint_bytes), CookedDefect::kNotCanonical);
}

//! The vehicles' slices partition the wheel table in vehicle order; a slice
//! holds its vehicle's wheels, one per role, in role order.
NOLINT_TEST_F(PhysicsSidecarStructureTest, RefusesWheelsOutsideTheirSlices)
{
  // Arrange
  AddVehicle(Bindings(), Node(0U), Node(1U), Node(2U));
  const auto valid = Bytes();
  const auto with_slice = [&valid](const uint32_t offset,
                            const uint32_t count) -> std::vector<std::byte> {
    auto bytes = valid;
    auto vehicle
      = DescriptorDefects::BindingAt<physics::VehicleBindingRecord>(valid, 0U);
    vehicle.wheel_slice_offset = offset;
    vehicle.wheel_slice_count = count;
    DescriptorDefects::SetBinding(bytes, 0U, vehicle);
    return bytes;
  };
  const auto with_wheel = [&valid](const uint32_t index,
                            const auto& edit) -> std::vector<std::byte> {
    auto bytes = valid;
    auto wheel
      = DescriptorDefects::BindingAt<physics::VehicleWheelBindingRecord>(
        valid, index);
    edit(wheel);
    DescriptorDefects::SetBinding(bytes, index, wheel);
    return bytes;
  };
  const auto other_vehicle = with_wheel(
    1U, [](auto& wheel) -> void { wheel.vehicle_node_index = 3U; });
  const auto same_role = with_wheel(
    1U, [](auto& wheel) -> void { wheel.side = VehicleWheelSide::kLeft; });
  const auto out_of_order
    = with_wheel(0U, [](auto& wheel) -> void { wheel.axle_index = 1U; });

  // Act and assert
  EXPECT_EQ(SidecarRefusal(with_slice(1U, 1U)), CookedDefect::kNotCanonical);
  EXPECT_EQ(SidecarRefusal(with_slice(0U, 3U)), CookedDefect::kOutOfBounds);
  EXPECT_EQ(SidecarRefusal(with_slice(0U, 1U)), CookedDefect::kInconsistent);
  EXPECT_EQ(SidecarRefusal(other_vehicle), CookedDefect::kInvalidReference);
  EXPECT_EQ(SidecarRefusal(same_role), CookedDefect::kDuplicate);
  EXPECT_EQ(SidecarRefusal(out_of_order), CookedDefect::kNotCanonical);
}

//! A soft body's vertex lists follow its record, pinned first, and lie in
//! the descriptor.
NOLINT_TEST_F(PhysicsSidecarStructureTest, RefusesVertexListsOffTheirRecord)
{
  // Arrange
  Bindings().SoftBody(Node(0U), Key("/Game/Cloth.opres"), {},
    { .pinned = { 0U, 1U }, .kinematic = { 2U } });
  const auto valid = Bytes();
  const auto with_pinned = [&valid](const uint32_t count,
                             const uint32_t at) -> std::vector<std::byte> {
    auto bytes = valid;
    auto body
      = DescriptorDefects::BindingAt<physics::SoftBodyBindingRecord>(valid, 0U);
    body.pinned_vertex_count = count;
    body.pinned_vertex_byte_offset = at;
    DescriptorDefects::SetBinding(bytes, 0U, body);
    return bytes;
  };
  constexpr auto kAfterRecord
    = static_cast<uint32_t>(sizeof(physics::SoftBodyBindingRecord));

  // Act and assert
  EXPECT_EQ(SidecarRefusal(with_pinned(2U, kAfterRecord + 4U)),
    CookedDefect::kNotCanonical);
  EXPECT_EQ(
    SidecarRefusal(with_pinned(0U, kAfterRecord)), CookedDefect::kNotCanonical);
  EXPECT_EQ(SidecarRefusal(with_pinned(0x1000'0000U, kAfterRecord)),
    CookedDefect::kOutOfBounds);
}

//! The reserved bytes of the arm of the sidecar's backend are zero; the same
//! bytes are scalars of the other backend.
NOLINT_TEST_F(PhysicsSidecarStructureTest, RefusesReservesOfItsBackend)
{
  // Arrange
  auto record = physics::RigidBodyBindingRecord {};
  record.backend_scalars.jolt._reserved[0] = 1U;
  Bindings()
    .RigidBody(
      Node(0U), Key("/Game/Box.ocshape"), Key("/Game/Wood.opmat"), record)
    .Backend(physics::PhysicsBackend::kPhysX);
  const auto physx = Bytes();
  auto jolt = physx;
  DescriptorDefects::SetSidecarBackend(jolt, physics::PhysicsBackend::kJolt);

  // Act and assert
  EXPECT_EQ(SidecarRefusal(jolt), CookedDefect::kReservedNotZero);
  EXPECT_EQ(SidecarRefusal(physx), std::nullopt);
}

} // namespace
