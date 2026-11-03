//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::loaders {

inline auto LoadInputActionAsset(const LoaderContext& context)
  -> std::unique_ptr<data::InputActionAsset>
{
  LOG_SCOPE_FUNCTION(1);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  auto check_result = [](auto&& result, const char* field) -> auto {
    if (!result) {
      LOG_F(
        ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
      throw data::CookedFormatError(data::CookedDefect::kTruncated,
        fmt::format("error reading input action asset ({}): {}", field,
          result.error().message()));
    }
  };

  auto pack = reader.ScopedAlignment(1);
  const auto base_pos = reader.Position();
  check_result(base_pos, "Position(base)");

  auto desc_blob
    = reader.ReadBlob(sizeof(data::pak::input::InputActionAssetDesc));
  check_result(desc_blob, "InputActionAssetDesc");
  const auto parsed_desc
    = serio::CopyAt<data::pak::input::InputActionAssetDesc>(*desc_blob);
  check_result(parsed_desc, "InputActionAssetDesc");
  const auto desc = *parsed_desc;

  if (desc.header.version != data::pak::input::kInputActionAssetVersion) {
    throw data::CookedFormatError(data::CookedDefect::kUnsupportedVersion,
      "Input descriptor version is not current; re-cook the asset");
  }

  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kInputAction) {
    throw data::CookedFormatError(data::CookedDefect::kWrongMagic,
      "invalid asset type for input action descriptor");
  }

  const auto known_value_type
    = data::pak::input::IsKnownValueType(desc.value_type);
  if (!known_value_type) {
    throw data::CookedFormatError(
      data::CookedDefect::kInvalidValue, "invalid input action value_type");
  }

  context.ValidateReferences({}, {});
  const auto strings = ReadDescriptorStrings(reader, *base_pos, desc.header);
  SkipDescriptorStrings(reader, *base_pos, desc.header);

  // Input contexts and scripts find an action by its name.
  auto name
    = ResolveDescriptorString(strings, desc.header.name_offset, "asset name");
  if (name.empty()) {
    throw ContentLoadError(
      ContentLoadFailure::kContentInvalid, "input action has no name");
  }

  auto asset
    = std::make_unique<data::InputActionAsset>(context.current_asset_key, desc,
      data::SourceOrigin {
        .key = context.source_key,
        .instance = context.source_instance,
      });
  asset->SetAssetName(std::move(name));
  return asset;
}

static_assert(oxygen::content::LoadFunction<decltype(LoadInputActionAsset)>);

} // namespace oxygen::content::loaders
