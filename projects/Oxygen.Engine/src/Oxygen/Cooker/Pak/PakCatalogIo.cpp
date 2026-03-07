//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Pak/Internal/PakCatalog_schema.h>
#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::pak {

namespace {

  using nlohmann::json;
  using nlohmann::ordered_json;
  using nlohmann::json_schema::error_handler;
  using nlohmann::json_schema::json_validator;

  constexpr auto kSchemaVersion = 1;
  constexpr auto kHexDigits = std::string_view { "0123456789abcdef" };

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
    std::vector<std::string> errors_ {};
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
    mutable json_validator validator_ {};
  };

  template <size_t N>
  auto ToHex(const std::array<uint8_t, N>& bytes) -> std::string
  {
    auto out = std::string {};
    out.reserve(N * 2U);
    for (const auto byte : bytes) {
      out.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
      out.push_back(kHexDigits[byte & 0x0FU]);
    }
    return out;
  }

  auto ParseHexNibble(const char c) noexcept -> uint8_t
  {
    constexpr auto kInvalidNibble = uint8_t { 0xFFU };
    if (c >= '0' && c <= '9') {
      return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<uint8_t>(c - 'a' + 10U);
    }
    return kInvalidNibble;
  }

  template <size_t N>
  auto ParseHexBytes(const std::string_view text)
    -> Result<std::array<uint8_t, N>>
  {
    constexpr auto kInvalidNibble = uint8_t { 0xFFU };
    if (text.size() != (N * 2U)) {
      return Result<std::array<uint8_t, N>>::Err(std::errc::invalid_argument);
    }

    auto bytes = std::array<uint8_t, N> {};
    for (size_t i = 0; i < N; ++i) {
      const auto high = ParseHexNibble(text[(i * 2U)]);
      const auto low = ParseHexNibble(text[(i * 2U) + 1U]);
      if (high == kInvalidNibble || low == kInvalidNibble) {
        return Result<std::array<uint8_t, N>>::Err(std::errc::invalid_argument);
      }
      bytes[i] = static_cast<uint8_t>((high << 4U) | low);
    }

    return Result<std::array<uint8_t, N>>::Ok(bytes);
  }

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

  auto BuildCanonicalDocument(const data::PakCatalog& catalog) -> ordered_json
  {
    auto entries = std::vector<data::PakCatalogEntry>(catalog.entries);
    std::ranges::sort(entries,
      [](const data::PakCatalogEntry& lhs, const data::PakCatalogEntry& rhs) {
        return lhs.asset_key < rhs.asset_key;
      });

    auto root = ordered_json::object();
    root["schema_version"] = kSchemaVersion;
    root["source_key"] = data::to_string(catalog.source_key);
    root["content_version"] = catalog.content_version;
    root["catalog_digest"] = ToHex(catalog.catalog_digest);

    auto json_entries = ordered_json::array();
    for (const auto& entry : entries) {
      auto json_entry = ordered_json::object();
      json_entry["asset_key"] = data::to_string(entry.asset_key);
      json_entry["asset_type"] = std::string(data::to_string(entry.asset_type));
      json_entry["descriptor_digest"] = ToHex(entry.descriptor_digest);
      json_entry["transitive_resource_digest"]
        = ToHex(entry.transitive_resource_digest);
      json_entries.push_back(std::move(json_entry));
    }
    root["entries"] = std::move(json_entries);

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

    const auto catalog_digest = ParseHexBytes<32>(
      document.at("catalog_digest").get_ref<const std::string&>());
    if (!catalog_digest.has_value()) {
      return Result<data::PakCatalog>::Err(catalog_digest.error());
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

      const auto descriptor_digest = ParseHexBytes<32>(
        item.at("descriptor_digest").get_ref<const std::string&>());
      if (!descriptor_digest.has_value()) {
        return Result<data::PakCatalog>::Err(descriptor_digest.error());
      }

      const auto transitive_digest = ParseHexBytes<32>(
        item.at("transitive_resource_digest").get_ref<const std::string&>());
      if (!transitive_digest.has_value()) {
        return Result<data::PakCatalog>::Err(transitive_digest.error());
      }

      entries.push_back(data::PakCatalogEntry {
        .asset_key = asset_key.value(),
        .asset_type = asset_type.value(),
        .descriptor_digest = descriptor_digest.value(),
        .transitive_resource_digest = transitive_digest.value(),
      });
    }

    std::ranges::sort(entries,
      [](const data::PakCatalogEntry& lhs, const data::PakCatalogEntry& rhs) {
        return lhs.asset_key < rhs.asset_key;
      });

    return Result<data::PakCatalog>::Ok(data::PakCatalog {
      .source_key = source_key.value(),
      .content_version = document.at("content_version").get<uint16_t>(),
      .catalog_digest = catalog_digest.value(),
      .entries = std::move(entries),
    });
  }

} // namespace

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

auto PakCatalogIo::Read(const std::filesystem::path& path)
  -> Result<data::PakCatalog>
{
  auto input = std::ifstream(path);
  if (!input) {
    return Result<data::PakCatalog>::Err(std::errc::no_such_file_or_directory);
  }

  auto buffer = std::ostringstream {};
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return Result<data::PakCatalog>::Err(std::errc::io_error);
  }

  return Parse(buffer.str());
}

auto PakCatalogIo::Write(const std::filesystem::path& path,
  const data::PakCatalog& catalog) -> Result<void>
{
  auto output = std::ofstream(path, std::ios::out | std::ios::trunc);
  if (!output) {
    return Result<void>::Err(std::errc::io_error);
  }

  output << ToCanonicalJsonString(catalog);
  if (!output) {
    return Result<void>::Err(std::errc::io_error);
  }

  return Result<void>::Ok();
}

} // namespace oxygen::content::pak
