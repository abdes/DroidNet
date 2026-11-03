//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::cooker::testing {

class SceneBuilder;

//! The identity of one `SceneBuilder`, which its handles carry.
using SceneBuilderId = NamedType<uint64_t, struct SceneBuilderIdTag, Comparable,
  Hashable, Printable, DefaultInitialized>;

//! A node of one `SceneBuilder`; only that builder and the sidecars paired
//! with its scene accept it.
class SceneNode final {
public:
  [[nodiscard]] auto Index() const noexcept -> data::pak::world::SceneNodeIndexT
  {
    return index_;
  }

  [[nodiscard]] auto Owner() const noexcept -> SceneBuilderId { return owner_; }

private:
  friend class SceneBuilder;

  SceneNode(
    const SceneBuilderId owner, const data::pak::world::SceneNodeIndexT index)
    : owner_(owner)
    , index_(index)
  {
  }

  SceneBuilderId owner_;
  data::pak::world::SceneNodeIndexT index_ = 0;
};

//! A script slot of one `SceneBuilder`.
class SceneSlot final {
public:
  [[nodiscard]] auto Id() const noexcept -> const data::ScriptSlotId&
  {
    return id_;
  }

private:
  friend class SceneBuilder;

  explicit SceneSlot(const data::ScriptSlotId id)
    : id_(id)
  {
  }

  data::ScriptSlotId id_;
};

//! The local transform of a node.
struct NodeTransform final {
  std::array<float, 3> translation { 0.0F, 0.0F, 0.0F };
  //! A quaternion, XYZW.
  std::array<float, 4> rotation { 0.0F, 0.0F, 0.0F, 1.0F };
  std::array<float, 3> scale { 1.0F, 1.0F, 1.0F };
};

//! The flags of a node (`kSceneNodeFlag_*`): its local values, and the
//! source modes of those it inherits.
struct NodeFlags final {
  uint32_t values = 0U;
  uint32_t inherited = 0U;
};

//! A script parameter's asset value; a nil key leaves it unset.
struct ScriptAssetValue final {
  data::AssetKey key;
  data::AssetType type = data::AssetType::kUnknown;
};

//! A script parameter's node value: a node of the scene or, with a path, a
//! node an instance under it brings in.
struct ScriptNodeValue final {
  SceneNode node;
  std::vector<data::NodeId> path;
};

//! A script parameter value, of a type the format holds.
using ScriptValue = std::variant<bool, int32_t, float, std::string,
  std::array<float, 2>, std::array<float, 3>, std::array<float, 4>,
  ScriptAssetValue, ScriptNodeValue>;

//! A parameter of a script slot or a slot change: `{ "speed", 3 }`.
struct ScriptParam final {
  std::string key;
  ScriptValue value;
};

//! The descriptor of a scene, as the scene cooker lays it out.
/*!
 A scene holds nodes, each with a name and a parent, and its components:
 renderables, cameras, lights, local fog volumes, overrides, scripting slots,
 scene instances, starting input contexts and the environment block. Every
 offset, count, entry size, string and node ID is computed:

 - A node's ID derives from its stable identity, `/<scene>/<names from the
   root>`, so equal descriptions give equal IDs.
 - Component tables are sorted as the cooker sorts them and appear in
   component type order; script slots and their parameters follow the
   tables, the environment block comes last.
 - `References()` are those the cook records: geometries, override assets,
   instanced scenes, input contexts, scripts and asset parameters, and the
   resources `Texture` binds.

 A handle of another builder, a parent the scene does not hold, flags a cook
 never writes or a scene the product reader refuses throws, where it is added
 or at `Bytes()`. Invalid content is a defect of valid bytes
 (`DescriptorDefects`).

 ```cpp
 auto main = SceneBuilder { "Main" };
 const auto root = main.Node("root");
 const auto crate = main.Node("crate", root);
 main.Renderable(crate, game.Geometry("Props/Crate.ogeo"));
 main.ScriptSlot(crate, spin, { { "speed", 3 }, { "label", "fast" } });
 game.Scene("Scenes/Main.oscene", main);
 ```
*/
class SceneBuilder final {
public:
  explicit SceneBuilder(std::string_view name);

  [[nodiscard]] auto Name() const noexcept -> const std::string&
  {
    return name_;
  }

  //=== Nodes ===-----------------------------------------------------------//

  //! Adds a root node.
  auto Node(std::string_view name) -> SceneNode;

  //! Adds a node under `parent`.
  auto Node(std::string_view name, SceneNode parent) -> SceneNode;

  //! Sets the local transform of `node`.
  auto Place(SceneNode node, const NodeTransform& transform) -> SceneBuilder&;

