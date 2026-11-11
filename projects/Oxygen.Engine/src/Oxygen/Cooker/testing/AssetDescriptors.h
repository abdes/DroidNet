//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::cooker::testing {

//! The physics material `name` with the values of `record`, as the physics
//! material cook writes it; it references nothing.
/*!
 @throw std::logic_error When the product loader refuses it.
*/
[[nodiscard]] auto PhysicsMaterialDescriptor(std::string_view name,
  data::pak::physics::PhysicsMaterialAssetDesc record = {})
  -> DescriptorFixture;

//! The collision shape `name` with the values of `record`, as the collision
//! shape cook writes it: it references its material and its cooked payload.
/*!
 A compound shape is not built: its children follow the record.

 @throw std::invalid_argument When `record` is a compound shape.
 @throw std::logic_error When the product loader refuses it, such as a
   shape without a material, or a payload that is not its kind's.
*/
[[nodiscard]] auto CollisionShapeDescriptor(std::string_view name,
  data::pak::physics::CollisionShapeAssetDesc record) -> DescriptorFixture;

//! The compound collision shape `name` with the values of `record` and the
//! child records `children`, as the collision shape cook writes it: the
//! children follow the record, and it references its material and the cooked
//! payload of each child that has one.
/*!
 @throw std::logic_error When the product loader refuses it, such as a
   compound without children or a child whose payload is not its kind's.
*/
[[nodiscard]] auto CompoundShapeDescriptor(std::string_view name,
  data::pak::physics::CollisionShapeAssetDesc record,
  std::span<const data::pak::physics::CompoundShapeChildDesc> children)
  -> DescriptorFixture;

//! The input action `name` with the values of `record`, as the input cook
//! writes it; it references nothing.
/*!
 @throw std::logic_error When the product loader refuses it.
*/
[[nodiscard]] auto InputActionDescriptor(std::string_view name,
  data::pak::input::InputActionAssetDesc record = {}) -> DescriptorFixture;

//! A cooked input mapping context, as the input cook writes it: its mappings,
//! each with its triggers (a combo with its steps), and the actions they name
//! as its references.
/*!
 Each record a test passes keeps its values; the builder sets the action,
 the slot name and the trigger range.

 ```cpp
 auto context = InputContextBuilder { "Gameplay" };
 context.Mapping(jump, "Space", { data::pak::input::InputTriggerRecord {} });
 game.InputContext("Input/Gameplay.oimap", context);
 ```
*/
class InputContextBuilder final {
public:
  explicit InputContextBuilder(std::string_view name = "Context",
    data::pak::input::InputMappingContextAssetDesc record = {});

  //! Maps the action `action` to the input slot `slot`, such as `Space`.
  auto Mapping(const data::AssetKey& action, std::string_view slot,
    std::vector<data::pak::input::InputTriggerRecord> triggers = {},
    data::pak::input::InputActionMappingRecord record = {})
    -> InputContextBuilder&;

  //! Maps the action `action` to the input slot `slot` through one combo
  //! trigger, which completes after the actions of `steps`.
  /*!
   The builder sets the trigger's type and its step range; `trigger` keeps its
   other values, such as a linked action.
  */
  auto Combo(const data::AssetKey& action, std::string_view slot,
    std::vector<data::pak::input::InputTriggerAuxRecord> steps,
    data::pak::input::InputTriggerRecord trigger = {}) -> InputContextBuilder&;

  [[nodiscard]] auto Name() const noexcept -> const std::string&
  {
    return name_;
  }

  //! The descriptor.
  /*!
   @throw std::logic_error When the product loader refuses it.
  */
  [[nodiscard]] auto Bytes() const -> std::vector<std::byte>;

  //! The references a cook records of the descriptor: the actions its
  //! mappings and triggers name.
  [[nodiscard]] auto References() const -> data::AssetReferences;

private:
  struct Mapped final {
    data::pak::input::InputActionMappingRecord record;
    std::string slot;
  };

  std::string name_;
  data::pak::input::InputMappingContextAssetDesc record_;
  std::vector<Mapped> mappings_;
  std::vector<data::pak::input::InputTriggerRecord> triggers_;
  std::vector<data::pak::input::InputTriggerAuxRecord> steps_;
};

} // namespace oxygen::cooker::testing
