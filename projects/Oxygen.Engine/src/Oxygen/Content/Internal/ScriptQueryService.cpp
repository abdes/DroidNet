//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>
#include <span>
#include <utility>

#include <Oxygen/Content/Internal/ScriptQueryService.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Device.h>

namespace oxygen::content::internal {

auto ScriptQueryService::MakeScriptResourceKeyForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::core::ResourceIndexT resource_index,
  const Callbacks& callbacks) -> std::optional<ResourceKey>
{
  const auto source_id
    = callbacks.resolve_source_id_for_asset(context_asset_key);
  if (!source_id.has_value()) {
    return std::nullopt;
  }
  return callbacks.make_script_resource_key(*source_id, resource_index);
}

auto ScriptQueryService::ReadScriptResourceForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::core::ResourceIndexT resource_index,
  const Callbacks& callbacks) const
  -> std::shared_ptr<const data::ScriptResource>
{
  const auto source_id
    = callbacks.resolve_source_id_for_asset(context_asset_key);
  if (!source_id.has_value()) {
    return nullptr;
  }

  auto source = callbacks.resolve_source_for_id(*source_id);
  if (source == nullptr) {
    return nullptr;
  }
  const auto* script_table = source->GetScriptTable();
  if (script_table == nullptr || !script_table->IsValidKey(resource_index)) {
    return nullptr;
  }

  const auto row = source->ResourceDescriptorBytes(
    data::ScriptResource::ClassTypeId(), resource_index);
  if (!row || row->size() != sizeof(data::pak::scripting::ScriptResourceDesc)) {
    return nullptr;
  }
  const auto parsed_desc
    = serio::CopyAt<data::pak::scripting::ScriptResourceDesc>(*row);
  if (!parsed_desc) {
    return nullptr;
  }
  const auto desc = *parsed_desc;

  // A synchronous query: the payload is read with the device's blocking read.
  serio::ByteBuffer data_buffer;
  if (desc.size_bytes > 0) {
    const auto region = source->PayloadOf(data::ScriptResource::ClassTypeId(),
      {
        .offset = serio::ByteOffset { desc.data_offset },
        .count = serio::ByteCount { desc.size_bytes },
      });
    if (!region) {
      return nullptr;
    }
    auto payload = serio::ReadAll(*region);
    if (!payload) {
      return nullptr;
    }
    data_buffer = std::move(payload).value();
  }

  auto resource
    = std::make_unique<data::ScriptResource>(desc, std::move(data_buffer));
  auto* pointer = resource.get();
  return { pointer,
    [source = std::move(source), resource = std::move(resource)](
      const data::ScriptResource*) mutable -> void {
      resource.reset();
      source.reset();
    } };
}

} // namespace oxygen::content::internal
