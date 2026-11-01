//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::cooker::testing {

namespace {

  namespace lc = data::loose_cooked;

  //! The asset name a descriptor carries: its file name without extension.
  auto NameOf(const std::string_view leaf) -> std::string
  {
    return std::filesystem::path(leaf).stem().generic_string();
  }

  //! The files of a root and the index records that describe them.
  class RootFiles final {
  public:
    //! Adds a member file and its record.
    auto AddFile(const lc::FileKind kind, const std::string& relpath,
      const std::span<const std::byte> bytes, const std::string& virtual_path)
      -> void
    {
      files_.insert_or_assign(
        relpath, std::vector<std::byte>(bytes.begin(), bytes.end()));
      records_.push_back({
        .kind = kind,
        .relpath = relpath,
        .size = bytes.size(),
        // A data file has no digest; its rows hold their payloads' digests.
        .sha256 = lc::IsResourceDataFile(kind) ? base::Sha256Digest {}
                                               : base::ComputeSha256(bytes),
        .virtual_path = virtual_path,
        // A texture descriptor or a sidecar has a producing source beside it.
        .source_path
        = virtual_path.empty() ? std::string {} : virtual_path + ".json",
      });
    }

    //! Adds the table and the data file of a table with a row of its own,
    //! or kept empty.
    template <class Table>
    auto AddTable(const Table& table, const lc::FileKind table_kind,
      const lc::FileKind data_kind, const std::string& name) -> void
    {
      if (table.rows.size() == 1U && !table.keep) {
        return;
      }
      AddFile(
        table_kind, name + ".table", std::as_bytes(std::span(table.rows)), {});
      AddFile(data_kind, name + ".data", table.data, {});
    }

    //! Adds the descriptor file of `asset`, padded to its recorded size when
    //! that is larger, and its index record.
    auto AddAsset(const LooseAsset& asset, const std::string& virtual_path)
      -> void
    {
      auto bytes = asset.descriptor;
      if (asset.recorded_size && *asset.recorded_size > bytes.size()) {
        bytes.resize(static_cast<size_t>(*asset.recorded_size));
      }
      // An asset of another mount keeps its descriptor apart from the
      // root's own, whose leaves may repeat.
      const auto relpath = asset.mount.empty()
        ? "Descriptors/" + asset.leaf
        : "Descriptors/" + asset.mount + "/" + asset.leaf;
      assets_.push_back({
        .key
        = asset.key.value_or(data::AssetKey::FromVirtualPath(virtual_path)),
        .asset_type = asset.type,
        .virtual_path = virtual_path,
        .descriptor_relpath = relpath,
        .descriptor_size = asset.recorded_size.value_or(bytes.size()),
        .descriptor_sha256
        = asset.recorded_sha256.value_or(base::ComputeSha256(bytes)),
        .references = asset.references,
        .source_path = virtual_path + ".json",
      });
      files_.insert_or_assign(relpath, std::move(bytes));
    }

    //! The files, the index encoded by the product's encoder included.
    auto Finish(const data::SourceKey& source_key)
      -> std::map<std::string, std::vector<std::byte>>
    {
      auto stream = serio::MemoryStream {};
      auto writer = serio::Writer(stream);
      const auto encoded = lc::EncodeLooseCookedIndex(
        {
          .source_key = source_key,
          .content_version = 0U,
          .assets = assets_,
          .files = records_,
        },
        writer);
      if (!encoded) {
        throw std::system_error(encoded.error(), "encode the loose index");
      }
      const auto index = stream.Take();
      const auto bytes = index.Bytes();
      files_.insert_or_assign(
        std::string(content::import::internal::loose_layout::kIndexFileName),
        std::vector<std::byte>(bytes.begin(), bytes.end()));
      return std::move(files_);
    }

  private:
    std::map<std::string, std::vector<std::byte>> files_;
    std::vector<lc::IndexedAsset> assets_;
    std::vector<lc::IndexedFile> records_;
  };

} // namespace

