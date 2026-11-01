//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
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
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::loaders {

namespace detail {

  //! Reads the parameter declarations that follow a script descriptor.
  /*!
   @param reader Descriptor reader; its position is unspecified afterwards.
   @param base_pos Reader position of the descriptor's first byte.
   @param table The descriptor's declaration table.
   @throw std::runtime_error if the table or a declaration is invalid.
  */
  inline auto ReadScriptParamDeclarations(serio::AnyReader& reader,
    const size_t base_pos, const data::pak::scripting::ScriptDataTable& table)
    -> std::vector<data::pak::scripting::ScriptParamRecord>
  {
    using data::pak::scripting::ScriptParamRecord;

    auto declarations = std::vector<ScriptParamRecord> {};
    if (table.count == 0U) {
      if (table.offset != 0U) {
        throw std::runtime_error(
          "Empty script parameter declarations have nonzero offset");
      }
      return declarations;
    }
    if (table.entry_size != sizeof(ScriptParamRecord)
      || table.offset < sizeof(data::pak::scripting::ScriptAssetDesc)) {
      throw std::runtime_error("Script parameter declaration table is invalid");
    }

    CheckLoaderResult(reader.Seek(base_pos + table.offset), "script asset",
      "Seek(param declarations)");
    declarations.resize(table.count);
    for (auto& declaration : declarations) {
      CheckLoaderResult(reader.ReadInto(declaration), "script asset",
        "ReadInto(ScriptParamRecord)");
    }
    return declarations;
  }

} // namespace detail

//! Loader for script assets.
inline auto LoadScriptAsset(const LoaderContext& context)
  -> std::unique_ptr<data::ScriptAsset>
{
  LOG_SCOPE_FUNCTION(1);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  auto pack = reader.ScopedAlignment(1);

  const auto base_pos = reader.Position();
  CheckLoaderResult(base_pos, "script asset", "Position(base)");
  auto desc_blob
    = reader.ReadBlob(sizeof(data::pak::scripting::ScriptAssetDesc));
  CheckLoaderResult(desc_blob, "script asset", "ScriptAssetDesc");
  const auto parsed_desc
    = serio::CopyAt<data::pak::scripting::ScriptAssetDesc>(*desc_blob);
  CheckLoaderResult(parsed_desc, "script asset", "ScriptAssetDesc");
  const auto desc = *parsed_desc;

  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kScript) {
    throw std::runtime_error("invalid asset type for script descriptor");
  }
  if (desc.header.version != data::pak::scripting::kScriptAssetVersion) {
    throw std::runtime_error(
      "Script descriptor version is not current; re-cook the script");
  }

  const auto resource_uses = std::array {
    data::ResourceReferenceUse {
      .reference = desc.bytecode_resource_index,
      .kind = data::ResourceKind::kScript,
    },
    data::ResourceReferenceUse {
      .reference = desc.source_resource_index,
      .kind = data::ResourceKind::kScript,
    },
  };
  context.ValidateReferences(resource_uses, {});

  const auto strings = ReadDescriptorStrings(reader, *base_pos, desc.header);
  const auto declared_params
    = detail::ReadScriptParamDeclarations(reader, *base_pos, desc.params);
  SkipDescriptorStrings(reader, *base_pos, desc.header);
  auto params = data::ScriptParameters {};
  try {
    params = data::DecodeScriptParameters(declared_params, strings);
  } catch (const std::invalid_argument& error) {
    throw std::runtime_error(error.what());
  }

  if (!context.parse_only) {
    if (!context.dependency_collector) {
      throw std::runtime_error(
        "ScriptAsset loader requires a dependency collector for non-parse-only "
        "loads");
    }

    const auto script_indices
      = std::array { desc.bytecode_resource_index, desc.source_resource_index };
    for (size_t i = 0; i < script_indices.size(); ++i) {
      const auto reference = script_indices.at(i);
      if (reference == data::kNoResourceReference) {
        continue;
      }
      if (i == 1 && reference == script_indices.at(0)) {
        continue;
      }
      const auto resource_index
        = context.ResolveResource(reference, data::ResourceKind::kScript);
      if (!resource_index) {
        continue;
      }
      internal::ResourceRef ref {
        .source = context.source_instance,
        .resource_type_id = data::ScriptResource::ClassTypeId(),
        .resource_index = *resource_index,
      };
      context.dependency_collector->AddResourceDependency(ref);
    }
  }

  auto script = std::make_unique<data::ScriptAsset>(context.current_asset_key,
    desc, std::move(params),
    ResolveDescriptorString(
      strings, desc.external_source_path_offset, "external source path"),
    data::SourceOrigin {
      .key = context.source_key,
      .instance = context.source_instance,
    });
  script->SetAssetName(
    ResolveDescriptorString(strings, desc.header.name_offset, "asset name"));
  return script;
}

//! Loader for script resources (bytecode or source blobs).
inline auto LoadScriptResource(const LoaderContext& context)
  -> std::unique_ptr<data::ScriptResource>
{
  LOG_SCOPE_FUNCTION(1);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  auto pack = reader.ScopedAlignment(1);
  auto desc_blob
    = reader.ReadBlob(sizeof(data::pak::scripting::ScriptResourceDesc));
  CheckLoaderResult(desc_blob, "script resource", "ScriptResourceDesc");
  const auto parsed_desc
    = serio::CopyAt<data::pak::scripting::ScriptResourceDesc>(*desc_blob);
  CheckLoaderResult(parsed_desc, "script resource", "ScriptResourceDesc");
  const auto desc = *parsed_desc;

  auto data_buffer = TakePayload(context, desc.size_bytes, "script resource");

  if (const auto digest = base::ComputeSha256(data_buffer.Bytes());
    digest != desc.content_hash) {
    throw std::runtime_error(
      fmt::format("script resource SHA-256 mismatch (expected={}, got={})",
        base::ToHexString(desc.content_hash), base::ToHexString(digest)));
  }

  return std::make_unique<data::ScriptResource>(desc, std::move(data_buffer));
}

static_assert(oxygen::content::LoadFunction<decltype(LoadScriptAsset)>);
static_assert(oxygen::content::LoadFunction<decltype(LoadScriptResource)>);

} // namespace oxygen::content::loaders