  //! Sets the flags of `node`; flags a cook never writes throw.
  auto Flags(SceneNode node, NodeFlags flags) -> SceneBuilder&;

  //! The ID of `node`.
  [[nodiscard]] auto IdOf(SceneNode node) const -> data::NodeId;

  [[nodiscard]] auto NodeCount() const noexcept -> uint32_t
  {
    return static_cast<uint32_t>(nodes_.size());
  }

  //=== Components ===------------------------------------------------------//

  //! Makes `node` render the geometry `geometry`.
  auto Renderable(SceneNode node, const data::AssetKey& geometry)
    -> SceneBuilder&;

  auto PerspectiveCamera(SceneNode node,
    data::pak::world::PerspectiveCameraRecord camera
    = DefaultPerspectiveCamera()) -> SceneBuilder&;

  auto OrthographicCamera(SceneNode node,
    data::pak::world::OrthographicCameraRecord camera
    = DefaultOrthographicCamera()) -> SceneBuilder&;

  auto DirectionalLight(SceneNode node,
    data::pak::world::DirectionalLightRecord light = DefaultDirectionalLight())
    -> SceneBuilder&;

  auto PointLight(SceneNode node,
    data::pak::world::PointLightRecord light = DefaultPointLight())
    -> SceneBuilder&;

  auto SpotLight(SceneNode node,
    data::pak::world::SpotLightRecord light = DefaultSpotLight())
    -> SceneBuilder&;

  auto LocalFogVolume(SceneNode node,
    data::pak::world::LocalFogVolumeRecord volume = DefaultLocalFogVolume())
    -> SceneBuilder&;

  //! Changes the property `id` of the renderable `node`, for its `part` (a
  //! surface) resolved against the geometry's `layout_revision`, or of the
  //! component itself with a nil part.
  auto Override(SceneNode node, data::schema::OverrideId id,
    data::pak::world::OverrideValue value, data::SurfaceId part = {},
    base::Sha256Digest layout_revision = {}) -> SceneBuilder&;

  //! Gives the renderable `node` the LOD policy `policy`.
  auto LodPolicy(SceneNode node, data::pak::geometry::LodPolicyRecord policy)
    -> SceneBuilder&;

  //=== Composition ===-----------------------------------------------------//

  //! Instances the scene `scene` under `node`.
  auto Instance(SceneNode node, const data::AssetKey& scene) -> SceneBuilder&;

  //! Changes the property `id` of the node at `path` below the instance root
  //! `root`.
  auto Change(SceneNode root, std::vector<data::NodeId> path,
    data::schema::OverrideId id, data::pak::world::OverrideValue value,
    data::SurfaceId part = {}, base::Sha256Digest layout_revision = {})
    -> SceneBuilder&;

  //! Changes the parameter `param.key` of the slot `slot` of the node at
  //! `path` below the instance root `root`.
  auto ScriptChange(SceneNode root, std::vector<data::NodeId> path,
    const data::ScriptSlotId& slot, ScriptParam param) -> SceneBuilder&;

  //! Starts the scene with the input context `context`, after those added
  //! before it.
  auto StartingInputContext(const data::AssetKey& context, int32_t priority = 0)
    -> SceneBuilder&;

  //! Starts the scene viewing through the camera of `node`.
  auto StartingCamera(SceneNode node) -> SceneBuilder&;

  //! Starts the scene viewing through the camera at `path` below the instance
  //! root `root`.
  auto StartingCamera(SceneNode root, std::vector<data::NodeId> path)
    -> SceneBuilder&;

  //! Marks the scene as a model's, which authored scenes instance.
  auto ModelScene() -> SceneBuilder&;

  //=== Scripting ===-------------------------------------------------------//

  //! Adds a slot running the script `script` on `node`, with `params`; its
  //! ID derives from the node's identity and the slot's position.
  auto ScriptSlot(SceneNode node, const data::AssetKey& script,
    std::vector<ScriptParam> params = {}, int32_t execution_order = 0)
    -> SceneSlot;

  //=== Environment and resources ===---------------------------------------//

  //! Adds an environment system record, typed by its header; `trailing` are
  //! the bytes its size includes after the record, such as the curve keys of
  //! a post process volume.
  template <typename Record>
  auto Environment(Record record, std::span<const std::byte> trailing = {})
    -> SceneBuilder&
  {
    record.header.record_size
      = static_cast<uint32_t>(sizeof(Record) + trailing.size());
    auto bytes = std::vector<std::byte>(sizeof(Record));
    std::memcpy(bytes.data(), &record, sizeof(Record));
    bytes.insert(bytes.end(), trailing.begin(), trailing.end());
    return AddEnvironment(std::move(bytes));
  }

