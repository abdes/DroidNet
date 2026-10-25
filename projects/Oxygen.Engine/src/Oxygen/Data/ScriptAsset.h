//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Asset reference value of a script parameter.
/*!
 `asset_type` is the declared asset kind; a nil `key` means unset.
*/
struct ScriptAssetParam {
  AssetKey key;
  AssetType asset_type = AssetType::kUnknown;

  auto operator==(const ScriptAssetParam&) const -> bool = default;
};

//! Scene node reference value of a script parameter.
/*!
 `node_index` indexes the nodes of the scene that owns the slot;
 `pak::scripting::kUnsetNodeParam` means unset. With a `node_path`, the node
 is one the instance rooted at `node_index` brings in, named by the scene's
 node path table.
*/
struct ScriptNodeParam {
  pak::world::SceneNodeIndexT node_index = pak::scripting::kUnsetNodeParam;
  pak::world::NodePathIndexT node_path = pak::world::kNoNodePath;

  auto operator==(const ScriptNodeParam&) const -> bool = default;
};

//! Script parameter variant for runtime use.
using ScriptParam = std::variant<bool, int32_t, float, Vec2, Vec3, Vec4,
  std::string, ScriptAssetParam, ScriptNodeParam>;

//! Declared script parameters: each key and default value, in declaration
//! order.
using ScriptParameters = std::vector<std::pair<std::string, ScriptParam>>;

//! Returns the key of a cooked script parameter record.
/*!
 @param strings The string table of the descriptor that holds the record.
 @return The key, or std::nullopt when it is empty or not in the table.
*/
OXGN_DATA_NDAPI auto TryGetScriptParamKey(
  const pak::scripting::ScriptParamRecord& record,
  const DescriptorStrings& strings) noexcept -> std::optional<std::string_view>;

//! Converts the value of a cooked script parameter record.
/*!
 @param strings The string table of the descriptor that holds the record.
 @return The value, or std::nullopt for `kNone`, an unknown type or a string
 that is not in the table.
*/
OXGN_DATA_NDAPI auto ToScriptParam(
  const pak::scripting::ScriptParamRecord& record,
  const DescriptorStrings& strings) -> std::optional<ScriptParam>;

//! Decodes cooked parameter declarations.
/*!
 @param strings The string table of the descriptor that holds the records.
 @throw std::invalid_argument if a declaration has an invalid key or value,
   or repeats a key.
*/
OXGN_DATA_NDAPI auto DecodeScriptParameters(
  std::span<const pak::scripting::ScriptParamRecord> records,
  const DescriptorStrings& strings) -> ScriptParameters;

//! Builds a cooked script parameter record from a key and a value.
/*!
 The record type follows the alternative `value` holds. The key and a string
 value are added to `strings`, the table of the descriptor that will hold the
 record.

 @return The record, or std::nullopt when the key is empty, or the key or a
 string value holds a NUL byte.
*/
OXGN_DATA_NDAPI auto MakeScriptParamRecord(std::string_view key,
  const ScriptParam& value, DescriptorStringsBuilder& strings)
  -> std::optional<pak::scripting::ScriptParamRecord>;

//! Script asset as stored in the PAK file asset directory.
/*!
  Represents a script asset that can be attached to scene nodes. It
  references script resources (bytecode and/or source), may expose an
  optional external source path, and carries the parameters the script
  declares: each parameter's key, type (the alternative held by its value,
  plus the asset kind for asset parameters) and default value, in declaration
  order.

  @see ScriptAssetDesc, ScriptParamRecord, ScriptResource
*/
class ScriptAsset : public Asset {
  OXYGEN_TYPED(ScriptAsset)

public:
  struct DefaultParameterEntry {
    std::string_view key;
    std::reference_wrapper<const ScriptParam> value;
  };

