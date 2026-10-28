//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data::pak::world {

//! The value of one schema field, of the field's `schema::FieldType`: a bool,
//! an int32 (an int, or the wire value of an enum member) or a float (a float,
//! or one axis of a vec3).
using FieldValue = std::variant<bool, std::int32_t, float>;

//! Writes one field of a scene component record.
/*!
 Each cooked component record has one writer per field set its components
 use. The descriptor reader and the scene changes (`ApplyChange`) write
 records through them, so an authored value and a change of the same property
 give the same record.

 A writer returns false, leaving the record unchanged, when the record holds
 no such field (a point light has no cone) or `value` is not of the field's
 type. It does not check the value's range: the record's validator does, as
 for any record.

 The node flag sources follow the tri-state rule of `NodeRecord`: Inherit
 sets the flag's `inherited_flags` bit, Shown or On its `node_flags` bit.
 An orthographic camera's size and aspect ratio rewrite its volume around its
 centre.
*/
OXGN_DATA_NDAPI auto WriteField(NodeRecord& record,
  schema::TransformField field, const FieldValue& value) -> bool;
OXGN_DATA_NDAPI auto WriteField(
  NodeRecord& record, schema::NodeField field, const FieldValue& value) -> bool;
OXGN_DATA_NDAPI auto WriteField(PerspectiveCameraRecord& record,
  schema::PerspectiveCameraField field, const FieldValue& value) -> bool;
OXGN_DATA_NDAPI auto WriteField(OrthographicCameraRecord& record,
  schema::OrthographicCameraField field, const FieldValue& value) -> bool;
OXGN_DATA_NDAPI auto WriteField(DirectionalLightRecord& record,
  schema::DirectionalLightField field, const FieldValue& value) -> bool;
OXGN_DATA_NDAPI auto WriteField(PointLightRecord& record,
  schema::LocalLightField field, const FieldValue& value) -> bool;
OXGN_DATA_NDAPI auto WriteField(SpotLightRecord& record,
  schema::LocalLightField field, const FieldValue& value) -> bool;
OXGN_DATA_NDAPI auto WriteField(LocalFogVolumeRecord& record,
  schema::LocalFogVolumeField field, const FieldValue& value) -> bool;

//! Reads one field of a scene component record.
/*!
 The inverse of `WriteField`: each cooked component record has one reader per
 field set its components use, so a value reported from a record is the value
 an authored property or a change wrote into it.

 A reader returns nullopt when the record holds no such field (a point light
 has no cone, the rotation fields reach the record only as a quaternion). A
 node flag source reads as its `VisibilitySource` or `ShadowSource` wire
 value, from the tri-state rule of `NodeRecord`.
*/
OXGN_DATA_NDAPI auto ReadField(const NodeRecord& record,
  schema::TransformField field) -> std::optional<FieldValue>;
OXGN_DATA_NDAPI auto ReadField(const NodeRecord& record,
  schema::NodeField field) -> std::optional<FieldValue>;
OXGN_DATA_NDAPI auto ReadField(const PerspectiveCameraRecord& record,
  schema::PerspectiveCameraField field) -> std::optional<FieldValue>;
OXGN_DATA_NDAPI auto ReadField(const OrthographicCameraRecord& record,
  schema::OrthographicCameraField field) -> std::optional<FieldValue>;
OXGN_DATA_NDAPI auto ReadField(const DirectionalLightRecord& record,
  schema::DirectionalLightField field) -> std::optional<FieldValue>;
OXGN_DATA_NDAPI auto ReadField(const PointLightRecord& record,
  schema::LocalLightField field) -> std::optional<FieldValue>;
OXGN_DATA_NDAPI auto ReadField(const SpotLightRecord& record,
  schema::LocalLightField field) -> std::optional<FieldValue>;
OXGN_DATA_NDAPI auto ReadField(const LocalFogVolumeRecord& record,
  schema::LocalFogVolumeField field) -> std::optional<FieldValue>;

//! A rotation quaternion, (x, y, z, w).
using Quaternion = std::array<float, 4>;

//! Writes the node's local rotation, which the record holds as a quaternion:
//! the rotation fields of the Transform field set reach it only whole.
OXGN_DATA_API auto WriteRotation(
  NodeRecord& record, const Quaternion& rotation) noexcept -> void;

//! The rotation of X-Y-Z Euler angles `{x, y, z}`, in degrees: about X, then
//! about Y, then about Z, each about a parent axis, so R = Rz * Ry * Rx (the
//! schema's `euler-xyz-degrees-to-quat` encoding, `space` in
//! Oxygen/Core/Constants.h).
OXGN_DATA_NDAPI auto EulerXyzDegreesToQuaternion(
  const std::array<float, 3>& degrees) noexcept -> Quaternion;

//! The X-Y-Z Euler angles of `rotation`, in degrees, ordered as
//! `EulerXyzDegreesToQuaternion` takes them. The Y angle lies in [-90°, 90°].
//! At ±90° about Y, where X and Z turn about one axis, the X angle is zero.
OXGN_DATA_NDAPI auto QuaternionToEulerXyzDegrees(
  const Quaternion& rotation) noexcept -> std::array<float, 3>;

} // namespace oxygen::data::pak::world
