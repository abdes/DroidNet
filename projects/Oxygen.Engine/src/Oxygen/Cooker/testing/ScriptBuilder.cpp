//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/ProductLoad.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>

namespace oxygen::cooker::testing {

namespace {

  namespace scripting = data::pak::scripting;

  //! The descriptor and its references, encoded together: the payload
  //! fields name the references' rows.
  struct Encoded final {
    std::vector<std::byte> bytes;
    data::AssetReferences references;
  };

  auto Encode(const std::string& name,
    const std::optional<ResourceIndexT> bytecode,
    const std::optional<ResourceIndexT> source,
    const std::string& external_source, const scripting::ScriptAssetFlags flags,
    const data::ScriptParameters& parameters) -> Encoded
  {
    auto descriptor = scripting::ScriptAssetDesc {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kScript);
    descriptor.header.version = scripting::kScriptAssetVersion;
    descriptor.flags = flags;
    auto strings = data::DescriptorStringsBuilder {};
    descriptor.header.name_offset = strings.Add(name);
    // The cook binds the bytecode first, then the source.
    auto references = content::import::AssetReferenceBuilder {};
    if (bytecode.has_value()) {
      descriptor.bytecode_resource_index
        = references.AddResource(data::ResourceKind::kScript, *bytecode);
    }
    if (source.has_value()) {
      descriptor.source_resource_index
        = references.AddResource(data::ResourceKind::kScript, *source);
    }
    auto records = std::vector<scripting::ScriptParamRecord> {};
    for (const auto& [key, value] : parameters) {
      const auto record = data::MakeScriptParamRecord(key, value, strings);
      if (!record.has_value()) {
        auto message = std::string { "the script '" };
        message.append(name).append("' has an invalid parameter '");
        message.append(key).append("'");
        throw std::invalid_argument(message);
      }
      records.push_back(*record);
    }
    if (!records.empty()) {
      descriptor.params = {
        .offset = sizeof(scripting::ScriptAssetDesc),
        .count = static_cast<uint32_t>(records.size()),
        .entry_size = sizeof(scripting::ScriptParamRecord),
      };
    }
    if (!external_source.empty()) {
      descriptor.external_source_path_offset = strings.Add(external_source);
    }
    auto encoded = Encoded {
      .bytes = {},
      .references = std::move(references).Build(),
    };
    AppendRecord(encoded.bytes, descriptor);
    const auto params = std::as_bytes(std::span(records));
    encoded.bytes.insert(encoded.bytes.end(), params.begin(), params.end());
    data::AppendDescriptorStrings(encoded.bytes, strings);
    data::StampDescriptorHash(encoded.bytes);
    return encoded;
  }

} // namespace

ScriptBuilder::ScriptBuilder(const std::string_view name)
  : name_(name)
{
}

auto ScriptBuilder::Bytecode(const ResourceIndexT row) -> ScriptBuilder&
{
  bytecode_ = row;
  return *this;
}

auto ScriptBuilder::Source(const ResourceIndexT row) -> ScriptBuilder&
{
  source_ = row;
  return *this;
}

auto ScriptBuilder::ExternalSource(std::string mount_path) -> ScriptBuilder&
{
  external_source_ = std::move(mount_path);
  return *this;
}

auto ScriptBuilder::Flags(const scripting::ScriptAssetFlags flags)
  -> ScriptBuilder&
{
  flags_ = flags;
  return *this;
}

auto ScriptBuilder::Parameters(data::ScriptParameters parameters)
  -> ScriptBuilder&
{
  parameters_ = std::move(parameters);
  return *this;
}

auto ScriptBuilder::Bytes() const -> std::vector<std::byte>
{
  auto encoded
    = Encode(name_, bytecode_, source_, external_source_, flags_, parameters_);
  detail::CheckWithLoader("script '" + name_ + "'", encoded.bytes,
    encoded.references, content::loaders::LoadScriptAsset);
  return std::move(encoded.bytes);
}

auto ScriptBuilder::References() const -> data::AssetReferences
{
  return Encode(
    name_, bytecode_, source_, external_source_, flags_, parameters_)
    .references;
}

} // namespace oxygen::cooker::testing