LooseCookedRoot::LooseCookedRoot(std::string mount, const uint8_t seed)
  : mount_(std::move(mount))
  , source_key_(SourceKeyOf(seed))
{
}

auto LooseCookedRoot::VirtualPath(const std::string_view leaf) const
  -> std::string
{
  return "/" + mount_ + "/" + std::string(leaf);
}

auto LooseCookedRoot::KeyOf(const std::string_view leaf) const -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath(VirtualPath(leaf));
}

auto LooseCookedRoot::VirtualPathOf(const LooseAsset& asset) const
  -> std::string
{
  return asset.mount.empty() ? VirtualPath(asset.leaf)
                             : "/" + asset.mount + "/" + asset.leaf;
}

auto LooseCookedRoot::Add(LooseAsset asset) -> data::AssetKey
{
  const auto key
    = asset.key.value_or(data::AssetKey::FromVirtualPath(VirtualPathOf(asset)));
  asset.key = key;
  assets_.push_back(std::move(asset));
  return key;
}

auto LooseCookedRoot::Material(const std::string_view leaf) -> data::AssetKey
{
  return Add({
    .type = data::AssetType::kMaterial,
    .leaf = std::string(leaf),
    .descriptor = MaterialDescriptor(NameOf(leaf)).bytes,
  });
}

auto LooseCookedRoot::Material(
  const std::string_view leaf, const ResourceIndexT texture) -> data::AssetKey
{
  auto descriptor = TexturedMaterialDescriptor(NameOf(leaf), texture);
  return Add({
    .type = data::AssetType::kMaterial,
    .leaf = std::string(leaf),
    .descriptor = std::move(descriptor.bytes),
    .references = std::move(descriptor.references),
  });
}

auto LooseCookedRoot::Geometry(const std::string_view leaf) -> data::AssetKey
{
  return Add({
    .type = data::AssetType::kGeometry,
    .leaf = std::string(leaf),
    .descriptor = GeometryDescriptor(NameOf(leaf)).bytes,
  });
}

auto LooseCookedRoot::Scene(const std::string_view leaf) -> data::AssetKey
{
  return Add({
    .type = data::AssetType::kScene,
    .leaf = std::string(leaf),
    .descriptor = SceneDescriptor(NameOf(leaf)).bytes,
  });
}

auto LooseCookedRoot::PhysicsSidecar(
  const std::string_view leaf, const data::AssetKey& scene) -> data::AssetKey
{
  const auto held
    = std::ranges::find_if(assets_, [&scene](const LooseAsset& asset) -> bool {
        return asset.type == data::AssetType::kScene && asset.key == scene;
      });
  if (held == assets_.end()) {
    throw std::invalid_argument("no scene '" + to_string(scene)
      + "' for the physics sidecar '" + std::string(leaf) + "'");
  }
  return Add({
    .type = data::AssetType::kPhysicsScene,
    .leaf = std::string(leaf),
    .descriptor = PhysicsSidecarDescriptor(scene, held->descriptor).bytes,
  });
}

auto LooseCookedRoot::AddTexture(const std::span<const std::byte> payload,
  data::pak::core::TextureResourceDesc desc) -> ResourceIndexT
{
  return AddRow(textures_, payload, desc);
}

auto LooseCookedRoot::AddBuffer(const std::span<const std::byte> payload,
  data::pak::core::BufferResourceDesc desc) -> ResourceIndexT
{
  return AddRow(buffers_, payload, desc);
}

auto LooseCookedRoot::AddScript(const std::span<const std::byte> payload,
  data::pak::scripting::ScriptResourceDesc desc) -> ResourceIndexT
{
  return AddRow(scripts_, payload, desc);
}

auto LooseCookedRoot::AddPhysics(const std::span<const std::byte> payload,
  data::pak::physics::PhysicsResourceDesc desc) -> ResourceIndexT
{
  return AddRow(physics_, payload, desc);
}

