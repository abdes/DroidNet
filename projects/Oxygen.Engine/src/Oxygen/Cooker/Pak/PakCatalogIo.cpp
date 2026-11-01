//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Pak/Internal/PakCatalogJson.h>
#include <Oxygen/Cooker/Pak/Internal/PakCatalog_schema.h>
#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::pak {

namespace {

  using nlohmann::json;
  using nlohmann::ordered_json;
  using nlohmann::json_schema::error_handler;
  using nlohmann::json_schema::json_validator;

  constexpr auto kSchemaVersion = data::pak::version::kPakCatalogVersion;

  class CollectingErrorHandler final : public error_handler {
  public:
    void error(const json::json_pointer& ptr, const json& instance,
      const std::string& message) override
    {
      auto out = std::ostringstream {};
      const auto path = ptr.to_string();
      out << (path.empty() ? "<root>" : path) << ": " << message;
      if (!instance.is_discarded() && instance.is_primitive()) {
        out << " (value=" << instance.dump() << ")";
      }
      errors_.push_back(out.str());
    }

    [[nodiscard]] auto HasErrors() const noexcept -> bool
    {
      return !errors_.empty();
    }

    [[nodiscard]] auto ToString() const -> std::string
    {
      auto out = std::ostringstream {};
      for (const auto& error : errors_) {
        out << "- " << error << "\n";
      }
      return out.str();
    }

  private:
    std::vector<std::string> errors_;
  };

  class SchemaValidator final {
  public:
    SchemaValidator()
    {
      validator_.set_root_schema(json::parse(kPakCatalogSchema));
    }

    [[nodiscard]] auto Validate(const json& instance) const
      -> std::optional<std::string>
    {
      try {
        auto handler = CollectingErrorHandler {};
        [[maybe_unused]] auto _ = validator_.validate(instance, handler);
        if (handler.HasErrors()) {
          return handler.ToString();
        }
        return std::nullopt;
      } catch (const std::exception& ex) {
        return std::string(ex.what());
      }
    }

    [[nodiscard]] static auto Instance() -> const SchemaValidator&
    {
      static const auto instance = SchemaValidator {};
      return instance;
    }

  private:
    mutable json_validator validator_;
  };

  auto ParseAssetType(const std::string_view text) -> Result<data::AssetType>
  {
    using data::AssetType;

    if (text == "Material") {
      return Result<AssetType>::Ok(AssetType::kMaterial);
    }
    if (text == "Geometry") {
      return Result<AssetType>::Ok(AssetType::kGeometry);
    }
    if (text == "Scene") {
      return Result<AssetType>::Ok(AssetType::kScene);
    }
    if (text == "Script") {
      return Result<AssetType>::Ok(AssetType::kScript);
    }
    if (text == "InputAction") {
      return Result<AssetType>::Ok(AssetType::kInputAction);
    }
    if (text == "InputMappingContext") {
      return Result<AssetType>::Ok(AssetType::kInputMappingContext);
    }
    if (text == "PhysicsMaterial") {
      return Result<AssetType>::Ok(AssetType::kPhysicsMaterial);
    }
    if (text == "CollisionShape") {
      return Result<AssetType>::Ok(AssetType::kCollisionShape);
    }
    if (text == "PhysicsScene") {
      return Result<AssetType>::Ok(AssetType::kPhysicsScene);
    }

    return Result<AssetType>::Err(std::errc::invalid_argument);
  }

  auto ToJsonBases(std::span<const data::PakCatalogBase> bases) -> ordered_json
  {
    auto items = ordered_json::array();
    for (const auto& layer : bases) {
      auto item = ordered_json::object();
      item["source_key"] = data::to_string(layer.source_key);
      item["content_version"] = layer.content_version;
      item["catalog_digest"] = base::ToHexString(layer.catalog_digest);
      items.push_back(std::move(item));
    }
    return items;
  }

  auto ParseJsonBases(const ordered_json& items)
    -> std::optional<std::vector<data::PakCatalogBase>>
  {
    auto bases = std::vector<data::PakCatalogBase> {};
    for (const auto& item : items) {
      const auto key = data::SourceKey::FromString(
        item.at("source_key").get_ref<const std::string&>());
      const auto digest = base::Sha256DigestFromHex(
        item.at("catalog_digest").get_ref<const std::string&>());
      if (!key || !digest) {
        return std::nullopt;
      }
      bases.push_back({
        .source_key = *key,
        .content_version = item.at("content_version").get<uint16_t>(),
        .catalog_digest = *digest,
      });
    }
    return bases;
  }

