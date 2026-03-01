//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Data/PakFormat_core.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers)

//! Oxygen PAK format input domain schema.
/*!
 Owns input action and mapping-context descriptors.
*/
namespace oxygen::data::pak::input {

[[maybe_unused]] constexpr uint8_t kInputActionAssetVersion = 1;
[[maybe_unused]] constexpr uint8_t kInputMappingContextAssetVersion = 1;

enum class InputActionAssetFlags : uint32_t { // NOLINT(*-enum-size)
  kNone = 0,
  kConsumesInput = OXYGEN_FLAG(0),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(InputActionAssetFlags)
OXGN_DATA_NDAPI auto to_string(InputActionAssetFlags value) -> std::string;

enum class InputMappingContextFlags : uint32_t { // NOLINT(*-enum-size)
  kNone = 0,
  kAutoLoad = OXYGEN_FLAG(0),
  kAutoActivate = OXYGEN_FLAG(1),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(InputMappingContextFlags)
OXGN_DATA_NDAPI auto to_string(InputMappingContextFlags value) -> std::string;

enum class InputMappingFlags : uint32_t { // NOLINT(*-enum-size)
  kNone = 0,
};
OXYGEN_DEFINE_FLAGS_OPERATORS(InputMappingFlags)
OXGN_DATA_NDAPI auto to_string(InputMappingFlags value) -> std::string;

enum class InputTriggerType : uint8_t {
// NOLINTNEXTLINE(*-macro-*)
#define OXNPUT_ACTION_TRIGGER_TYPE(name, value) name = value,
#define OXNPUT_ACTION_TRIGGER_BEHAVIOR(name, value)
#include <Oxygen/Core/Meta/Input/ActionTriggers.inc>
#undef OXNPUT_ACTION_TRIGGER_BEHAVIOR
#undef OXNPUT_ACTION_TRIGGER_TYPE
};
OXGN_DATA_NDAPI auto to_string(InputTriggerType value) noexcept
  -> std::string_view;

enum class InputTriggerBehavior : uint8_t {
#define OXNPUT_ACTION_TRIGGER_TYPE(name, value)
// NOLINTNEXTLINE(*-macro-*)
#define OXNPUT_ACTION_TRIGGER_BEHAVIOR(name, value) name = value,
#include <Oxygen/Core/Meta/Input/ActionTriggers.inc>
#undef OXNPUT_ACTION_TRIGGER_BEHAVIOR
#undef OXNPUT_ACTION_TRIGGER_TYPE
};
OXGN_DATA_NDAPI auto to_string(InputTriggerBehavior value) noexcept
  -> std::string_view;

#pragma pack(push, 1)
struct InputDataTable {
  uint64_t offset = 0;
  uint32_t count = 0;
  uint32_t entry_size = 0;
};
#pragma pack(pop)
static_assert(sizeof(InputDataTable) == 16);

#pragma pack(push, 1)
struct InputActionAssetDesc {
  core::AssetHeader header;
  uint8_t value_type = 0;
  uint8_t reserved0[3] = {};
  InputActionAssetFlags flags = InputActionAssetFlags::kNone;
  uint8_t reserved1[153] = {};
};
#pragma pack(pop)
static_assert(sizeof(InputActionAssetDesc) == 256);

#pragma pack(push, 1)
struct InputMappingContextAssetDesc {
  core::AssetHeader header;
  InputMappingContextFlags flags = InputMappingContextFlags::kNone;
  int32_t default_priority = 0;
  InputDataTable mappings = {};
  InputDataTable triggers = {};
  InputDataTable trigger_aux = {};
  InputDataTable strings = {};
  uint8_t reserved[89] = {};
};
#pragma pack(pop)
static_assert(sizeof(InputMappingContextAssetDesc) == 256);

#pragma pack(push, 1)
struct InputActionMappingRecord {
  AssetKey action_asset_key = {};
  uint32_t slot_name_offset = 0;
  uint32_t trigger_start_index = 0;
  uint32_t trigger_count = 0;
  InputMappingFlags flags = InputMappingFlags::kNone;
  float scale[2] = { 1.0F, 1.0F };
  float bias[2] = { 0.0F, 0.0F };
  uint8_t reserved[16] = {};
};
#pragma pack(pop)
static_assert(sizeof(InputActionMappingRecord) == 64);

#pragma pack(push, 1)
struct InputTriggerRecord {
  InputTriggerType type = InputTriggerType::kPressed;
  InputTriggerBehavior behavior = InputTriggerBehavior::kImplicit;
  uint16_t reserved0 = 0;
  uint32_t flags = 0;
  float actuation_threshold = 0.5F;
  AssetKey linked_action_asset_key = {};
  uint32_t aux_start_index = 0;
  uint32_t aux_count = 0;
  float fparams[5] = {};
  uint32_t uparams[5] = {};
  uint8_t reserved1[20] = {};
};
#pragma pack(pop)
static_assert(sizeof(InputTriggerRecord) == 96);

#pragma pack(push, 1)
struct InputTriggerAuxRecord {
  AssetKey action_asset_key = {};
  uint32_t completion_states = 0;
  uint64_t time_to_complete_ns = 0;
  uint32_t flags = 0;
};
#pragma pack(pop)
static_assert(sizeof(InputTriggerAuxRecord) == 32);

} // namespace oxygen::data::pak::input

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
