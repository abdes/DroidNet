//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Tools/PakTool/ScriptSealing.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_scripting.h>

namespace oxygen::content::pak::tool {

namespace {

  using data::AssetType;
  using data::pak::core::DataBlobSizeT;
  using data::pak::core::OffsetT;
  using data::pak::core::ResourceIndexT;
  using data::pak::scripting::ScriptAssetDesc;
  using data::pak::scripting::ScriptAssetFlags;
  using data::pak::scripting::ScriptCompression;
  using data::pak::scripting::ScriptEncoding;
  using data::pak::scripting::ScriptLanguage;
  using data::pak::scripting::ScriptResourceDesc;

  struct ScriptTables {
    std::vector<ScriptResourceDesc> entries {};
    std::vector<std::byte> data_blob {};
    bool existed = false;
  };

  struct ScriptAssetPatchContext {
    data::AssetKey key {};
    std::string virtual_path;
    std::string descriptor_relpath;
    std::filesystem::path descriptor_path;
    ScriptAssetDesc descriptor {};
  };

  struct SealedLooseSourceResult {
    std::optional<std::filesystem::path> staged_root;
    uint32_t sealed_asset_count = 0;
  };

  [[nodiscard]] auto MakeError(std::string code, std::string message,
    const std::filesystem::path& source_path = {},
    const std::filesystem::path& descriptor_path = {},
    const std::filesystem::path& resolved_path = {},
    std::string external_source_path = {})
    -> Result<ScriptSealingResult, ScriptSealingError>
  {
    return Result<ScriptSealingResult, ScriptSealingError>::Err(
      ScriptSealingError {
        .error_code = std::move(code),
        .error_message = std::move(message),
        .source_path = source_path,
        .descriptor_path = descriptor_path,
        .resolved_path = resolved_path,
        .external_source_path = std::move(external_source_path),
      });
  }

  [[nodiscard]] auto ReadFileBytes(const std::filesystem::path& path)
    -> std::optional<std::vector<std::byte>>
  {
    auto in = std::ifstream(path, std::ios::binary);
    if (!in.is_open()) {
      return std::nullopt;
    }

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) {
      return std::nullopt;
    }
    in.seekg(0, std::ios::beg);

    auto bytes = std::vector<std::byte>(static_cast<size_t>(size));
    if (!bytes.empty()) {
      in.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
      if (!in) {
        return std::nullopt;
      }
    }
    return bytes;
  }

  auto WriteFileBytes(
    const std::filesystem::path& path, std::span<const std::byte> bytes) -> bool
  {
    std::error_code ec {};
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return false;
    }

    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    if (!bytes.empty()) {
      out.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(out);
  }

  [[nodiscard]] auto HasParentTraversal(const std::filesystem::path& path)
    -> bool
  {
    return std::ranges::any_of(
      path, [](const auto& part) { return part == ".."; });
  }

  [[nodiscard]] auto ComputeDescriptorSha256(const std::filesystem::path& path)
    -> std::optional<base::Sha256Digest>
  {
    const auto bytes = ReadFileBytes(path);
    if (!bytes.has_value()) {
      return std::nullopt;
    }
    return base::ComputeSha256(
      std::span<const std::byte>(bytes->data(), bytes->size()));
  }

