//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Cooker/testing/TextureBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::cooker::testing {

//! An asset of a `LooseCookedRoot`.
struct LooseAsset final {
  data::AssetType type = data::AssetType::kUnknown;
  //! Its virtual path below the mount, such as `Props/Wood.omat`.
  std::string leaf;
  //! Its mount, for a root whose content lies in several mounts; empty for
  //! the root's own.
  std::string mount;
  std::vector<std::byte> descriptor;
  data::AssetReferences references;
  //! Its key; by default the key of its virtual path.
  std::optional<data::AssetKey> key;
  //! A size or digest the index records instead of the descriptor's, for
  //! tests of a root that lies about its descriptors.
  std::optional<uint64_t> recorded_size;
  std::optional<base::Sha256Digest> recorded_sha256;
  //! What the asset requires of other mounts, as a cook records it.
  std::vector<data::loose_cooked::IndexedRequirement> requirements;
};

//! The resource tables of a loose cooked root.
enum class ResourceTable : uint8_t {
  kTextures,
  kBuffers,
  kScripts,
  kPhysics,
};

//! A loose cooked root of one mount a test describes: its assets and its
//! resource tables, held in memory.
/*!
 Every path it takes is relative to its mount: `LooseCookedRoot("Props")`
 places `Materials/Red.omat` at `/Props/Materials/Red.omat`. Each resource
 table starts with the reserved empty row zero, so the first resource a test
 adds is row 1; the add functions return the row. A table with no row of its
 own is left out, unless `AddEmptyTable` keeps it.

 `Files()` encodes the root with the product's encoders: the descriptor
 structs, `AssetReferences::Encode`, the resource descriptor structs and the
 loose index encoder.

 ```cpp
 auto game = LooseCookedRoot { "Game" };
 const auto wood = game.AddTexture(SeedBytes(1));
 auto material = MaterialBuilder { "Wood" };
 material.Texture(&MaterialAssetDesc::base_color_texture, wood);
 game.Material("Materials/Wood.omat", material);
 ```
*/
class LooseCookedRoot final {
public:
  explicit LooseCookedRoot(std::string mount = "Game", uint8_t seed = 1U);

  //! The mount, the first segment of every virtual path of the root.
  [[nodiscard]] auto Name() const noexcept -> const std::string&
  {
    return mount_;
  }

  [[nodiscard]] auto SourceKey() const noexcept -> const data::SourceKey&
  {
    return source_key_;
  }

  //! `/<mount>/<leaf>`.
  [[nodiscard]] auto VirtualPath(std::string_view leaf) const -> std::string;

  //! The key of the asset at `leaf`.
  [[nodiscard]] auto KeyOf(std::string_view leaf) const -> data::AssetKey;

  //! The virtual path of `asset`, in its own mount or the root's.
  [[nodiscard]] auto VirtualPathOf(const LooseAsset& asset) const
    -> std::string;

  //! Adds an asset; returns its key.
  auto Add(LooseAsset asset) -> data::AssetKey;

  //! Adds a material named after its file; returns its key.
  auto Material(std::string_view leaf) -> data::AssetKey;

  //! Adds the material `material` describes, with its references; returns its
  //! key.
  auto Material(std::string_view leaf, const MaterialBuilder& material)
    -> data::AssetKey;

  //! Adds a procedural cube geometry; returns its key.
  auto Geometry(std::string_view leaf) -> data::AssetKey;

  //! Adds the geometry `geometry` describes, with its references; returns its
  //! key.
  auto Geometry(std::string_view leaf, const GeometryBuilder& geometry)
    -> data::AssetKey;

  //! Adds a scene with no nodes; returns its key.
  auto Scene(std::string_view leaf) -> data::AssetKey;

  //! Adds the scene `scene` describes, with its references; returns its key.
  auto Scene(std::string_view leaf, const SceneBuilder& scene)
    -> data::AssetKey;

  //! Adds the physics sidecar of the scene `scene`, which the root already
  //! holds, with `bindings`, matching it as a cook does: its key, node count
  //! and content hash. Returns the sidecar's key.
  /*!
   @throw std::invalid_argument When the root holds no scene `scene`, or the
   bindings name the nodes of another scene.
  */
  auto PhysicsSidecar(std::string_view leaf, const data::AssetKey& scene,
    const PhysicsSidecarBuilder& bindings = {}) -> data::AssetKey;

  //! Adds the script `script` describes, with its references; returns its
  //! key. By default, a script with no payload, named after its file.
  auto Script(std::string_view leaf, const ScriptBuilder& script)
    -> data::AssetKey;
  auto Script(std::string_view leaf) -> data::AssetKey;

  //! Adds a physics material named after its file; returns its key.
  auto PhysicsMaterial(std::string_view leaf,
    data::pak::physics::PhysicsMaterialAssetDesc record = {}) -> data::AssetKey;

