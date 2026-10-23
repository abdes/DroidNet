//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include <Oxygen/Data/Schema/Generated.SceneComponents.h>

namespace oxygen::interop::module {

  //! Component and field ids of the engine's scene components schema.
  /*!
   The ids are generated from `Oxygen/Data/Schema/SceneComponents.yaml`; a
   new component or field is a schema entry, never an edit here. `ComponentId`
   is the coarse dimension of the property-pipeline wire vocabulary; each
   `IComponentPropertyApplier` dispatches on its component's field enum.

   Property pipeline routing:
     managed PropertyId ─► PropertyEntry { component, field, FieldValue }
                          │
                          └─► PropertyApplierRegistry.Find(component)
                              └─► applier.Apply(node, entries…)
  */
  using data::schema::ComponentId;
  using data::schema::DirectionalLightField;
  using data::schema::LocalFogVolumeField;
  using data::schema::LocalLightField;
  using data::schema::NodeField;
  using data::schema::OrthographicCameraField;
  using data::schema::PerspectiveCameraField;
  using data::schema::TransformField;

  using data::schema::FieldType;
  using data::schema::FindFieldType;

  //! The value of one field: a bool, an int32 or a float, the alternatives in
  //! `FieldType` order. The schema gives each field one value type.
  using FieldValue = std::variant<bool, std::int32_t, float>;

  static_assert(std::is_same_v<std::variant_alternative_t<
                                 static_cast<std::size_t>(FieldType::kBool),
                                 FieldValue>,
    bool>);
  static_assert(std::is_same_v<std::variant_alternative_t<
                                 static_cast<std::size_t>(FieldType::kInt32),
                                 FieldValue>,
    std::int32_t>);
  static_assert(std::is_same_v<std::variant_alternative_t<
                                 static_cast<std::size_t>(FieldType::kFloat),
                                 FieldValue>,
    float>);

  //! The value type `value` holds.
  [[nodiscard]] constexpr auto TypeOf(const FieldValue& value) noexcept
    -> FieldType
  {
    return static_cast<FieldType>(value.index());
  }

  //! One field value on the property-pipeline wire.
  /*!
   Property pipeline §5.3 transports a flat sequence of these entries
   per target node. SetPropertiesCommand groups them by `component`
   and hands each contiguous run to the matching applier; the applier
   alone knows how to translate `field` into a component setter.

   The value holds the field's schema value type (`FindFieldType`);
   SetPropertiesCommand refuses an entry that does not. Vectors and
   colors go as one float field per axis.
  */
  struct PropertyEntry {
    ComponentId component;
    std::uint16_t field;
    FieldValue value;

    //! The value of a bool field.
    [[nodiscard]] auto Bool() const -> bool { return std::get<bool>(value); }

    //! The value of an int32 field: an int, or an enum member's wire value.
    [[nodiscard]] auto Int32() const -> std::int32_t
    {
      return std::get<std::int32_t>(value);
    }

    //! The value of a float field.
    [[nodiscard]] auto Float() const -> float { return std::get<float>(value); }
  };

  //! Whether `entry` addresses a field of the schema and holds its value
  //! type.
  [[nodiscard]] constexpr auto IsWellTyped(const PropertyEntry& entry) noexcept
    -> bool
  {
    const auto type = FindFieldType(entry.component, entry.field);
    return type.has_value() && *type == TypeOf(entry.value);
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