  //! Binds the texture row `row` of the scene's source; returns the index a
  //! record of the scene names it by.
  auto Texture(ResourceIndexT row) -> data::ResourceReferenceIndex;

  //! Binds the texture `texture` another source holds; returns the index a
  //! record of the scene names it by.
  auto ExternalTexture(const data::AssetKey& texture)
    -> data::ResourceReferenceIndex;

  //=== Output ===----------------------------------------------------------//

  //! The descriptor.
  /*!
   @throw std::logic_error When the product reader refuses the scene.
  */
  [[nodiscard]] auto Bytes() const -> std::vector<std::byte>;

  //! The references a cook records of the scene.
  [[nodiscard]] auto References() const -> data::AssetReferences;

  //! The scene asset the product reads from `Bytes()`.
  [[nodiscard]] auto Build(const data::AssetKey& key) const
    -> std::shared_ptr<data::SceneAsset>;

  //! Whether `node` is a node of this builder.
  [[nodiscard]] auto Holds(SceneNode node) const noexcept -> bool;

  [[nodiscard]] auto Owner() const noexcept -> SceneBuilderId { return owner_; }

  //=== Defaults ===--------------------------------------------------------//

  [[nodiscard]] static auto DefaultPerspectiveCamera()
    -> data::pak::world::PerspectiveCameraRecord;
  [[nodiscard]] static auto DefaultOrthographicCamera()
    -> data::pak::world::OrthographicCameraRecord;
  [[nodiscard]] static auto DefaultDirectionalLight()
    -> data::pak::world::DirectionalLightRecord;
  [[nodiscard]] static auto DefaultPointLight()
    -> data::pak::world::PointLightRecord;
  [[nodiscard]] static auto DefaultSpotLight()
    -> data::pak::world::SpotLightRecord;
  [[nodiscard]] static auto DefaultLocalFogVolume()
    -> data::pak::world::LocalFogVolumeRecord;

private:
  struct NodeEntry final {
    std::string name;
    std::string identity;
    data::pak::world::SceneNodeIndexT parent = 0;
    data::NodeId id;
    NodeTransform transform;
    NodeFlags flags;
  };

  struct OverrideEntry final {
    data::pak::world::SceneNodeIndexT node = 0;
    std::vector<data::NodeId> path;
    data::schema::OverrideId id = data::schema::OverrideId::kInvalid;
    data::SurfaceId part;
    base::Sha256Digest layout_revision {};
    data::pak::world::OverrideValue value {};
  };

  struct SlotEntry final {
    data::pak::world::SceneNodeIndexT node = 0;
    data::ScriptSlotId id;
    data::AssetKey script;
    std::vector<ScriptParam> params;
    int32_t execution_order = 0;
  };

  struct ScriptChangeEntry final {
    data::pak::world::SceneNodeIndexT node = 0;
    std::vector<data::NodeId> path;
    data::ScriptSlotId slot;
    ScriptParam param;
  };

  [[nodiscard]] auto Require(SceneNode node) const
    -> data::pak::world::SceneNodeIndexT;
  auto AddEnvironment(std::vector<std::byte> record) -> SceneBuilder&;
  auto BindTexture(const data::TextureTarget& texture)
    -> data::ResourceReferenceIndex;

  std::string name_;
  SceneBuilderId owner_;
  std::vector<NodeEntry> nodes_;
  std::vector<data::pak::world::RenderableRecord> renderables_;
  std::vector<data::pak::world::PerspectiveCameraRecord> perspective_cameras_;
  std::vector<data::pak::world::OrthographicCameraRecord> orthographic_cameras_;
  std::vector<data::pak::world::DirectionalLightRecord> directional_lights_;
  std::vector<data::pak::world::PointLightRecord> point_lights_;
  std::vector<data::pak::world::SpotLightRecord> spot_lights_;
  std::vector<data::pak::world::LocalFogVolumeRecord> local_fog_volumes_;
  std::vector<OverrideEntry> overrides_;
  std::vector<data::pak::geometry::LodPolicyRecord> lod_policies_;
  std::vector<data::pak::world::SceneInstanceRecord> instances_;
  std::vector<data::pak::world::StartingInputContextRecord> contexts_;
  std::vector<SlotEntry> slots_;
  std::vector<ScriptChangeEntry> script_changes_;
  data::pak::world::SceneNodeIndexT starting_camera_
    = data::pak::world::kNoStartingCamera;
  std::vector<data::NodeId> starting_camera_path_;
  data::pak::world::SceneAssetFlags flags_
    = data::pak::world::SceneAssetFlags::kNone;
  std::vector<std::vector<std::byte>> environment_;
  //! The textures the scene binds, in binding order.
  std::vector<data::TextureTarget> textures_;
};

} // namespace oxygen::cooker::testing
