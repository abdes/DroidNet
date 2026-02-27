//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Data/PakFormat_world.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers)

//! Oxygen PAK format scripting domain schema.
/*!
 Owns script assets/resources and scripting scene-component payload records.
*/
namespace oxygen::data::pak::scripting {

// NOLINTNEXTLINE(*-enum-size)
enum class ScriptParamType : uint32_t {
  kNone = 0,
  kBool = 1,
  kInt32 = 2,
  kFloat = 3,
  kString = 4,
  kVec2 = 5,
  kVec3 = 6,
  kVec4 = 7
};
OXGN_DATA_NDAPI auto to_string(ScriptParamType value) noexcept
  -> std::string_view;

enum class ScriptLanguage : uint8_t { kLuau = 0 };
OXGN_DATA_NDAPI auto to_string(ScriptLanguage value) noexcept
  -> std::string_view;

enum class ScriptEncoding : uint8_t { kBytecode = 0, kSource = 1 };
OXGN_DATA_NDAPI auto to_string(ScriptEncoding value) noexcept
  -> std::string_view;

enum class ScriptCompression : uint8_t { kNone = 0, kZstd = 1 };
OXGN_DATA_NDAPI auto to_string(ScriptCompression value) noexcept
  -> std::string_view;

enum class ScriptAssetFlags : uint32_t { // NOLINT(*-enum-size)
  kNone = 0,
  kAllowExternalSource = OXYGEN_FLAG(0),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ScriptAssetFlags)
OXGN_DATA_NDAPI auto to_string(ScriptAssetFlags value) -> std::string;

enum class ScriptingComponentFlags : uint32_t { // NOLINT(*-enum-size)
  kNone = 0
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ScriptingComponentFlags)
OXGN_DATA_NDAPI auto to_string(ScriptingComponentFlags value) noexcept
  -> std::string;

enum class ScriptSlotFlags : uint32_t { // NOLINT(*-enum-size)
  kNone = 0
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ScriptSlotFlags)
OXGN_DATA_NDAPI auto to_string(ScriptSlotFlags value) noexcept -> std::string;

//! Fixed-size script parameter record (128 bytes).
#pragma pack(push, 1)
// NOLINTNEXTLINE(*-type-member-init) - MUST be initialized by users
struct ScriptParamRecord {
  char key[64]; // Parameter name (null-terminated)
  ScriptParamType type = ScriptParamType::kNone;
  union {
    bool as_bool;
    int32_t as_int32;
    float as_float;
    float as_vec[4]; // Used for Vec2, Vec3, Vec4
    char as_string[60]; // Maximum 59 chars + null terminator
  } value;
};
#pragma pack(pop)
static_assert(sizeof(ScriptParamRecord) == 128);

//! Scripting component slot in a scene (128 bytes).
#pragma pack(push, 1)
// NOLINTNEXTLINE(*-type-member-init) - MUST be initialized by users
struct ScriptSlotRecord {
  AssetKey script_asset_key; // References a ScriptAssetDesc
  core::OffsetT params_array_offset = 0; // Absolute offset in PAK
  uint32_t params_count = 0; // Number of ScriptParamRecords
  int32_t execution_order = 0; // Lower = earlier.
  ScriptSlotFlags flags = ScriptSlotFlags::kNone;
  uint8_t reserved[92] = {}; // Padded to 128 bytes
};
#pragma pack(pop)
static_assert(sizeof(ScriptSlotRecord) == 128);

//! Scripting component record for scene component tables (16 bytes).
#pragma pack(push, 1)
struct ScriptingComponentRecord {
  world::SceneNodeIndexT node_index = 0;
  ScriptingComponentFlags flags = ScriptingComponentFlags::kNone;
  uint32_t slot_start_index = 0;
  uint32_t slot_count = 0;
};
#pragma pack(pop)
static_assert(sizeof(ScriptingComponentRecord) == 16);

//! Script Resource Descriptor (32 bytes).
#pragma pack(push, 1)
struct ScriptResourceDesc {
  core::OffsetT data_offset = 0; // Absolute offset to script payload
  core::DataBlobSizeT size_bytes = 0; // Size of script payload
  ScriptLanguage language = ScriptLanguage::kLuau;
  ScriptEncoding encoding = ScriptEncoding::kBytecode;
  ScriptCompression compression = ScriptCompression::kNone;
  uint64_t content_hash = 0; // First 8 bytes of SHA256 of payload
  uint8_t reserved[9] = {}; // Padded to 32 bytes
};
#pragma pack(pop)
static_assert(sizeof(ScriptResourceDesc) == 32);

//! Script Asset Descriptor (256 bytes).
#pragma pack(push, 1)
struct ScriptAssetDesc {
  core::AssetHeader header;
  core::ResourceIndexT bytecode_resource_index = core::kNoResourceIndex;
  core::ResourceIndexT source_resource_index = core::kNoResourceIndex;
  ScriptAssetFlags flags = ScriptAssetFlags::kNone;
  char external_source_path[120] = {}; // Null-terminated, null-padded
  uint8_t reserved[29] = {}; // Padded to 256 bytes
};
#pragma pack(pop)
static_assert(sizeof(ScriptAssetDesc) == 256);

} // namespace oxygen::data::pak::scripting

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
