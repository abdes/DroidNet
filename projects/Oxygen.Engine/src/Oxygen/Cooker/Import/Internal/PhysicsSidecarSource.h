//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

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
  bool backend_explicit { false };
  nlohmann::json authored_binding;
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

//! A joint binding: the record and the authored backend choice.
struct JointBindingSource final {
  data::pak::physics::JointBindingRecord record {};
  data::pak::physics::PhysicsResourceFormat constraint_format
    = data::pak::physics::PhysicsResourceFormat::kJoltConstraintBinary;
  bool backend_explicit { false };
  nlohmann::json authored_binding;
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
