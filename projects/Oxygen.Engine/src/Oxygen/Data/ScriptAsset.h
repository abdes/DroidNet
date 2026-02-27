//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Script parameter variant for runtime use.
using ScriptParam
  = std::variant<bool, int32_t, float, Vec2, Vec3, Vec4, std::string>;

//! Script asset as stored in the PAK file asset directory.
/*!
  Represents a script asset that can be attached to scene nodes. It
  references
  script resources (bytecode and/or source), may expose an optional
  external
  source path, and can contain default parameters.

  @see ScriptAssetDesc, ScriptParamRecord, ScriptResource
*/
class ScriptAsset : public Asset {
  OXYGEN_TYPED(ScriptAsset)

public:
  struct DefaultParameterEntry {
    std::string_view key;
    std::reference_wrapper<const ScriptParam> value;
  };

  //! Constructs a ScriptAsset with descriptor, default parameters, and resource
  //! metadata.
  OXGN_DATA_API ScriptAsset(AssetKey asset_key,
    pak::scripting::ScriptAssetDesc desc,
    const std::vector<pak::scripting::ScriptParamRecord>& default_params = {});

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

  //! Returns the bytecode ScriptResource index (0 means not assigned).
  [[nodiscard]] auto GetBytecodeResourceIndex() const noexcept
  {
    return desc_.bytecode_resource_index;
  }

  //! Returns the source ScriptResource index (0 means not assigned).
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
      != 0u;
  }

  //! Returns external source path when present and valid.
  [[nodiscard]] OXGN_DATA_API auto TryGetExternalSourcePath() const noexcept
    -> std::optional<std::string_view>;

  //! Returns external source path or throws when absent/invalid.
  [[nodiscard]] OXGN_DATA_API auto GetExternalSourcePath() const
    -> std::string_view;

  //! Returns true if any embedded script payload is assigned.
  [[nodiscard]] auto HasEmbeddedResource() const noexcept -> bool
  {
    return GetBytecodeResourceIndex() != pak::core::kNoResourceIndex
      || GetSourceResourceIndex() != pak::core::kNoResourceIndex;
  }

  //! Returns the number of default parameters defined for this script.
  [[nodiscard]] auto ParametersCount() const noexcept -> std::size_t
  {
    return params_.size();
  }

  //! True when a default parameter with this key exists.
  [[nodiscard]] OXGN_DATA_API auto HasParameter(
    std::string_view key) const noexcept -> bool;

  //! Tries to retrieve a default parameter by name.
  //! Returns std::nullopt when the key is not present.
  [[nodiscard]] OXGN_DATA_API auto TryGetParameter(
    std::string_view key) const noexcept
    -> std::optional<std::reference_wrapper<const ScriptParam>>;

  //! Retrieves a default parameter by name.
  //! Throws std::out_of_range when not found.
  [[nodiscard]] OXGN_DATA_API auto GetParameter(std::string_view key) const
    -> const ScriptParam&;

  //! Returns a lazy view over all default parameters.
  //! Entry references remain valid for the lifetime of this ScriptAsset.
  [[nodiscard]] auto Parameters() const
  {
    return params_ | std::views::transform([](const auto& kv) -> auto {
      return DefaultParameterEntry { .key = kv.first,
        .value = std::cref(kv.second) };
    });
  }

private:
  pak::scripting::ScriptAssetDesc desc_ {};
  std::unordered_map<std::string, ScriptParam> params_;
};

} // namespace oxygen::data