  auto ParseJsonDependencies(const ordered_json& items)
    -> std::optional<std::vector<data::PakCatalogDependency>>
  {
    auto dependencies = std::vector<data::PakCatalogDependency> {};
    for (const auto& item : items) {
      auto dependency = data::PakCatalogDependency {
        .mount = item.at("mount").get<std::string>(),
        .requirements = {},
      };
      for (const auto& entry : item.at("requirements")) {
        const auto key = data::AssetKey::FromString(
          entry.at("key").get_ref<const std::string&>());
        if (!key) {
          return std::nullopt;
        }
        if (entry.at("kind").get_ref<const std::string&>() == "texture") {
          dependency.requirements.push_back({
            .kind = data::KeyReferenceKind::kTexture,
            .key = *key,
            .type = data::AssetType::kUnknown,
          });
          continue;
        }
        const auto type
          = ParseAssetType(entry.at("type").get_ref<const std::string&>());
        if (!type) {
          return std::nullopt;
        }
        dependency.requirements.push_back({
          .kind = data::KeyReferenceKind::kAsset,
          .key = *key,
          .type = *type,
        });
      }
      dependencies.push_back(std::move(dependency));
    }
    return dependencies;
  }

  auto BuildCanonicalDocument(const data::PakCatalog& catalog) -> ordered_json
  {
    auto entries = std::vector<data::PakCatalogEntry>(catalog.entries);
    std::ranges::sort(entries,
      [](const data::PakCatalogEntry& lhs, const data::PakCatalogEntry& rhs)
        -> bool { return lhs.asset_key < rhs.asset_key; });

    auto root = ordered_json::object();
    root["schema_version"] = kSchemaVersion;
    root["source_key"] = data::to_string(catalog.source_key);
    root["content_version"] = catalog.content_version;
    root["catalog_digest"] = base::ToHexString(catalog.catalog_digest);
    root["mount"] = catalog.mount;

    auto json_entries = ordered_json::array();
    for (const auto& entry : entries) {
      auto json_entry = ordered_json::object();
      json_entry["asset_key"] = data::to_string(entry.asset_key);
      json_entry["asset_type"] = std::string(data::to_string(entry.asset_type));
      json_entry["descriptor_digest"]
        = base::ToHexString(entry.descriptor_digest);
      json_entry["transitive_resource_digest"]
        = base::ToHexString(entry.transitive_resource_digest);
      json_entries.push_back(std::move(json_entry));
    }
    root["entries"] = std::move(json_entries);
    auto deleted = catalog.deleted;
    std::ranges::sort(deleted);
    root["deleted"] = ordered_json::array();
    for (const auto& key : deleted) {
      root["deleted"].push_back(data::to_string(key));
    }
    root["bases"] = ToJsonBases(catalog.bases);
    root["dependencies"] = internal::ToJsonDependencies(catalog.dependencies);

    return root;
  }

  auto ParseCatalogDocument(const ordered_json& document)
    -> Result<data::PakCatalog>
  {
    const auto schema_error = SchemaValidator::Instance().Validate(document);
    if (schema_error.has_value()) {
      return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
    }

    const auto source_key = data::SourceKey::FromString(
      document.at("source_key").get_ref<const std::string&>());
    if (!source_key.has_value()) {
      return Result<data::PakCatalog>::Err(source_key.error());
    }

    const auto catalog_digest = base::Sha256DigestFromHex(
      document.at("catalog_digest").get_ref<const std::string&>());
    if (!catalog_digest.has_value()) {
      return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
    }

    auto entries = std::vector<data::PakCatalogEntry> {};
    entries.reserve(document.at("entries").size());
    auto seen_asset_keys = std::unordered_set<data::AssetKey> {};
    seen_asset_keys.reserve(document.at("entries").size());

    for (const auto& item : document.at("entries")) {
      const auto asset_key = data::AssetKey::FromString(
        item.at("asset_key").get_ref<const std::string&>());
      if (!asset_key.has_value()) {
        return Result<data::PakCatalog>::Err(asset_key.error());
      }

      if (!seen_asset_keys.insert(asset_key.value()).second) {
        return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
      }

      const auto asset_type
        = ParseAssetType(item.at("asset_type").get_ref<const std::string&>());
      if (!asset_type.has_value()) {
        return Result<data::PakCatalog>::Err(asset_type.error());
      }

      const auto descriptor_digest = base::Sha256DigestFromHex(
        item.at("descriptor_digest").get_ref<const std::string&>());
      if (!descriptor_digest.has_value()) {
        return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
      }

      const auto transitive_digest = base::Sha256DigestFromHex(
        item.at("transitive_resource_digest").get_ref<const std::string&>());
      if (!transitive_digest.has_value()) {
        return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
      }

      entries.push_back(data::PakCatalogEntry {
        .asset_key = asset_key.value(),
        .asset_type = asset_type.value(),
        .descriptor_digest = descriptor_digest.value(),
        .transitive_resource_digest = transitive_digest.value(),
      });
    }

    std::ranges::sort(entries,
      [](const data::PakCatalogEntry& lhs, const data::PakCatalogEntry& rhs)
        -> bool { return lhs.asset_key < rhs.asset_key; });

    auto catalog = data::PakCatalog {
      .source_key = source_key.value(),
      .content_version = document.at("content_version").get<uint16_t>(),
      .catalog_digest = catalog_digest.value(),
      .mount = document.at("mount").get<std::string>(),
      .entries = std::move(entries),
      .deleted = {},
      .bases = {},
      .dependencies = {},
    };
    for (const auto& item : document.at("deleted")) {
      const auto key
        = data::AssetKey::FromString(item.get_ref<const std::string&>());
      if (!key) {
        return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
      }
      catalog.deleted.push_back(*key);
    }
    std::ranges::sort(catalog.deleted);
    auto bases = ParseJsonBases(document.at("bases"));
    auto dependencies = ParseJsonDependencies(document.at("dependencies"));
    if (!bases || !dependencies) {
      return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
    }
    catalog.bases = std::move(*bases);
    catalog.dependencies = std::move(*dependencies);
    if (!catalog.Validate()) {
      return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
    }
    return Result<data::PakCatalog>::Ok(std::move(catalog));
  }

} // namespace

