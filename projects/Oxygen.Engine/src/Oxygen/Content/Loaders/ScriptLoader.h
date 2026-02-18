//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>

namespace oxygen::content::loaders {

//! Loader for script assets.
inline auto LoadScriptAsset(const LoaderContext& context)
  -> std::unique_ptr<data::ScriptAsset>
{
  LOG_SCOPE_FUNCTION(INFO);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  auto pack = reader.ScopedAlignment(1);

  auto desc_blob = reader.ReadBlob(sizeof(data::pak::ScriptAssetDesc));
  CheckLoaderResult(desc_blob, "script asset", "ScriptAssetDesc");
  data::pak::ScriptAssetDesc desc {};
  std::memcpy(&desc, desc_blob->data(), sizeof(desc));

  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kScript) {
    throw std::runtime_error("invalid asset type for script descriptor");
  }

  if (!context.parse_only) {
    if (!context.dependency_collector) {
      throw std::runtime_error(
        "ScriptAsset loader requires a dependency collector for non-parse-only "
        "loads");
    }

    // TODO(phase-next): Wire runtime external source loading using
    // desc.external_source_path + ScriptAssetFlags::kAllowExternalSource.
    // Current runtime path resolves embedded script resources only.

    const auto script_indices
      = std::array { desc.bytecode_resource_index, desc.source_resource_index };
    for (size_t i = 0; i < script_indices.size(); ++i) {
      const auto resource_index = script_indices.at(i);
      if (resource_index == data::pak::kNoResourceIndex) {
        continue;
      }
      if (i == 1 && resource_index == script_indices[0]) {
        continue;
      }
      internal::ResourceRef ref {
        .source = context.source_token,
        .resource_type_id = data::ScriptResource::ClassTypeId(),
        .resource_index = resource_index,
      };
      context.dependency_collector->AddResourceDependency(ref);
    }
  }

  // ScriptAsset defaults are currently carried by ScriptSlotRecord parameter
  // arrays in scene data. Asset-level defaults remain empty in this phase.
  return std::make_unique<data::ScriptAsset>(context.current_asset_key, desc,
    std::vector<data::pak::ScriptParamRecord> {});
}

//! Loader for script resources (bytecode or source blobs).
inline auto LoadScriptResource(LoaderContext context)
  -> std::unique_ptr<data::ScriptResource>
{
  LOG_SCOPE_FUNCTION(INFO);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  auto pack = reader.ScopedAlignment(1);
  auto desc_blob = reader.ReadBlob(sizeof(data::pak::ScriptResourceDesc));
  CheckLoaderResult(desc_blob, "script resource", "ScriptResourceDesc");
  data::pak::ScriptResourceDesc desc {};
  std::memcpy(&desc, desc_blob->data(), sizeof(desc));

  std::vector<uint8_t> data_buffer(desc.size_bytes);
  if (desc.size_bytes > 0) {
    constexpr std::size_t script_index
      = IndexOf<data::ScriptResource, ResourceTypeList>::value;
    DCHECK_NOTNULL_F(std::get<script_index>(context.data_readers),
      "expecting data reader for ScriptResource to be valid");
    auto& data_reader = *std::get<script_index>(context.data_readers);

    CheckLoaderResult(
      data_reader.Seek(desc.data_offset), "script resource", "data seek");
    const auto byte_view = std::as_writable_bytes(std::span(data_buffer));
    CheckLoaderResult(
      data_reader.ReadBlobInto(byte_view), "script resource", "data read");
  }

  if (desc.content_hash != 0) {
    constexpr size_t kScriptContentHashBytes = 8;
    constexpr size_t kByteShiftBits = 8;
    const auto byte_span = std::span(data_buffer);
    const auto digest = base::ComputeSha256(std::as_bytes(byte_span));
    uint64_t computed_hash = 0;
    for (size_t i = 0; i < kScriptContentHashBytes; ++i) {
      computed_hash |= static_cast<uint64_t>(digest.at(i))
        << (i * kByteShiftBits);
    }
    if (computed_hash != desc.content_hash) {
      throw std::runtime_error(
        fmt::format("script resource content hash mismatch "
                    "(expected=0x{:016X}, got=0x{:016X})",
          desc.content_hash, computed_hash));
    }
  }

  return std::make_unique<data::ScriptResource>(desc, std::move(data_buffer));
}

static_assert(oxygen::content::LoadFunction<decltype(LoadScriptAsset)>);
static_assert(oxygen::content::LoadFunction<decltype(LoadScriptResource)>);

} // namespace oxygen::content::loaders