auto LooseCookedRoot::DescribeTexture(
  const std::string_view leaf, const ResourceIndexT row) -> std::string
{
  auto path = VirtualPath(leaf);
  AddFile(lc::FileKind::kTextureDescriptor, std::string(leaf),
    data::EncodeTextureResourceDescriptor({
      .index = row,
      .descriptor = textures_.rows.at(row.get()),
    }),
    path);
  return path;
}

auto LooseCookedRoot::BufferRow(const ResourceIndexT row) const
  -> data::pak::core::BufferResourceDesc
{
  return buffers_.rows.at(row.get());
}

auto LooseCookedRoot::AddEmptyTable(const ResourceTable table)
  -> LooseCookedRoot&
{
  switch (table) {
  case ResourceTable::kTextures:
    textures_.keep = true;
    break;
  case ResourceTable::kBuffers:
    buffers_.keep = true;
    break;
  case ResourceTable::kScripts:
    scripts_.keep = true;
    break;
  case ResourceTable::kPhysics:
    physics_.keep = true;
    break;
  }
  return *this;
}

auto LooseCookedRoot::AddFile(const lc::FileKind kind, std::string relpath,
  std::vector<std::byte> bytes, std::string virtual_path) -> LooseCookedRoot&
{
  members_.push_back({
    .kind = kind,
    .relpath = std::move(relpath),
    .bytes = std::move(bytes),
    .virtual_path = std::move(virtual_path),
  });
  return *this;
}

auto LooseCookedRoot::BaseCatalog(std::vector<data::PakCatalogEntry> entries,
  const uint8_t layer) const -> data::PakCatalog
{
  constexpr auto kFirstBaseSeed = uint8_t { 0xC1U };
  auto catalog = data::PakCatalog {
    .source_key = SourceKeyOf(static_cast<uint8_t>(kFirstBaseSeed + layer)),
    .content_version = 1U,
    .catalog_digest = {},
    .mount = mount_,
    .entries = std::move(entries),
  };
  catalog.catalog_digest = catalog.ComputeDigest().value();
  return catalog;
}

auto LooseCookedRoot::Files() const
  -> std::map<std::string, std::vector<std::byte>>
{
  auto files = RootFiles {};
  files.AddTable(textures_, lc::FileKind::kTexturesTable,
    lc::FileKind::kTexturesData, "textures");
  files.AddTable(buffers_, lc::FileKind::kBuffersTable,
    lc::FileKind::kBuffersData, "buffers");
  files.AddTable(scripts_, lc::FileKind::kScriptsTable,
    lc::FileKind::kScriptsData, "scripts");
  files.AddTable(physics_, lc::FileKind::kPhysicsTable,
    lc::FileKind::kPhysicsData, "physics");
  for (const auto& file : members_) {
    files.AddFile(file.kind, file.relpath, file.bytes, file.virtual_path);
  }
  for (const auto& asset : assets_) {
    files.AddAsset(asset, VirtualPathOf(asset));
  }
  return files.Finish(source_key_);
}

auto LooseCookedRoot::OnePixelTexture() -> data::pak::core::TextureResourceDesc
{
  return {
    .data_offset = 0U,
    .size_bytes = 0U,
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0U,
    .width = 1U,
    .height = 1U,
    .depth = 1U,
    .array_layers = 1U,
    .mip_levels = 1U,
    .format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256U,
  };
}

template <class Desc>
auto LooseCookedRoot::AddRow(Table<Desc>& table,
  const std::span<const std::byte> payload, Desc desc) -> ResourceIndexT
{
  desc.data_offset = table.data.size();
  desc.size_bytes = static_cast<decltype(desc.size_bytes)>(payload.size());
  desc.content_hash = base::ComputeSha256(payload);
  table.data.insert(table.data.end(), payload.begin(), payload.end());
  table.rows.push_back(desc);
  return ResourceIndexT { static_cast<uint32_t>(table.rows.size() - 1U) };
}

} // namespace oxygen::cooker::testing
