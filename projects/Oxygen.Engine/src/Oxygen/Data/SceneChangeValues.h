//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <numbers>
#include <optional>
#include <variant>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneRecordFields.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>

namespace oxygen::data::schema {

//! A property value in the schema's units: a bool, an int32 (an int, or the
//! wire value of an enum member), a float, the three floats of a vec3, or the
//! key of an asset.
using SchemaValue
  = std::variant<bool, std::int32_t, float, std::array<float, 3>, AssetKey>;

//! The pieces `EncodeChange`, `ApplyChange` and `ReadProperty`
//! (Generated.SceneChanges.h) are made of.
/*!
 An encoder returns the change value of one property, or nothing when `value`
 is of another type or a field it reaches would not hold it
 (`IsValidFieldValue`). Bytes of the change value its member does not use are
 zero. A reader returns the value of one property of a record in the schema's
 units, or nothing when the record holds no such field.
*/
namespace detail {

  [[nodiscard]] inline auto EncodeBool(const SchemaValue& value)
    -> std::optional<pak::world::OverrideValue>
  {
    const auto* flag = std::get_if<bool>(&value);
    if (flag == nullptr) {
      return std::nullopt;
    }
    auto encoded = pak::world::OverrideValue {};
    encoded.as_bool = *flag ? std::uint8_t { 1U } : std::uint8_t { 0U };
    return encoded;
  }

  //! An int, or the wire value of an enum member, sent to `field`.
  [[nodiscard]] inline auto EncodeInt32(const SchemaValue& value,
    const ComponentId component, const std::uint16_t field)
    -> std::optional<pak::world::OverrideValue>
  {
    const auto* number = std::get_if<std::int32_t>(&value);
    if (number == nullptr || !IsValidFieldValue(component, field, *number)) {
      return std::nullopt;
    }
    auto encoded = pak::world::OverrideValue {};
    encoded.as_int32 = *number;
    return encoded;
  }

  //! A float sent to `field`, in degrees when `degrees_to_radians` is set.
  [[nodiscard]] inline auto EncodeFloat(const SchemaValue& value,
    const ComponentId component, const std::uint16_t field,
    const bool degrees_to_radians = false)
    -> std::optional<pak::world::OverrideValue>
  {
    const auto* number = std::get_if<float>(&value);
    if (number == nullptr) {
      return std::nullopt;
    }
    const auto field_value = degrees_to_radians
      ? static_cast<float>(
          static_cast<double>(*number) * std::numbers::pi / 180.0)
      : *number;
    if (!IsValidFieldValue(component, field, field_value)) {
      return std::nullopt;
    }
    auto encoded = pak::world::OverrideValue {};
    encoded.as_float = field_value;
    return encoded;
  }

  //! Three floats, one sent to each of `fields`.
  [[nodiscard]] inline auto EncodeVec3(const SchemaValue& value,
    const ComponentId component, const std::array<std::uint16_t, 3>& fields)
    -> std::optional<pak::world::OverrideValue>
  {
    const auto* vector = std::get_if<std::array<float, 3>>(&value);
    if (vector == nullptr) {
      return std::nullopt;
    }
    auto encoded = pak::world::OverrideValue {};
    for (auto i = 0U; i < fields.size(); ++i) {
      if (!IsValidFieldValue(component, fields.at(i), vector->at(i))) {
        return std::nullopt;
      }
    }
    encoded.as_vec3[0] = vector->at(0);
    encoded.as_vec3[1] = vector->at(1);
    encoded.as_vec3[2] = vector->at(2);
    return encoded;
  }

  //! Three X-Y-Z Euler angles in degrees, one valid for each of `fields`, as
  //! the quaternion they describe.
  [[nodiscard]] inline auto EncodeRotation(const SchemaValue& value,
    const ComponentId component, const std::array<std::uint16_t, 3>& fields)
    -> std::optional<pak::world::OverrideValue>
  {
    const auto* angles = std::get_if<std::array<float, 3>>(&value);
    if (angles == nullptr || !EncodeVec3(value, component, fields)) {
      return std::nullopt;
    }
    const auto rotation = pak::world::EulerXyzDegreesToQuaternion(*angles);
    auto encoded = pak::world::OverrideValue {};
    encoded.as_quat[0] = rotation[0];
    encoded.as_quat[1] = rotation[1];
    encoded.as_quat[2] = rotation[2];
    encoded.as_quat[3] = rotation[3];
    return encoded;
  }

  [[nodiscard]] inline auto EncodeAsset(const SchemaValue& value)
    -> std::optional<pak::world::OverrideValue>
  {
    const auto* key = std::get_if<AssetKey>(&value);
    if (key == nullptr) {
      return std::nullopt;
    }
    auto encoded = pak::world::OverrideValue {};
    encoded.as_asset = *key;
    return encoded;
  }

  //! Writes the three floats of `vector` to `fields` of `record`.
  template <typename Record, typename Field>
  [[nodiscard]] auto WriteVec3(Record& record,
    const std::array<Field, 3>& fields, const std::array<float, 3>& vector)
    -> bool
  {
    return pak::world::WriteField(record, fields[0], vector[0])
      && pak::world::WriteField(record, fields[1], vector[1])
      && pak::world::WriteField(record, fields[2], vector[2]);
  }

  //! The value of `field` of `record`, in radians read as degrees when
  //! `radians_to_degrees` is set.
  template <typename Record, typename Field>
  [[nodiscard]] auto ReadValue(const Record& record, const Field field,
    const bool radians_to_degrees = false) -> std::optional<SchemaValue>
  {
    const auto value = pak::world::ReadField(record, field);
    if (!value.has_value()) {
      return std::nullopt;
    }
    if (const auto* number = std::get_if<float>(&*value);
      number != nullptr && radians_to_degrees) {
      return SchemaValue {
        static_cast<float>(
          static_cast<double>(*number) * 180.0 / std::numbers::pi),
      };
    }
    return std::visit(
      [](const auto field_value) -> SchemaValue { return field_value; },
      *value);
  }

  //! The three floats of `fields` of `record`.
  template <typename Record, typename Field>
  [[nodiscard]] auto ReadVec3(const Record& record,
    const std::array<Field, 3>& fields) -> std::optional<SchemaValue>
  {
    auto vector = std::array<float, 3> {};
    for (auto i = 0U; i < fields.size(); ++i) {
      const auto value = pak::world::ReadField(record, fields.at(i));
      const auto* number
        = value.has_value() ? std::get_if<float>(&*value) : nullptr;
      if (number == nullptr) {
        return std::nullopt;
      }
      vector.at(i) = *number;
    }
    return vector;
  }

  //! The node's local rotation as X-Y-Z Euler angles in degrees.
  [[nodiscard]] inline auto ReadRotation(const pak::world::NodeRecord& record)
    -> SchemaValue
  {
    return pak::world::QuaternionToEulerXyzDegrees({
      record.rotation[0],
      record.rotation[1],
      record.rotation[2],
      record.rotation[3],
    });
  }

} // namespace detail

} // namespace oxygen::data::schema