  [[nodiscard]] auto ReadLooseCookedIndexHeader(
    const std::filesystem::path& index_path)
    -> std::optional<data::loose_cooked::IndexHeader>
  {
    auto in = std::ifstream(index_path, std::ios::binary);
    if (!in.is_open()) {
      return std::nullopt;
    }

    auto header = data::loose_cooked::IndexHeader {};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in) {
      return std::nullopt;
    }
    return header;
  }

  [[nodiscard]] auto ComputeContentHash64(
    const std::span<const std::byte> bytes) -> uint64_t
  {
    constexpr auto kScriptContentHashBytes = size_t { 8 };
    constexpr auto kByteShiftBits = size_t { 8 };

    const auto digest = base::ComputeSha256(bytes);
    auto hash = uint64_t { 0 };
    for (size_t i = 0; i < kScriptContentHashBytes; ++i) {
      hash |= static_cast<uint64_t>(digest.at(i)) << (i * kByteShiftBits);
    }
    return hash;
  }

  [[nodiscard]] auto TryGetExternalSourcePath(const ScriptAssetDesc& descriptor)
    -> std::optional<std::string_view>
  {
    const auto* const begin = std::begin(descriptor.external_source_path);
    const auto* const end = std::end(descriptor.external_source_path);
    const auto* const nul = std::find(begin, end, '\0');
    if (nul == begin) {
      return std::nullopt;
    }
    if (nul == end) {
      return std::nullopt;
    }
    return std::string_view(begin, static_cast<size_t>(nul - begin));
  }

  [[nodiscard]] auto ResolveExternalSourcePath(
    const std::filesystem::path& cooked_root,
    const std::string_view external_source_path)
    -> std::optional<std::filesystem::path>
  {
    if (external_source_path.empty()) {
      return std::nullopt;
    }

    const auto root_parent = cooked_root.parent_path().lexically_normal();
    if (root_parent.empty()) {
      return std::nullopt;
    }

    const auto stored_path
      = std::filesystem::path(std::string(external_source_path))
          .lexically_normal();
    if (stored_path.empty() || stored_path.is_absolute()
      || HasParentTraversal(stored_path)) {
      return std::nullopt;
    }

    const auto resolved = (root_parent / stored_path).lexically_normal();
    const auto relative = resolved.lexically_relative(root_parent);
    if (relative.empty() || relative.is_absolute()
      || HasParentTraversal(relative)) {
      return std::nullopt;
    }
    return resolved;
  }

  [[nodiscard]] auto MakeStagedLooseRoot(
    const std::filesystem::path& original_root,
    const std::filesystem::path& staging_parent, const size_t source_index)
    -> std::filesystem::path
  {
    static auto counter = std::atomic_uint64_t { 0 };
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);

    auto name = original_root.filename().string();
    if (name.empty()) {
      name = "cooked";
    }
    for (auto& ch : name) {
      if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-'
            || ch == '_' || ch == '.')) {
        ch = '_';
      }
    }

    return staging_parent / "oxygen_paktool_sealed_sources"
      / (name + "-source-" + std::to_string(source_index) + "-case-"
        + std::to_string(id));
  }

  auto CopyCookedRoot(const std::filesystem::path& from,
    const std::filesystem::path& to) -> std::error_code
  {
    auto ec = std::error_code {};
    std::filesystem::remove_all(to, ec);
    ec.clear();
    std::filesystem::create_directories(to.parent_path(), ec);
    if (ec) {
      return ec;
    }
    std::filesystem::copy(
      from, to, std::filesystem::copy_options::recursive, ec);
    return ec;
  }

  [[nodiscard]] auto LoadScriptTables(const std::filesystem::path& cooked_root)
    -> std::optional<ScriptTables>
  {
    const auto layout = import::LooseCookedLayout {};
    const auto table_path
      = cooked_root / std::filesystem::path(layout.ScriptsTableRelPath());
    const auto data_path
      = cooked_root / std::filesystem::path(layout.ScriptsDataRelPath());

    const auto table_exists = std::filesystem::exists(table_path);
    const auto data_exists = std::filesystem::exists(data_path);
    if (table_exists != data_exists) {
      return std::nullopt;
    }

    auto tables = ScriptTables {};
    tables.existed = table_exists;

    if (!table_exists) {
      tables.entries.push_back(ScriptResourceDesc {});
      return tables;
    }

    const auto table_bytes = ReadFileBytes(table_path);
    const auto data_bytes = ReadFileBytes(data_path);
    if (!table_bytes.has_value() || !data_bytes.has_value()) {
      return std::nullopt;
    }
    if ((table_bytes->size() % sizeof(ScriptResourceDesc)) != 0U) {
      return std::nullopt;
    }

    tables.entries.resize(table_bytes->size() / sizeof(ScriptResourceDesc));
    if (!table_bytes->empty()) {
      std::memcpy(
        tables.entries.data(), table_bytes->data(), table_bytes->size());
    }
    if (tables.entries.empty()) {
      tables.entries.push_back(ScriptResourceDesc {});
    }
    tables.data_blob = std::move(*data_bytes);
    return tables;
  }

  auto PersistScriptTables(const std::filesystem::path& cooked_root,
    const ScriptTables& tables) -> bool
  {
    const auto layout = import::LooseCookedLayout {};
    const auto table_path
      = cooked_root / std::filesystem::path(layout.ScriptsTableRelPath());
    const auto data_path
      = cooked_root / std::filesystem::path(layout.ScriptsDataRelPath());

    return WriteFileBytes(table_path,
             std::as_bytes(std::span<const ScriptResourceDesc>(
               tables.entries.data(), tables.entries.size())))
      && WriteFileBytes(data_path,
        std::span<const std::byte>(
          tables.data_blob.data(), tables.data_blob.size()));
  }

  [[nodiscard]] auto ReadScriptAssetContext(
    const std::filesystem::path& cooked_root, const lc::AssetEntry& asset_entry)
    -> std::optional<ScriptAssetPatchContext>
  {
    if (static_cast<AssetType>(asset_entry.asset_type) != AssetType::kScript) {
      return std::nullopt;
    }

    const auto descriptor_path
      = cooked_root / std::filesystem::path(asset_entry.descriptor_relpath);
    const auto descriptor_bytes = ReadFileBytes(descriptor_path);
    if (!descriptor_bytes.has_value()
      || descriptor_bytes->size() != sizeof(ScriptAssetDesc)) {
      return std::nullopt;
    }

    auto descriptor = ScriptAssetDesc {};
    std::memcpy(&descriptor, descriptor_bytes->data(), sizeof(ScriptAssetDesc));
    if (static_cast<AssetType>(descriptor.header.asset_type)
      != AssetType::kScript) {
      return std::nullopt;
    }

    return ScriptAssetPatchContext {
      .key = asset_entry.key,
      .virtual_path = asset_entry.virtual_path,
      .descriptor_relpath = asset_entry.descriptor_relpath,
      .descriptor_path = descriptor_path,
      .descriptor = descriptor,
    };
  }

  [[nodiscard]] auto AppendEmbeddedSourceResource(ScriptTables& tables,
    std::span<const std::byte> source_bytes) -> std::optional<ResourceIndexT>
  {
    if (tables.entries.size()
      >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
      return std::nullopt;
    }
    if (tables.data_blob.size()
      > static_cast<size_t>((std::numeric_limits<OffsetT>::max)())) {
      return std::nullopt;
    }
    if (source_bytes.size()
      > static_cast<size_t>((std::numeric_limits<DataBlobSizeT>::max)())) {
      return std::nullopt;
    }

    const auto hashing_enabled = std::ranges::any_of(tables.entries,
      [](const ScriptResourceDesc& entry) { return entry.content_hash != 0; });
    const auto resource_index
      = ResourceIndexT { static_cast<uint32_t>(tables.entries.size()) };

    auto resource_desc = ScriptResourceDesc {};
    resource_desc.data_offset = static_cast<OffsetT>(tables.data_blob.size());
    resource_desc.size_bytes = static_cast<DataBlobSizeT>(source_bytes.size());
    resource_desc.language = ScriptLanguage::kLuau;
    resource_desc.encoding = ScriptEncoding::kSource;
    resource_desc.compression = ScriptCompression::kNone;
    resource_desc.content_hash
      = hashing_enabled ? ComputeContentHash64(source_bytes) : 0;

    tables.entries.push_back(resource_desc);
    tables.data_blob.insert(
      tables.data_blob.end(), source_bytes.begin(), source_bytes.end());
    return resource_index;
  }

  auto ClearExternalSourceContract(ScriptAssetDesc& descriptor) -> void
  {
    descriptor.flags
      = static_cast<ScriptAssetFlags>(static_cast<uint32_t>(descriptor.flags)
        & ~static_cast<uint32_t>(ScriptAssetFlags::kAllowExternalSource));
    std::ranges::fill(descriptor.external_source_path, '\0');
  }

  [[nodiscard]] auto RewriteStagedScriptDescriptor(
    const std::filesystem::path& original_root,
    const std::filesystem::path& staged_root, const lc::AssetEntry& asset_entry,
    ScriptTables& tables) -> Result<bool, ScriptSealingError>
  {
    const auto context = ReadScriptAssetContext(staged_root, asset_entry);
    if (!context.has_value()) {
      return Result<bool, ScriptSealingError>::Err(ScriptSealingError {
        .error_code = "paktool.script_seal.script_descriptor_invalid",
        .error_message = "Failed to parse staged script descriptor.",
        .source_path = original_root,
        .descriptor_path
        = staged_root / std::filesystem::path(asset_entry.descriptor_relpath),
      });
    }

    auto descriptor = context->descriptor;
    const auto allows_external
      = (static_cast<uint32_t>(descriptor.flags)
          & static_cast<uint32_t>(ScriptAssetFlags::kAllowExternalSource))
      != 0U;
    if (!allows_external) {
      return Result<bool, ScriptSealingError>::Ok(false);
    }

    if (descriptor.source_resource_index == data::pak::core::kNoResourceIndex) {
      const auto external_source = TryGetExternalSourcePath(descriptor);
      if (!external_source.has_value()) {
        return Result<bool, ScriptSealingError>::Err(ScriptSealingError {
          .error_code = "paktool.script_seal.external_source_missing",
          .error_message
          = "Script descriptor requires external source sealing but does not "
            "carry a valid external source path.",
          .source_path = original_root,
          .descriptor_path = context->descriptor_path,
        });
      }

      const auto resolved_path
        = ResolveExternalSourcePath(original_root, *external_source);
      if (!resolved_path.has_value()) {
        return Result<bool, ScriptSealingError>::Err(ScriptSealingError {
          .error_code = "paktool.script_seal.external_source_unresolvable",
          .error_message
          = "External script source path could not be resolved relative to "
            "the loose-cooked root parent.",
          .source_path = original_root,
          .descriptor_path = context->descriptor_path,
          .external_source_path = std::string(*external_source),
        });
      }

      const auto source_bytes = ReadFileBytes(*resolved_path);
      if (!source_bytes.has_value()) {
        return Result<bool, ScriptSealingError>::Err(ScriptSealingError {
          .error_code = "paktool.script_seal.external_source_read_failed",
          .error_message = "Failed to read external script source bytes.",
          .source_path = original_root,
          .descriptor_path = context->descriptor_path,
          .resolved_path = *resolved_path,
          .external_source_path = std::string(*external_source),
        });
      }

      const auto source_index = AppendEmbeddedSourceResource(tables,
        std::span<const std::byte>(source_bytes->data(), source_bytes->size()));
      if (!source_index.has_value()) {
        return Result<bool, ScriptSealingError>::Err(ScriptSealingError {
          .error_code = "paktool.script_seal.script_table_append_failed",
          .error_message
          = "Failed to append sealed script source to staged scripts.table.",
          .source_path = original_root,
          .descriptor_path = context->descriptor_path,
          .resolved_path = *resolved_path,
          .external_source_path = std::string(*external_source),
        });
      }
      descriptor.source_resource_index = *source_index;
    }

    ClearExternalSourceContract(descriptor);
    if (!WriteFileBytes(context->descriptor_path,
          std::as_bytes(std::span { &descriptor, 1 }))) {
      return Result<bool, ScriptSealingError>::Err(ScriptSealingError {
        .error_code = "paktool.script_seal.script_descriptor_write_failed",
        .error_message = "Failed to persist sealed script descriptor bytes.",
        .source_path = original_root,
        .descriptor_path = context->descriptor_path,
      });
    }

    return Result<bool, ScriptSealingError>::Ok(true);
  }

  auto RefreshStagedLooseCookedIndex(const std::filesystem::path& staged_root,
    const lc::Inspection& inspection) -> Result<void, ScriptSealingError>
  {
    const auto index_path = staged_root / "container.index.bin";
    const auto index_header = ReadLooseCookedIndexHeader(index_path);
    if (!index_header.has_value()) {
      return Result<void, ScriptSealingError>::Err(ScriptSealingError {
        .error_code = "paktool.script_seal.index_header_read_failed",
        .error_message
        = "Failed to read the staged loose-cooked index header before "
          "refreshing metadata.",
        .source_path = staged_root,
        .resolved_path = index_path,
      });
    }

    auto source_identity_bytes
      = std::array<std::uint8_t, data::SourceKey::kSizeBytes> {};
    std::copy_n(index_header->source_identity.begin(),
      source_identity_bytes.size(), source_identity_bytes.begin());
    const auto source_key = data::SourceKey::FromBytes(source_identity_bytes);
    if (!source_key.has_value()) {
      return Result<void, ScriptSealingError>::Err(ScriptSealingError {
        .error_code = "paktool.script_seal.index_source_key_invalid",
        .error_message
        = "The staged loose-cooked index carries an invalid UUIDv7 source "
          "identity.",
        .source_path = staged_root,
        .resolved_path = index_path,
      });
    }

    auto remove_ec = std::error_code {};
    std::filesystem::remove(index_path, remove_ec);
    if (remove_ec) {
      return Result<void, ScriptSealingError>::Err(ScriptSealingError {
        .error_code = "paktool.script_seal.index_remove_failed",
        .error_message = remove_ec.message(),
        .source_path = staged_root,
        .resolved_path = index_path,
      });
    }

    auto writer = import::LooseCookedWriter(staged_root);
    writer.SetSourceKey(source_key.value());
    writer.SetContentVersion(index_header->content_version);
    auto seen_file_kinds = std::unordered_set<data::loose_cooked::FileKind> {};

    for (const auto& asset : inspection.Assets()) {
      const auto digest = ComputeDescriptorSha256(
        staged_root / std::filesystem::path(asset.descriptor_relpath));
      if (!digest.has_value()) {
        return Result<void, ScriptSealingError>::Err(ScriptSealingError {
          .error_code = "paktool.script_seal.index_descriptor_hash_failed",
          .error_message
          = "Failed to compute descriptor digest while refreshing the staged "
            "loose-cooked index.",
          .source_path = staged_root,
          .descriptor_path
          = staged_root / std::filesystem::path(asset.descriptor_relpath),
        });
      }

      writer.RegisterExternalAssetDescriptor(asset.key,
        static_cast<AssetType>(asset.asset_type), asset.virtual_path,
        asset.descriptor_relpath, 0, *digest);
    }

    for (const auto& file : inspection.Files()) {
      const auto file_path = staged_root / std::filesystem::path(file.relpath);
      if (!std::filesystem::exists(file_path)) {
        continue;
      }
      writer.RegisterExternalFile(file.kind, file.relpath);
      seen_file_kinds.insert(file.kind);
    }

    const auto layout = import::LooseCookedLayout {};
    const auto scripts_table_relpath = layout.ScriptsTableRelPath();
    const auto scripts_data_relpath = layout.ScriptsDataRelPath();
    if (!seen_file_kinds.contains(data::loose_cooked::FileKind::kScriptsTable)
      && std::filesystem::exists(
        staged_root / std::filesystem::path(scripts_table_relpath))) {
      writer.RegisterExternalFile(
        data::loose_cooked::FileKind::kScriptsTable, scripts_table_relpath);
    }
    if (!seen_file_kinds.contains(data::loose_cooked::FileKind::kScriptsData)
      && std::filesystem::exists(
        staged_root / std::filesystem::path(scripts_data_relpath))) {
      writer.RegisterExternalFile(
        data::loose_cooked::FileKind::kScriptsData, scripts_data_relpath);
    }

    try {
      static_cast<void>(writer.Finish());
    } catch (const std::exception& ex) {
      return Result<void, ScriptSealingError>::Err(ScriptSealingError {
        .error_code = "paktool.script_seal.index_refresh_failed",
        .error_message = ex.what(),
        .source_path = staged_root,
      });
    }

    return Result<void, ScriptSealingError>::Ok();
  }

  [[nodiscard]] auto SealLooseCookedSource(const data::CookedSource& source,
    const std::filesystem::path& staging_parent, const size_t source_index)
    -> Result<SealedLooseSourceResult, ScriptSealingError>
  {
    auto inspection = lc::Inspection {};
    try {
      inspection.LoadFromRoot(source.path);
    } catch (const std::exception& ex) {
      return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.loose_source_load_failed",
          .error_message = ex.what(),
          .source_path = source.path,
        });
    }

    auto needs_sealing = false;
    for (const auto& asset : inspection.Assets()) {
      if (static_cast<AssetType>(asset.asset_type) != AssetType::kScript) {
        continue;
      }
      const auto context = ReadScriptAssetContext(source.path, asset);
      if (!context.has_value()) {
        return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
          ScriptSealingError {
            .error_code = "paktool.script_seal.script_descriptor_invalid",
            .error_message = "Failed to parse loose-cooked script descriptor.",
            .source_path = source.path,
            .descriptor_path
            = source.path / std::filesystem::path(asset.descriptor_relpath),
          });
      }

      const auto allows_external
        = (static_cast<uint32_t>(context->descriptor.flags)
            & static_cast<uint32_t>(ScriptAssetFlags::kAllowExternalSource))
        != 0U;
      if (allows_external) {
        needs_sealing = true;
        break;
      }
    }

    if (!needs_sealing) {
      return Result<SealedLooseSourceResult, ScriptSealingError>::Ok(
        SealedLooseSourceResult {});
    }

    const auto staged_root
      = MakeStagedLooseRoot(source.path, staging_parent, source_index);
    if (const auto ec = CopyCookedRoot(source.path, staged_root); ec) {
      return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.staging_copy_failed",
          .error_message = ec.message(),
          .source_path = source.path,
          .resolved_path = staged_root,
        });
    }

    const auto cleanup_staged_root = [&staged_root]() {
      CleanupStagedLooseRoots(
        std::span<const std::filesystem::path>(&staged_root, size_t { 1 }));
    };

    auto tables = LoadScriptTables(staged_root);
    if (!tables.has_value()) {
      cleanup_staged_root();
      return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.script_tables_invalid",
          .error_message
          = "Staged scripts.table and scripts.data are missing, mismatched, or "
            "invalid.",
          .source_path = source.path,
          .resolved_path = staged_root,
        });
    }

    auto sealed_count = uint32_t { 0 };
    for (const auto& asset : inspection.Assets()) {
      if (static_cast<AssetType>(asset.asset_type) != AssetType::kScript) {
        continue;
      }
      const auto sealed = RewriteStagedScriptDescriptor(
        source.path, staged_root, asset, *tables);
      if (!sealed.has_value()) {
        cleanup_staged_root();
        return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
          sealed.error());
      }
      if (*sealed) {
        ++sealed_count;
      }
    }

    if (sealed_count == 0U) {
      cleanup_staged_root();
      return Result<SealedLooseSourceResult, ScriptSealingError>::Ok(
        SealedLooseSourceResult {});
    }

    if (!PersistScriptTables(staged_root, *tables)) {
      cleanup_staged_root();
      return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.script_tables_write_failed",
          .error_message
          = "Failed to persist staged scripts.table/scripts.data.",
          .source_path = source.path,
          .resolved_path = staged_root,
        });
    }

    if (const auto refreshed
      = RefreshStagedLooseCookedIndex(staged_root, inspection);
      !refreshed) {
      cleanup_staged_root();
      return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
        refreshed.error());
    }

    LOG_F(INFO,
      "PakTool sealed loose-cooked scripts: source='{}' staged='{}' count={}",
      source.path.string(), staged_root.string(), sealed_count);
    return Result<SealedLooseSourceResult, ScriptSealingError>::Ok(
      SealedLooseSourceResult {
        .staged_root = staged_root,
        .sealed_asset_count = sealed_count,
      });
  }

} // namespace

