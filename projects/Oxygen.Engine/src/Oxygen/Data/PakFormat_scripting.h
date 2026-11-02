//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Serio/Record.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays)

//! Oxygen PAK format scripting domain schema.
/*!
 Owns script assets/resources and scripting scene-component payload records.
*/
namespace oxygen::data::pak::scripting {

inline constexpr uint8_t kScriptAssetVersion = version::kScriptAssetVersion;

//! The most bytes a script source or a cooked script payload may hold.
inline constexpr uint64_t kMaxScriptBytes = uint64_t { 64 } * 1024 * 1024;

enum class ScriptParamType : uint32_t {
  kNone = 0,
  kBool = 1,
  kInt32 = 2,
  kFloat = 3,
  kString = 4,
  kVec2 = 5,
  kVec3 = 6,
  kVec4 = 7,
  kAsset = 8,
  kNode = 9,
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

enum class ScriptAssetFlags : uint32_t {
  kNone = 0,
  kAllowExternalSource = OXYGEN_FLAG(0),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ScriptAssetFlags)
OXGN_DATA_NDAPI auto to_string(ScriptAssetFlags value) -> std::string;

enum class ScriptingComponentFlags : uint32_t { kNone = 0 };
OXYGEN_DEFINE_FLAGS_OPERATORS(ScriptingComponentFlags)
OXGN_DATA_NDAPI auto to_string(ScriptingComponentFlags value) -> std::string;

enum class ScriptSlotFlags : uint32_t { kNone = 0 };
OXYGEN_DEFINE_FLAGS_OPERATORS(ScriptSlotFlags)
OXGN_DATA_NDAPI auto to_string(ScriptSlotFlags value) -> std::string;

//! `ScriptParamRecord::value.as_node.node_index` of a node parameter with no
//! node.
inline constexpr world::SceneNodeIndexT kUnsetNodeParam = 0xFFFFFFFFU;

//! Asset reference value of a `kAsset` script parameter.
/*!
 `asset_type` is the asset kind the script declares. A nil `key` means the
 parameter is unset.
*/
#pragma pack(push, 1)
struct ScriptAssetParamValue {
  AssetKey key;
  AssetType asset_type = AssetType::kUnknown;
};
#pragma pack(pop)
static_assert(sizeof(ScriptAssetParamValue) == 17);

//! Fixed-size script parameter record.
/*!
 Used both for a script asset's parameter declarations (key, type and default
 value) and for a script slot's overrides. A `kNode` value is a `NodeRef` into
 the scene that owns the slot: one of its own nodes, or a node an instance
 brings in. Declarations carry no node or asset default: their value is
 `kUnsetNodeParam` in the node index, or a nil key with the declared asset
 kind.

 A `kBool` value is `as_bool`, 0 or 1: records hold no C++ `bool`, which a
 byte other than 0 or 1 would make undefined behavior to read.

 `key_offset` and a `kString` value's `as_string_offset` are offsets into the
 string table of the descriptor that holds the record: the script descriptor
 for declarations, the scene descriptor for slot overrides.
*/
#pragma pack(push, 1)
// NOLINTNEXTLINE(*-type-member-init) - MUST be initialized by users
struct ScriptParamRecord {
  core::StringTableOffsetT key_offset; // Parameter name
  ScriptParamType type = ScriptParamType::kNone;
  union {
    uint8_t as_bool = 0;
    int32_t as_int32;
    float as_float;
    float as_vec[4]; // Used for Vec2, Vec3, Vec4
    core::StringTableOffsetT as_string_offset;
    ScriptAssetParamValue as_asset;
    world::NodeRef as_node;
  } value;
};
#pragma pack(pop)
static_assert(sizeof(ScriptParamRecord) == 25);
static_assert(offsetof(ScriptParamRecord, key_offset) == 0);
static_assert(offsetof(ScriptParamRecord, type) == 4);
static_assert(offsetof(ScriptParamRecord, value) == 8);

//! Whether `record` holds a known type, a key, and a value its type allows;
//! loading checks this for declarations and overrides alike.
[[nodiscard]] inline auto HasValidScriptParamValues(
  const ScriptParamRecord& record) -> bool
{
  if (record.type > ScriptParamType::kNode || record.key_offset == 0) {
    return false;
  }
  if (record.type == ScriptParamType::kAsset) {
    const auto asset_type = record.value.as_asset.asset_type;
    return asset_type != AssetType::kUnknown
      && asset_type <= AssetType::kMaxAssetType;
  }
  if (record.type == ScriptParamType::kBool) {
    return record.value.as_bool <= 1U;
  }
  return true;
}

//! Scripting component slot in a scene.
#pragma pack(push, 1)
// NOLINTNEXTLINE(*-type-member-init) - MUST be initialized by users
struct ScriptSlotRecord {
  //! Unique among the node's slots; a change of a slot's parameter names it.
  ScriptSlotId slot_id;
  AssetKey script_asset_key; // References a ScriptAssetDesc
  core::OffsetT params_array_offset = 0; // Relative to the scene descriptor
  uint32_t params_count = 0; // Number of ScriptParamRecords
  int32_t execution_order = 0; // Lower = earlier.
  ScriptSlotFlags flags = ScriptSlotFlags::kNone;
};
#pragma pack(pop)
static_assert(sizeof(ScriptSlotRecord) == 52);

//! A change of one script parameter of a node an instance brings in
//! (`ComponentType::kScriptParamOverride`).
/*!
 `target` names the node with a node path, `slot_id` its slot and `param` the
 parameter's key and value, with the parameter's type. A target holds at most
 one change per slot and key; the table is sorted by target, slot and key.
*/
#pragma pack(push, 1)
struct ScriptParamOverrideRecord {
  world::NodeRef target;
  ScriptSlotId slot_id;
  ScriptParamRecord param {};
};
#pragma pack(pop)
static_assert(sizeof(ScriptParamOverrideRecord) == 49);

//! Scripting component record for scene component tables.
#pragma pack(push, 1)
struct ScriptingComponentRecord {
  world::SceneNodeIndexT node_index = 0;
  ScriptingComponentFlags flags = ScriptingComponentFlags::kNone;
  uint32_t slot_start_index = 0;
  uint32_t slot_count = 0;
};
#pragma pack(pop)
static_assert(sizeof(ScriptingComponentRecord) == 16);

//! Script Resource Descriptor.
#pragma pack(push, 1)
struct ScriptResourceDesc {
  core::OffsetT data_offset = 0; // Absolute offset to script payload
  core::DataBlobSizeT size_bytes = 0; // Size of script payload
  ScriptLanguage language = ScriptLanguage::kLuau;
  ScriptEncoding encoding = ScriptEncoding::kBytecode;
  ScriptCompression compression = ScriptCompression::kNone;
  core::ContentHashDigest content_hash = {}; // SHA-256 of the payload
};
#pragma pack(pop)
static_assert(sizeof(ScriptResourceDesc) == 47);

//! Descriptor-local packed record array inside a script descriptor.
using ScriptDataTable = world::SceneDataTable;

//! Script Asset Descriptor.
/*!
 `params` locates the script's parameter declarations, one ScriptParamRecord
 per declared parameter, at a descriptor-relative offset after this struct.
 Authoring hints (range, unit, description, options, color) are not cooked.
 `external_source_path_offset` is in the descriptor string table, which follows
 the declarations; it holds the source's mount path, `/<Mount>/<path in
 mount>`, and offset `0` (the empty string) means no external source.
*/
#pragma pack(push, 1)
struct ScriptAssetDesc {
  core::AssetHeader header;
  ResourceReferenceIndex bytecode_resource_index = kNoResourceReference;
  ResourceReferenceIndex source_resource_index = kNoResourceReference;
  ScriptAssetFlags flags = ScriptAssetFlags::kNone;
  core::StringTableOffsetT external_source_path_offset = 0;
  ScriptDataTable params {};
};
#pragma pack(pop)
static_assert(sizeof(ScriptAssetDesc) == 83);

} // namespace oxygen::data::pak::scripting

namespace oxygen::serio {
// clang-format off
template <> inline constexpr bool kIsRecord<data::pak::scripting::ScriptingComponentRecord> = true;
template <> inline constexpr bool kIsRecord<data::pak::scripting::ScriptSlotRecord> = true;
template <> inline constexpr bool kIsRecord<data::pak::scripting::ScriptResourceDesc> = true;
template <> inline constexpr bool kIsRecord<data::pak::scripting::ScriptAssetDesc> = true;
// clang-format on
} // namespace oxygen::serio

// NOLINTEND(*-avoid-c-arrays)
OXYGEN_DIAGNOSTIC_POP
