//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::content::import {

//! A rigid body binding: the record and the references its cook resolves.
struct RigidBodyBindingSource final {
  data::pak::physics::RigidBodyBindingRecord record {};
  std::string shape_ref;
  std::string material_ref;
  //! The `backend.target` authored, `kNone` without a backend block; the
  //! record's backend scalars hold that target's arm.
  core::meta::physics::PhysicsBackend authored_backend {
    core::meta::physics::PhysicsBackend::kNone,
  };
};

//! A collider binding: the record and the references its cook resolves.
struct ColliderBindingSource final {
  data::pak::physics::ColliderBindingRecord record {};
  std::string shape_ref;
  std::string material_ref;
};

//! A character binding: the record and the references its cook resolves.
struct CharacterBindingSource final {
  data::pak::physics::CharacterBindingRecord record {};
  std::string shape_ref;
  std::optional<std::string> inner_shape_ref;
  //! The `backend.target` authored, `kNone` without a backend block.
  core::meta::physics::PhysicsBackend authored_backend {
    core::meta::physics::PhysicsBackend::kNone,
  };
};

//! A soft body binding: the record, its references and the authored vertices.
struct SoftBodyBindingSource final {
  data::pak::physics::SoftBodyBindingRecord record {};
  std::string source_mesh_ref;
  data::AssetKey source_mesh_asset_key;
  data::AssetReferences source_mesh_references;
  std::optional<std::string> collision_mesh_ref;
  std::vector<uint32_t> pinned_vertices;
  std::vector<uint32_t> kinematic_vertices;
  //! The `backend.target` authored, `kNone` without a backend block.
  core::meta::physics::PhysicsBackend authored_backend {
    core::meta::physics::PhysicsBackend::kNone,
  };
};

//! A wheel of a vehicle binding.
struct VehicleWheelSource final {
  uint32_t node_index { 0 };
  uint16_t axle_index { 0 };
  data::pak::physics::VehicleWheelSide side {
    data::pak::physics::VehicleWheelSide::kLeft,
  };
  data::pak::physics::VehicleWheelBackendScalars backend_scalars {};
  core::meta::physics::PhysicsBackend backend_target {
    core::meta::physics::PhysicsBackend::kNone,
  };
};

//! The constraint type of a joint binding.
enum class JointConstraintType : uint8_t {
  kFixed,
  kPoint,
  kHinge,
  kSlider,
  kDistance,
  kCone,
  kSixDof,
};

//! The space a joint's frames are authored in.
enum class JointConstraintSpace : uint8_t {
  //! Frames in world space.
  kWorld,
  //! Each frame in its body's centre-of-mass space.
  kLocal,
};

//! One frame of a joint: +X is the primary axis (hinge, slide, twist), +Y the
//! normal axis.
struct JointFrameSource final {
  std::array<float, 3> position { 0.0F, 0.0F, 0.0F };
  //! Unit quaternion [x, y, z, w].
  std::array<float, 4> rotation { 0.0F, 0.0F, 0.0F, 1.0F };
};

//! A value per joint-frame axis: [tx, ty, tz, rx, ry, rz].
using JointAxisValues = std::array<float, 6>;

//! The motion of one six-DOF axis.
enum class JointAxisMode : uint8_t {
  kFree,
  kLimited,
  kLocked,
};

//! A motion per joint-frame axis: [tx, ty, tz, rx, ry, rz].
using JointAxisModes = std::array<JointAxisMode, 6>;

//! The joint settings preparation proved valid for the constraint type: every
//! authored value has a backend setting, and every value the type does not
//! use is absent or zero.
struct JointSettingsSource final {
  JointConstraintType type { JointConstraintType::kFixed };
  JointConstraintSpace space { JointConstraintSpace::kWorld };
  JointFrameSource frame_a;
  JointFrameSource frame_b;
  //! Six-DOF only; absent means every axis limited when limits are authored,
  //! free otherwise.
  std::optional<JointAxisModes> axis_modes;
  //! Limits, both or neither.
  std::optional<JointAxisValues> limits_lower;
  std::optional<JointAxisValues> limits_upper;
  //! Limit springs, both or neither.
  std::optional<JointAxisValues> spring_stiffnesses;
  std::optional<JointAxisValues> spring_dampings;
  std::optional<JointAxisValues> motor_max_forces;
  std::optional<JointAxisValues> motor_max_torques;
  //! Position-motor drive, both or neither.
  std::optional<JointAxisValues> motor_drive_frequencies;
  std::optional<JointAxisValues> motor_damping_ratios;
  uint32_t priority { 0 };
};

//! A joint binding: the record, the authored backend choice and its settings.
struct JointBindingSource final {
  data::pak::physics::JointBindingRecord record {};
  data::pak::physics::PhysicsResourceFormat constraint_format
    = data::pak::physics::PhysicsResourceFormat::kJoltConstraintBinary;
  //! The `backend.target` authored, `kNone` without a backend block.
  core::meta::physics::PhysicsBackend authored_backend {
    core::meta::physics::PhysicsBackend::kNone,
  };
  JointSettingsSource settings;
};

//! A vehicle binding: the record, its wheels and the authored backend choice.
struct VehicleBindingSource final {
  data::pak::physics::VehicleBindingRecord record {};
  std::vector<VehicleWheelSource> wheels;
  data::pak::physics::PhysicsResourceFormat constraint_format
    = data::pak::physics::PhysicsResourceFormat::kJoltVehicleConstraintBinary;
  bool backend_explicit { false };
  nlohmann::json authored_binding;
};

//! The bindings of a physics sidecar, as authored.
struct PhysicsSidecarDocument final {
  std::vector<RigidBodyBindingSource> rigid_bodies;
  std::vector<ColliderBindingSource> colliders;
  std::vector<CharacterBindingSource> characters;
  std::vector<SoftBodyBindingSource> soft_bodies;
  std::vector<JointBindingSource> joints;
  std::vector<VehicleBindingSource> vehicles;
  std::vector<data::pak::physics::VehicleWheelBindingRecord> vehicle_wheels;
  std::vector<data::pak::physics::AggregateBindingRecord> aggregates;
};

//! A reference a physics sidecar's bindings make, as authored.
struct PhysicsSidecarReference final {
  std::string written;
  //! Names a geometry; otherwise a shape or a physics material.
  bool geometry = false;
  std::string json_pointer;
};

//! The references the bindings of a sidecar document make, in document order.
OXGN_COOK_NDAPI auto PhysicsSidecarReferences(const nlohmann::json& document)
  -> std::vector<PhysicsSidecarReference>;

//! Validates and interprets sidecar bytes: every rule the document states
//! alone, with no scene, backend or cooked-resource lookup.
/*!
 The node bounds, the references, and the agreement with the requested backend
 need the target scene and stay with the cook.

 Adds the errors it finds to `diagnostics`, each located by the RFC 6901
 pointer of the value at fault.

 @return the bindings, or nothing when an error was added.
*/
OXGN_COOK_NDAPI auto PreparePhysicsSidecarDocument(std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<PhysicsSidecarDocument>;

} // namespace oxygen::content::import