  //! Adds a collision shape named after its file, with the references to
  //! its material and payload; returns its key.
  auto CollisionShape(std::string_view leaf,
    const data::pak::physics::CollisionShapeAssetDesc& record)
    -> data::AssetKey;

  //! Adds an input action named after its file; returns its key.
  auto InputAction(std::string_view leaf,
    data::pak::input::InputActionAssetDesc record = {}) -> data::AssetKey;

  //! Adds the input mapping context `context` describes, with its
  //! references; returns its key.
  auto InputContext(std::string_view leaf, const InputContextBuilder& context)
    -> data::AssetKey;

  //! Adds a texture row with `payload`; returns its row. Every `Add*` row
  //! records the offset, size and SHA-256 of its payload, whatever `desc`
  //! holds.
  auto AddTexture(std::span<const std::byte> payload,
    data::pak::core::TextureResourceDesc desc = OnePixelTexture())
    -> ResourceIndexT;

  //! Adds the texture `texture` describes, a payload the texture loader
  //! reads, with its row; returns its row.
  auto AddTexture(const TextureBuilder& texture) -> ResourceIndexT;

  auto AddBuffer(std::span<const std::byte> payload,
    data::pak::core::BufferResourceDesc desc = RawBuffer()) -> ResourceIndexT;

  auto AddScript(std::span<const std::byte> payload,
    data::pak::scripting::ScriptResourceDesc desc = {}) -> ResourceIndexT;

  auto AddPhysics(std::span<const std::byte> payload,
    data::pak::physics::PhysicsResourceDesc desc = {}) -> ResourceIndexT;

  //! Adds the texture descriptor file `leaf` that names texture `row`, as a
  //! cooked root lists a texture in its browse index; returns its virtual
  //! path.
  auto DescribeTexture(std::string_view leaf, ResourceIndexT row)
    -> std::string;

  //! Adds the buffer descriptor file `relpath` that names buffer `row`, with
  //! only its whole-buffer view.
  auto DescribeBuffer(std::string relpath, ResourceIndexT row)
    -> LooseCookedRoot&;

  //! The buffer row `row`, with where its payload lies in the data file.
  [[nodiscard]] auto BufferRow(ResourceIndexT row) const
    -> data::pak::core::BufferResourceDesc;

  //! Keeps `table` with only its reserved row zero.
  auto AddEmptyTable(ResourceTable table) -> LooseCookedRoot&;

  //! Adds a member file as is, such as a texture descriptor or a sidecar.
  auto AddFile(data::loose_cooked::FileKind kind, std::string relpath,
    std::vector<std::byte> bytes, std::string virtual_path = {})
    -> LooseCookedRoot&;

  //! The catalog of a base pak of this root that holds `entries`; each
  //! `layer` has an identity of its own, as the bases of one patch must.
  [[nodiscard]] auto BaseCatalog(
    std::vector<data::PakCatalogEntry> entries = {}, uint8_t layer = 0U) const
    -> data::PakCatalog;

  //! The files of the loose root, by path relative to it.
  [[nodiscard]] auto Files() const
    -> std::map<std::string, std::vector<std::byte>>;

  //! A 1x1 RGBA8 texture row with no payload position yet.
  [[nodiscard]] static auto OnePixelTexture()
    -> data::pak::core::TextureResourceDesc;

  //! A raw buffer row (one-byte elements) with no payload position yet.
  [[nodiscard]] static auto RawBuffer() -> data::pak::core::BufferResourceDesc;

private:
  template <class Desc> struct Table final {
    std::vector<Desc> rows { Desc {} };
    std::vector<std::byte> data;
    //! Whether to write the table when it holds only row zero.
    bool keep = false;
  };

  struct MemberFile final {
    data::loose_cooked::FileKind kind = data::loose_cooked::FileKind::kUnknown;
    std::string relpath;
    std::vector<std::byte> bytes;
    std::string virtual_path;
  };

  template <class Desc>
  static auto AddRow(Table<Desc>& table, std::span<const std::byte> payload,
    Desc desc) -> ResourceIndexT;

  std::string mount_;
  data::SourceKey source_key_;
  std::vector<LooseAsset> assets_;
  //! The builder of each scene a `SceneBuilder` described.
  std::map<data::AssetKey, SceneBuilderId> scene_builders_;
  std::vector<MemberFile> members_;
  Table<data::pak::core::TextureResourceDesc> textures_;
  Table<data::pak::core::BufferResourceDesc> buffers_;
  Table<data::pak::scripting::ScriptResourceDesc> scripts_;
  Table<data::pak::physics::PhysicsResourceDesc> physics_;
};

} // namespace oxygen::cooker::testing