namespace internal {

  auto ToJsonDependencies(
    std::span<const data::PakCatalogDependency> dependencies) -> ordered_json
  {
    auto sorted = std::vector<data::PakCatalogDependency>(
      dependencies.begin(), dependencies.end());
    std::ranges::sort(sorted, {}, &data::PakCatalogDependency::mount);
    auto items = ordered_json::array();
    for (auto& dependency : sorted) {
      std::ranges::sort(dependency.requirements, {},
        [](const data::PakRequirement& requirement) -> auto {
          return std::pair { requirement.kind, requirement.key };
        });
      auto requirements = ordered_json::array();
      for (const auto& requirement : dependency.requirements) {
        auto item = ordered_json::object();
        if (requirement.kind == data::KeyReferenceKind::kTexture) {
          item["kind"] = "texture";
          item["key"] = data::to_string(requirement.key);
        } else {
          item["kind"] = "asset";
          item["key"] = data::to_string(requirement.key);
          item["type"] = std::string(data::to_string(requirement.type));
        }
        requirements.push_back(std::move(item));
      }
      items.push_back(ordered_json {
        { "mount", dependency.mount },
        { "requirements", std::move(requirements) },
      });
    }
    return items;
  }

} // namespace internal

auto PakCatalogIo::ToCanonicalJsonString(const data::PakCatalog& catalog)
  -> std::string
{
  auto document = BuildCanonicalDocument(catalog);
  auto text = document.dump(2);
  text.push_back('\n');
  return text;
}

auto PakCatalogIo::Parse(const std::string_view text)
  -> Result<data::PakCatalog>
{
  try {
    const auto document = ordered_json::parse(std::string(text));
    return ParseCatalogDocument(document);
  } catch (const std::exception&) {
    return Result<data::PakCatalog>::Err(std::errc::invalid_argument);
  }
}

auto PakCatalogIo::Read(const std::filesystem::path& path,
  const cooked::CookerStorageReader& storage) -> Result<data::PakCatalog>
{
  const auto bytes
    = cooked::ReadWholeFile(storage, path, cooked::kMaxMetadataBytes);
  if (!bytes) {
    return Result<data::PakCatalog>::Err(bytes.error());
  }
  auto text = std::string(bytes->size(), '\0');
  std::ranges::transform(*bytes, text.begin(),
    [](const std::byte b) -> char { return static_cast<char>(b); });
  return Parse(text);
}

auto PakCatalogIo::Write(const std::filesystem::path& path,
  const data::PakCatalog& catalog, cooked::CookerStorage& storage)
  -> Result<void>
{
  if (!catalog.Validate()) {
    return Result<void>::Err(std::errc::invalid_argument);
  }
  const auto text = ToCanonicalJsonString(catalog);
  return cooked::PublishWholeFile(
    storage, path, std::as_bytes(std::span(text)));
}

} // namespace oxygen::content::pak
