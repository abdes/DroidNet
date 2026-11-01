//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "RequestSnapshot.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/PakTool/ScriptSealing.h>
#include <Oxygen/Cooker/Tools/PakTool/StagedSourceStorage.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::pak::tool {

namespace {

  namespace loose_layout = import::internal::loose_layout;

  constexpr auto kSealedSourcesDirName = "oxygen_paktool_sealed_sources";

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
    std::vector<ScriptResourceDesc> entries;
    std::vector<std::byte> data_blob;
    bool existed = false;
  };

  struct ScriptAssetPatchContext {
    data::AssetKey key;
    std::string virtual_path;
    std::string descriptor_relpath;
    std::filesystem::path descriptor_path;
    ScriptAssetDesc descriptor {};
    //! Whole descriptor, including the parameter declarations after it.
    std::vector<std::byte> descriptor_bytes;
    data::DescriptorStrings strings;
  };

  struct SealedLooseSourceResult {
    std::optional<std::filesystem::path> staged_root;
    uint32_t sealed_asset_count = 0;
  };

  //! Where sealing reads and publishes, and every file it published, so that
  //! cleanup removes exactly those.
  struct SealingStorage {
    observer_ptr<StagedSourceStorage> storage;
    observer_ptr<std::vector<std::filesystem::path>> published;

    [[nodiscard]] auto Read(
      const std::filesystem::path& path, const serio::ByteCount limit) const
      -> std::optional<std::vector<std::byte>>
    {
      auto bytes = cooked::ReadWholeFile(*storage, path, limit);
      if (!bytes) {
        return std::nullopt;
      }
      return std::move(bytes).value();
    }

    [[nodiscard]] auto Write(const std::filesystem::path& path,
      const std::span<const std::byte> bytes) const -> bool
    {
      if (!cooked::PublishWholeFile(*storage, path, bytes)) {
        return false;
      }
      if (std::ranges::find(*published, path) == published->end()) {
        published->push_back(path);
      }
      return true;
    }
  };

  //! A sealed script descriptor: its new references and what was written.
  struct SealedDescriptor {
    data::AssetReferences references;
    import::LooseCookedFileDigest written;
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

  [[nodiscard]] auto HasParentTraversal(const std::filesystem::path& path)
    -> bool
  {
    return std::ranges::any_of(
      path, [](const auto& part) -> bool { return part == ".."; });
  }

  [[nodiscard]] auto TryGetExternalSourcePath(const ScriptAssetDesc& descriptor,
    const data::DescriptorStrings& strings) -> std::optional<std::string_view>
  {
    const auto path = strings.TryGet(descriptor.external_source_path_offset);
    if (!path || path->empty()) {
      return std::nullopt;
    }
    return path;
  }

  //! Why an external source path does not resolve.
  struct UnresolvedSource {
    std::string_view code;
    std::string_view message;
  };

  //! The file an external source's mount path, `/<Mount>/<path in mount>`,
  //! names in its source mount's folder.
  [[nodiscard]] auto ResolveExternalSourcePath(
    const cooked::CookerStorageReader& storage,
    const SourceMountFolders& source_mounts,
    const std::string_view external_source_path)
    -> std::expected<std::filesystem::path, UnresolvedSource>
  {
    constexpr auto kNotMountPath = UnresolvedSource {
      .code = "paktool.script_seal.external_source_not_mount_path",
      .message = "External script source is not a mount path, "
                 "/<Mount>/<path in mount>, that stays in its mount.",
    };
    const auto slash = external_source_path.find('/', 1);
    if (!external_source_path.starts_with('/')
      || slash == std::string_view::npos || slash == 1
      || slash + 1 == external_source_path.size()) {
      return std::unexpected(kNotMountPath);
    }
    const auto in_mount
      = std::filesystem::path(external_source_path.substr(slash + 1))
          .lexically_normal();
    if (in_mount.empty() || in_mount.has_root_path()
      || HasParentTraversal(in_mount)) {
      return std::unexpected(kNotMountPath);
    }
    const auto mount
      = source_mounts.find(external_source_path.substr(1, slash - 1));
    if (mount == source_mounts.end()) {
      return std::unexpected(UnresolvedSource {
        .code = "paktool.script_seal.external_source_mount_unbound",
        .message = "External script source names a source mount that no "
                   "--mount, --mounts or .mounts.json binds.",
      });
    }
    auto candidate = (mount->second / in_mount).lexically_normal();
    if (storage.KindOf(candidate) != cooked::PathKind::kFile) {
      return std::unexpected(UnresolvedSource {
        .code = "paktool.script_seal.external_source_unresolvable",
        .message = "External script source was not found in its source mount.",
      });
    }
    return candidate;
  }

  [[nodiscard]] auto NormalizeLooseCookedRoot(const std::filesystem::path& root)
    -> std::optional<std::filesystem::path>
  {
    if (root.empty()) {
      return std::nullopt;
    }

    if (root.is_absolute()) {
      return root.lexically_normal();
    }

    std::error_code ec;
    const auto absolute_root = std::filesystem::absolute(root, ec);
    if (ec) {
      return std::nullopt;
    }
    return absolute_root.lexically_normal();
  }

  struct StagedLooseRootRequest final {
    std::filesystem::path original_root;
    std::filesystem::path staging_parent;
    size_t source_index = 0;
  };

  [[nodiscard]] auto MakeStagedLooseRoot(const StagedLooseRootRequest& request)
    -> std::filesystem::path
  {
    const auto& original_root = request.original_root;
    const auto& staging_parent = request.staging_parent;
    const auto source_index = request.source_index;
    static auto counter = std::atomic_uint64_t { 0 };
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);

    auto name = original_root.filename().string();
    if (name.empty()) {
      name = "cooked";
    }
    for (auto& ch : name) {
      if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '-'
        && ch != '_' && ch != '.') {
        ch = '_';
      }
    }

    return staging_parent / kSealedSourcesDirName
      / (name + "-source-" + std::to_string(source_index) + "-case-"
        + std::to_string(id));
  }

  [[nodiscard]] auto LoadScriptTables(const SealingStorage& io,
    const std::filesystem::path& cooked_root) -> std::optional<ScriptTables>
  {
    const auto table_path = cooked_root / loose_layout::kScriptsTable;
    const auto data_path = cooked_root / loose_layout::kScriptsData;

    const auto table_exists
      = io.storage->KindOf(table_path) == cooked::PathKind::kFile;
    const auto data_exists
      = io.storage->KindOf(data_path) == cooked::PathKind::kFile;
    if (table_exists != data_exists) {
      return std::nullopt;
    }

    auto tables = ScriptTables {};
    tables.existed = table_exists;

    if (!table_exists) {
      tables.entries.push_back(ScriptResourceDesc {});
      return tables;
    }

    const auto table_bytes = io.Read(table_path, cooked::kMaxMetadataBytes);
    const auto data_bytes = io.Read(data_path, cooked::kMaxDataFileBytes);
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
    tables.data_blob = *data_bytes;
    return tables;
  }

  auto PersistScriptTables(const SealingStorage& io,
    const std::filesystem::path& cooked_root, const ScriptTables& tables)
    -> bool
  {
    return io.Write(cooked_root / loose_layout::kScriptsTable,
             std::as_bytes(std::span(tables.entries)))
      && io.Write(cooked_root / loose_layout::kScriptsData,
        std::span<const std::byte>(tables.data_blob));
  }

  //! The relative path of the descriptor of `key` in a loose root, as its
  //! index records it; empty when the index has none.
  [[nodiscard]] auto DescriptorRelPath(
    const lc::LooseCookedIndex& index, const data::AssetKey& key) -> std::string
  {
    return std::string(index.FindDescriptorRelPath(key).value_or(""));
  }

  //! Parses the script descriptor `descriptor_bytes` of `asset_entry`, stored
  //! at `descriptor_path` below its root.
  [[nodiscard]] auto ReadScriptAssetContext(
    const cooked::CookedAsset& asset_entry, std::string descriptor_relpath,
    std::filesystem::path descriptor_path,
    const std::optional<std::vector<std::byte>>& descriptor_bytes)
    -> std::optional<ScriptAssetPatchContext>
  {
    if (asset_entry.type != AssetType::kScript) {
      return std::nullopt;
    }

    if (!descriptor_bytes.has_value()) {
      return std::nullopt;
    }

    const auto descriptor_read
      = serio::CopyAt<ScriptAssetDesc>(*descriptor_bytes);
    if (!descriptor_read) {
      return std::nullopt;
    }
    const auto& descriptor = *descriptor_read;
    if (static_cast<AssetType>(descriptor.header.asset_type)
        != AssetType::kScript
      || descriptor.header.version
        != data::pak::scripting::kScriptAssetVersion) {
      return std::nullopt;
    }
    auto strings = data::DescriptorStrings {};
    try {
      strings = data::DescriptorStrings::FromDescriptor(
        *descriptor_bytes, descriptor.header.strings);
    } catch (const std::invalid_argument&) {
      return std::nullopt;
    }

    return ScriptAssetPatchContext {
      .key = asset_entry.key,
      .virtual_path = asset_entry.virtual_path,
      .descriptor_relpath = std::move(descriptor_relpath),
      .descriptor_path = std::move(descriptor_path),
      .descriptor = descriptor,
      .descriptor_bytes = *descriptor_bytes,
      .strings = std::move(strings),
    };
  }

  //! The script descriptor of `asset_entry` as the loose root `root` stores it.
  [[nodiscard]] auto ReadStagedScriptAssetContext(const SealingStorage& io,
    const std::filesystem::path& root, const lc::LooseCookedIndex& index,
    const cooked::CookedAsset& asset_entry)
    -> std::optional<ScriptAssetPatchContext>
  {
    auto relpath = DescriptorRelPath(index, asset_entry.key);
    if (relpath.empty()) {
      return std::nullopt;
    }
    auto path = root / std::filesystem::path(relpath);
    const auto bytes = io.Read(path, cooked::kMaxMetadataBytes);
    return ReadScriptAssetContext(
      asset_entry, std::move(relpath), std::move(path), bytes);
  }

  [[nodiscard]] auto AppendEmbeddedSourceResource(ScriptTables& tables,
    std::span<const std::byte> source_bytes) -> std::optional<ResourceIndexT>
  {
    if (tables.entries.size()
      >= static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
      return std::nullopt;
    }
    if (tables.data_blob.size()
      > static_cast<size_t>(std::numeric_limits<OffsetT>::max())) {
      return std::nullopt;
    }
    if (source_bytes.size()
      > static_cast<size_t>(std::numeric_limits<DataBlobSizeT>::max())) {
      return std::nullopt;
    }

    const auto resource_index
      = ResourceIndexT { static_cast<uint32_t>(tables.entries.size()) };

    auto resource_desc = ScriptResourceDesc {};
    resource_desc.data_offset = static_cast<OffsetT>(tables.data_blob.size());
    resource_desc.size_bytes = static_cast<DataBlobSizeT>(source_bytes.size());
    resource_desc.language = ScriptLanguage::kLuau;
    resource_desc.encoding = ScriptEncoding::kSource;
    resource_desc.compression = ScriptCompression::kNone;
    resource_desc.content_hash = base::ComputeSha256(source_bytes);

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
    descriptor.external_source_path_offset = 0;
  }

  //! The sealed descriptor: `descriptor`, the declarations and a string table
  //! rebuilt without the external source path, which must not ship.
  /*!
   @return The bytes, or nullopt when the declarations or strings are invalid.
  */
  [[nodiscard]] auto BuildSealedDescriptor(
    const ScriptAssetPatchContext& context, ScriptAssetDesc descriptor)
    -> std::optional<std::vector<std::byte>>
  {
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptParamType;

    const auto& table = descriptor.params;
    const auto& bytes = context.descriptor_bytes;
    const auto records_size
      = size_t { table.count } * sizeof(ScriptParamRecord);
    if (table.count != 0U
      && (table.offset != sizeof(ScriptAssetDesc)
        || records_size > bytes.size() - sizeof(ScriptAssetDesc))) {
      return std::nullopt;
    }
    auto records = std::vector<ScriptParamRecord>(table.count);
    std::memcpy(records.data(),
      std::span(bytes).subspan(sizeof(ScriptAssetDesc), records_size).data(),
      records_size);

    auto strings = data::DescriptorStringsBuilder {};
    const auto copy = [&](const data::pak::core::StringTableOffsetT offset)
      -> std::optional<data::pak::core::StringTableOffsetT> {
      const auto text = context.strings.TryGet(offset);
      if (!text) {
        return std::nullopt;
      }
      return strings.Add(*text);
    };
    const auto name = copy(descriptor.header.name_offset);
    if (!name) {
      return std::nullopt;
    }
    descriptor.header.name_offset = *name;
    for (auto& record : records) {
      const auto key = copy(record.key_offset);
      if (!key) {
        return std::nullopt;
      }
      record.key_offset = *key;
      if (record.type == ScriptParamType::kString) {
        const auto value = copy(record.value.as_string_offset);
        if (!value) {
          return std::nullopt;
        }
        record.value.as_string_offset = *value;
      }
    }

    const auto descriptor_bytes = std::as_bytes(std::span { &descriptor, 1U });
    auto sealed = std::vector<std::byte>(
      descriptor_bytes.begin(), descriptor_bytes.end());
    const auto record_bytes = std::as_bytes(std::span(records));
    sealed.insert(sealed.end(), record_bytes.begin(), record_bytes.end());
    data::AppendDescriptorStrings(sealed, strings);
    return sealed;
  }

  [[nodiscard]] auto RewriteStagedScriptDescriptor(const SealingStorage& io,
    const std::filesystem::path& original_root,
    const std::filesystem::path& staged_root, const lc::LooseCookedIndex& index,
    const cooked::CookedAsset& asset_entry, ScriptTables& tables,
    const SourceMountFolders& source_mounts)
    -> Result<std::optional<SealedDescriptor>, ScriptSealingError>
  {
    const auto context
      = ReadStagedScriptAssetContext(io, staged_root, index, asset_entry);
    if (!context.has_value()) {
      return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.script_descriptor_invalid",
          .error_message = "Failed to parse staged script descriptor.",
          .source_path = original_root,
          .descriptor_path = staged_root
            / std::filesystem::path(DescriptorRelPath(index, asset_entry.key)),
        });
    }

    auto descriptor = context->descriptor;
    import::AssetReferenceBuilder references(asset_entry.references);
    const auto allows_external
      = (static_cast<uint32_t>(descriptor.flags)
          & static_cast<uint32_t>(ScriptAssetFlags::kAllowExternalSource))
      != 0U;
    if (!allows_external) {
      return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Ok(
        std::nullopt);
    }

    // Compiled scripts ship their bytecode; only source-only scripts need
    // their external source embedded.
    if (descriptor.source_resource_index == data::kNoResourceReference
      && descriptor.bytecode_resource_index == data::kNoResourceReference) {
      const auto external_source
        = TryGetExternalSourcePath(descriptor, context->strings);
      if (!external_source.has_value()) {
        return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Err(
          ScriptSealingError {
            .error_code = "paktool.script_seal.external_source_missing",
            .error_message
            = "Script descriptor requires external source sealing but does not "
              "carry a valid external source path.",
            .source_path = original_root,
            .descriptor_path = context->descriptor_path,
          });
      }

      const auto resolved_path = ResolveExternalSourcePath(
        *io.storage, source_mounts, *external_source);
      if (!resolved_path.has_value()) {
        return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Err(
          ScriptSealingError {
            .error_code = std::string(resolved_path.error().code),
            .error_message = std::string(resolved_path.error().message),
            .source_path = original_root,
            .descriptor_path = context->descriptor_path,
            .external_source_path = std::string(*external_source),
          });
      }

      const auto source_bytes = io.Read(*resolved_path,
        serio::ByteCount { data::pak::scripting::kMaxScriptBytes });
      if (!source_bytes.has_value()) {
        return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Err(
          ScriptSealingError {
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
        return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Err(
          ScriptSealingError {
            .error_code = "paktool.script_seal.script_table_append_failed",
            .error_message
            = "Failed to append sealed script source to staged scripts.table.",
            .source_path = original_root,
            .descriptor_path = context->descriptor_path,
            .resolved_path = *resolved_path,
            .external_source_path = std::string(*external_source),
          });
      }
      descriptor.source_resource_index
        = references.AddResource(data::ResourceKind::kScript, *source_index);
    }

    ClearExternalSourceContract(descriptor);
    const auto descriptor_bytes = BuildSealedDescriptor(*context, descriptor);
    if (!descriptor_bytes) {
      return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.script_descriptor_invalid",
          .error_message = "Failed to parse staged script descriptor.",
          .source_path = original_root,
          .descriptor_path = context->descriptor_path,
        });
    }
    if (!io.Write(context->descriptor_path, *descriptor_bytes)) {
      return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.script_descriptor_write_failed",
          .error_message = "Failed to persist sealed script descriptor bytes.",
          .source_path = original_root,
          .descriptor_path = context->descriptor_path,
        });
    }

    return Result<std::optional<SealedDescriptor>, ScriptSealingError>::Ok(
      SealedDescriptor {
        .references = std::move(references).Build(),
        .written = {
          .size = descriptor_bytes->size(),
          .sha256 = base::ComputeSha256(*descriptor_bytes),
        },
      });
  }

  //! Publishes the staged root's index: the original index, which the staged
  //! root shows, with the sealed descriptors and the script tables replaced.
  //! Nothing sealing left as it was is registered, so nothing is read again.
  auto RefreshStagedLooseCookedIndex(const SealingStorage& io,
    const std::filesystem::path& staged_root,
    const cooked::CookedContent& content, const lc::LooseCookedIndex& index,
    const std::unordered_map<data::AssetKey, SealedDescriptor>& sealed,
    const ScriptTables& tables) -> Result<void, ScriptSealingError>
  {
    // Finishing the index locks the staged root through its lock file, which
    // is staging too, whether or not the refresh succeeds.
    io.published->push_back(
      staged_root / data::loose_cooked::kIndexLockFileName);
    try {
      auto writer = import::LooseCookedWriter(staged_root, *io.storage);
      for (const auto& asset : content.Assets()) {
        const auto sealed_asset = sealed.find(asset.key);
        if (sealed_asset == sealed.end()) {
          continue;
        }
        writer.RegisterProducedAssetDescriptor(asset.key, asset.type,
          asset.virtual_path, DescriptorRelPath(index, asset.key),
          sealed_asset->second.written, sealed_asset->second.references,
          import::LooseCookedProducer {
            .source = asset.source_path,
            .completes = {},
          });
      }
      const auto table_bytes = std::as_bytes(std::span(tables.entries));
      writer.RegisterExternalFile(data::loose_cooked::FileKind::kScriptsTable,
        loose_layout::kScriptsTable,
        import::LooseCookedFileDigest {
          .size = table_bytes.size(),
          .sha256 = base::ComputeSha256(table_bytes),
        });
      writer.RegisterExternalDataFile(
        data::loose_cooked::FileKind::kScriptsData, loose_layout::kScriptsData);
      static_cast<void>(writer.Finish());
    } catch (const std::exception& ex) {
      return Result<void, ScriptSealingError>::Err(ScriptSealingError {
        .error_code = "paktool.script_seal.index_refresh_failed",
        .error_message = ex.what(),
        .source_path = staged_root,
      });
    }
    io.published->push_back(staged_root / loose_layout::kIndexFileName);
    return Result<void, ScriptSealingError>::Ok();
  }

  [[nodiscard]] auto SealLooseCookedSource(const SealingStorage& io,
    const data::CookedSource& source,
    const std::filesystem::path& staging_parent, const size_t source_index,
    const SourceMountFolders& source_mounts)
    -> Result<SealedLooseSourceResult, ScriptSealingError>
  {
    const auto normalized_root = NormalizeLooseCookedRoot(source.path);
    if (!normalized_root.has_value()) {
      return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.loose_source_path_invalid",
          .error_message
          = "Loose-cooked source root could not be normalized to an absolute "
            "path.",
          .source_path = source.path,
        });
    }

    auto content = std::optional<cooked::CookedContent> {};
    auto index = std::optional<lc::LooseCookedIndex> {};
    try {
      content.emplace(
        cooked::CookedContent::Open(*io.storage, *normalized_root));
      const auto index_bytes = cooked::ReadWholeFile(*io.storage,
        *normalized_root / loose_layout::kIndexFileName,
        cooked::kMaxMetadataBytes);
      if (!index_bytes) {
        throw std::system_error(index_bytes.error(), "read the source's index");
      }
      index.emplace(lc::LooseCookedIndex::LoadFromBytes(*index_bytes));
    } catch (const std::exception& ex) {
      return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
        ScriptSealingError {
          .error_code = "paktool.script_seal.loose_source_load_failed",
          .error_message = ex.what(),
          .source_path = source.path,
        });
    }

    auto needs_sealing = false;
    for (const auto& asset : content->Assets()) {
      if (asset.type != AssetType::kScript) {
        continue;
      }
      auto relpath = DescriptorRelPath(*index, asset.key);
      auto descriptor_path = *normalized_root / std::filesystem::path(relpath);
      auto bytes = std::optional<std::vector<std::byte>> {};
      try {
        bytes = content->Descriptor(asset.key);
      } catch (const std::exception&) {
        bytes.reset();
      }
      const auto context = ReadScriptAssetContext(
        asset, std::move(relpath), descriptor_path, bytes);
      if (!context.has_value()) {
        return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
          ScriptSealingError {
            .error_code = "paktool.script_seal.script_descriptor_invalid",
            .error_message = "Failed to parse loose-cooked script descriptor.",
            .source_path = source.path,
            .descriptor_path = std::move(descriptor_path),
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

    const auto staged_root = MakeStagedLooseRoot({
      .original_root = *normalized_root,
      .staging_parent = staging_parent,
      .source_index = source_index,
    });
    // The staged root holds only what sealing changes; it shows the rest of
    // the original root.
    io.storage->Stage(staged_root, *normalized_root);
    const auto published_before = io.published->size();
    const auto cleanup_staged_root = [&io, published_before] -> void {
      const auto staged = std::span(*io.published).subspan(published_before);
      CleanupStagedFiles(*io.storage, staged);
      io.published->resize(published_before);
    };

    auto tables = LoadScriptTables(io, staged_root);
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
    std::unordered_map<data::AssetKey, SealedDescriptor> sealed_descriptors;
    for (const auto& asset : content->Assets()) {
      if (asset.type != AssetType::kScript) {
        continue;
      }
      auto sealed = RewriteStagedScriptDescriptor(io, *normalized_root,
        staged_root, *index, asset, *tables, source_mounts);
      if (!sealed.has_value()) {
        cleanup_staged_root();
        return Result<SealedLooseSourceResult, ScriptSealingError>::Err(
          sealed.error());
      }
      if (auto& descriptor = *sealed) {
        sealed_descriptors.emplace(asset.key, std::move(*descriptor));
        ++sealed_count;
      }
    }

    if (sealed_count == 0U) {
      cleanup_staged_root();
      return Result<SealedLooseSourceResult, ScriptSealingError>::Ok(
        SealedLooseSourceResult {});
    }

    if (!PersistScriptTables(io, staged_root, *tables)) {
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

    if (const auto refreshed = RefreshStagedLooseCookedIndex(
          io, staged_root, *content, *index, sealed_descriptors, *tables);
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
  const std::filesystem::path& staging_parent,
  const SourceMountFolders& source_mounts, StagedSourceStorage& storage)
  -> Result<ScriptSealingResult, ScriptSealingError>
{
  auto sealed_request = build_request;
  auto staged_loose_roots = std::vector<std::filesystem::path> {};
  auto staged_files = std::vector<std::filesystem::path> {};
  auto sealed_script_assets = uint32_t { 0 };
  const auto io = SealingStorage {
    .storage = make_observer(&storage),
    .published = make_observer(&staged_files),
  };

  for (size_t i = 0; i < sealed_request.sources.size(); ++i) {
    const auto& source = sealed_request.sources.at(i);
    if (source.kind != data::CookedSourceKind::kLooseCooked) {
      continue;
    }

    const auto sealed_root
      = SealLooseCookedSource(io, source, staging_parent, i, source_mounts);
    if (!sealed_root.has_value()) {
      CleanupStagedFiles(storage, staged_files);
      return MakeError(sealed_root.error().error_code,
        sealed_root.error().error_message, sealed_root.error().source_path,
        sealed_root.error().descriptor_path, sealed_root.error().resolved_path,
        sealed_root.error().external_source_path);
    }

    if (sealed_root->staged_root.has_value()) {
      sealed_request.sources.at(i).path = *sealed_root->staged_root;
      staged_loose_roots.push_back(*sealed_root->staged_root);
      sealed_script_assets += sealed_root->sealed_asset_count;
    }
  }

  return Result<ScriptSealingResult, ScriptSealingError>::Ok(
    ScriptSealingResult {
      .build_request = std::move(sealed_request),
      .staged_loose_roots = std::move(staged_loose_roots),
      .staged_files = std::move(staged_files),
      .sealed_script_assets = sealed_script_assets,
    });
}

auto CleanupStagedFiles(cooked::CookerStorage& storage,
  const std::span<const std::filesystem::path> staged_files) -> void
{
  auto folders = std::vector<std::filesystem::path> {};
  for (const auto& file : staged_files) {
    if (const auto removed = storage.Remove(file); !removed) {
      LOG_F(WARNING, "PakTool failed to remove staged file '{}' [{}]",
        file.string(), removed.error().message());
    }
    // Every folder up to and including the sealed sources folder is staging.
    for (auto folder = file.parent_path(); folder.has_relative_path();
      folder = folder.parent_path()) {
      folders.push_back(folder);
      if (folder.filename() == kSealedSourcesDirName) {
        break;
      }
    }
  }
  // Deepest first, so a folder is emptied before its parent is tried.
  std::ranges::sort(folders, std::ranges::greater {},
    [](const std::filesystem::path& folder) -> size_t {
      return static_cast<size_t>(std::distance(folder.begin(), folder.end()));
    });
  folders.erase(std::ranges::unique(folders).begin(), folders.end());
  for (const auto& folder : folders) {
    if (const auto removed = storage.RemoveEmptyFolder(folder); !removed) {
      LOG_F(WARNING, "PakTool failed to remove staging folder '{}' [{}]",
        folder.string(), removed.error().message());
    }
  }
}

} // namespace oxygen::content::pak::tool