auto SealLooseCookedSourcesForPakBuild(
  const pak::PakBuildRequest& build_request,
  const std::filesystem::path& staging_parent)
  -> Result<ScriptSealingResult, ScriptSealingError>
{
  auto sealed_request = build_request;
  auto staged_loose_roots = std::vector<std::filesystem::path> {};
  auto sealed_script_assets = uint32_t { 0 };

  for (size_t i = 0; i < sealed_request.sources.size(); ++i) {
    const auto& source = sealed_request.sources[i];
    if (source.kind != data::CookedSourceKind::kLooseCooked) {
      continue;
    }

    const auto sealed_root = SealLooseCookedSource(source, staging_parent, i);
    if (!sealed_root.has_value()) {
      CleanupStagedLooseRoots(
        std::span<const std::filesystem::path>(staged_loose_roots));
      return MakeError(sealed_root.error().error_code,
        sealed_root.error().error_message, sealed_root.error().source_path,
        sealed_root.error().descriptor_path, sealed_root.error().resolved_path,
        sealed_root.error().external_source_path);
    }

    if (sealed_root->staged_root.has_value()) {
      sealed_request.sources[i].path = *sealed_root->staged_root;
      staged_loose_roots.push_back(*sealed_root->staged_root);
      sealed_script_assets += sealed_root->sealed_asset_count;
    }
  }

  return Result<ScriptSealingResult, ScriptSealingError>::Ok(
    ScriptSealingResult {
      .build_request = std::move(sealed_request),
      .staged_loose_roots = std::move(staged_loose_roots),
      .sealed_script_assets = sealed_script_assets,
    });
}

auto CleanupStagedLooseRoots(
  std::span<const std::filesystem::path> staged_loose_roots) -> void
{
  for (const auto& staged_root : staged_loose_roots) {
    if (staged_root.empty()) {
      continue;
    }

    auto ec = std::error_code {};
    std::filesystem::remove_all(staged_root, ec);
    if (ec) {
      LOG_F(WARNING, "PakTool failed to clean staged loose root '{}' [{}]",
        staged_root.string(), ec.message());
    } else {
      LOG_F(
        INFO, "PakTool cleaned staged loose root '{}'", staged_root.string());
    }
  }
}

} // namespace oxygen::content::pak::tool