  //! Constructs a ScriptAsset with descriptor, declared parameters, and
  //! resource metadata.
  /*!
   @param external_source_path The external source path, empty when none.
   @throw std::invalid_argument if a declaration has an empty key or repeats
   a key.
  */
  OXGN_DATA_API ScriptAsset(AssetKey asset_key,
    pak::scripting::ScriptAssetDesc desc, ScriptParameters declared_params = {},
    std::string external_source_path = {}, SourceOrigin source_origin = {});

  ~ScriptAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ScriptAsset)
  OXYGEN_DEFAULT_MOVABLE(ScriptAsset)

  //! Returns the asset header metadata.
  [[nodiscard]] auto GetHeader() const noexcept
    -> const pak::core::AssetHeader& override
  {
    return desc_.header;
  }

  //! Returns the script flags bitfield.
  [[nodiscard]] auto GetFlags() const noexcept
    -> pak::scripting::ScriptAssetFlags
  {
    return desc_.flags;
  }

  //! Returns the bytecode descriptor-local ScriptResource reference.
  [[nodiscard]] auto GetBytecodeResourceIndex() const noexcept
  {
    return desc_.bytecode_resource_index;
  }

  //! Returns the source descriptor-local ScriptResource reference.
  [[nodiscard]] auto GetSourceResourceIndex() const noexcept
  {
    return desc_.source_resource_index;
  }

  //! Returns true when this asset permits external source fallback.
  [[nodiscard]] auto AllowsExternalSource() const noexcept -> bool
  {
    return (static_cast<uint32_t>(desc_.flags)
             & static_cast<uint32_t>(
               pak::scripting::ScriptAssetFlags::kAllowExternalSource))
      != 0U;
  }

  //! Returns the external source path when present.
  [[nodiscard]] auto TryGetExternalSourcePath() const noexcept
    -> std::optional<std::string_view>
  {
    if (external_source_path_.empty()) {
      return std::nullopt;
    }
    return std::string_view { external_source_path_ };
  }

  //! Returns external source path or throws when absent.
  [[nodiscard]] OXGN_DATA_API auto GetExternalSourcePath() const
    -> std::string_view;

  //! Returns true if any embedded script payload is assigned.
  [[nodiscard]] auto HasEmbeddedResource() const noexcept -> bool
  {
    return GetBytecodeResourceIndex() != kNoResourceReference
      || GetSourceResourceIndex() != kNoResourceReference;
  }

  //! A copy of this asset that declares `declared_params` instead, as after
  //! its source changed. The key, descriptor, name, external source path and
  //! source origin are kept.
  /*!
   @throw std::invalid_argument as the constructor.
  */
  [[nodiscard]] OXGN_DATA_API auto WithDeclarations(
    ScriptParameters declared_params) const -> std::shared_ptr<ScriptAsset>;

  //! Returns the number of parameters this script declares.
  [[nodiscard]] auto ParametersCount() const noexcept -> std::size_t
  {
    return params_.size();
  }

  //! True when the script declares a parameter with this key.
  [[nodiscard]] OXGN_DATA_API auto HasParameter(
    std::string_view key) const noexcept -> bool;

  //! Tries to retrieve a declared parameter's default value by name.
  //! Returns std::nullopt when the key is not declared.
  [[nodiscard]] OXGN_DATA_API auto TryGetParameter(
    std::string_view key) const noexcept
    -> std::optional<std::reference_wrapper<const ScriptParam>>;

  //! Retrieves a declared parameter's default value by name.
  //! Throws std::out_of_range when not declared.
  [[nodiscard]] OXGN_DATA_API auto GetParameter(std::string_view key) const
    -> const ScriptParam&;

  //! Returns a lazy view over all declared parameters, in declaration order.
  //! Entry references remain valid for the lifetime of this ScriptAsset.
  [[nodiscard]] auto Parameters() const
  {
    return params_ | std::views::transform([](const auto& kv) -> auto {
      return DefaultParameterEntry {
        .key = kv.first,
        .value = std::cref(kv.second),
      };
    });
  }

private:
  pak::scripting::ScriptAssetDesc desc_ {};
  ScriptParameters params_;
  std::string external_source_path_;
};

} // namespace oxygen::data
