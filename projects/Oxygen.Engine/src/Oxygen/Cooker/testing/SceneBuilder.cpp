//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/NodePathTable.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::cooker::testing {

namespace {

  namespace world = data::pak::world;
  namespace script = data::pak::scripting;

  //! Each builder's handles are its own.
  auto NextOwner() -> SceneBuilderId
  {
    static std::atomic<uint64_t> next { 1U };
    return SceneBuilderId { next.fetch_add(1U) };
  }

  auto Append(std::vector<std::byte>& out, std::span<const std::byte> bytes)
    -> void
  {
    out.insert(out.end(), bytes.begin(), bytes.end());
  }

  //! One component table: its type and its packed records.
  struct Table final {
    data::ComponentType type {};
    uint32_t entry_size = 0;
    uint32_t count = 0;
    std::vector<std::byte> bytes;
  };

  template <typename Record>
  auto AddTable(std::vector<Table>& tables, const data::ComponentType type,
    std::span<const Record> records) -> void
  {
    if (records.empty()) {
      return;
    }
    const auto bytes = std::as_bytes(records);
    tables.push_back({
      .type = type,
      .entry_size = sizeof(Record),
      .count = static_cast<uint32_t>(records.size()),
      .bytes = { bytes.begin(), bytes.end() },
    });
  }

  template <typename Record>
  auto SortByNode(std::vector<Record>& records) -> void
  {
    std::ranges::stable_sort(
      records, {}, [](const Record& record) -> world::SceneNodeIndexT {
        return record.node_index;
      });
  }

  //! The nodes that own a record of `lists`.
  template <typename... Lists>
  auto OwnersOf(const Lists&... lists) -> std::vector<world::SceneNodeIndexT>
  {
    auto owners = std::vector<world::SceneNodeIndexT> {};
    const auto add = [&owners](const auto& records) -> void {
      for (const auto& record : records) {
        owners.push_back(record.node_index);
      }
    };
    (add(lists), ...);
    return owners;
  }

  //! Adds a record owned by `node`, one per node across `owned`.
  template <typename Record>
  auto AddOwned(std::vector<Record>& records,
    std::vector<world::SceneNodeIndexT>& owned,
    const world::SceneNodeIndexT node, Record record, const char* what) -> void
  {
    if (std::ranges::contains(owned, node)) {
      throw std::invalid_argument(
        std::string("a node holds one ") + what + " at most");
    }
    owned.push_back(node);
    record.node_index = node;
    records.push_back(record);
  }

} // namespace

SceneBuilder::SceneBuilder(const std::string_view name)
  : name_(name)
  , owner_(NextOwner())
{
}

auto SceneBuilder::Node(const std::string_view name) -> SceneNode
{
  const auto index = static_cast<world::SceneNodeIndexT>(nodes_.size());
  auto identity = "/" + name_ + "/"
    + (name.empty() ? "#" + std::to_string(index) : std::string(name));
  if (std::ranges::contains(nodes_, identity, &NodeEntry::identity)) {
    throw std::invalid_argument("two nodes of '" + name_ + "' are " + identity);
  }
  const auto id = data::NodeId::FromStableIdentity(identity);
  nodes_.push_back({
    .name = std::string(name),
    .identity = std::move(identity),
    .parent = index,
    .id = id,
  });
  return { owner_, index };
}

auto SceneBuilder::Node(const std::string_view name, const SceneNode parent)
  -> SceneNode
{
  const auto parent_index = Require(parent);
  const auto index = static_cast<world::SceneNodeIndexT>(nodes_.size());
  auto identity = nodes_.at(parent_index).identity + "/"
    + (name.empty() ? "#" + std::to_string(index) : std::string(name));
  if (std::ranges::contains(nodes_, identity, &NodeEntry::identity)) {
    throw std::invalid_argument("two nodes of '" + name_ + "' are " + identity);
  }
  const auto id = data::NodeId::FromStableIdentity(identity);
  nodes_.push_back({
    .name = std::string(name),
    .identity = std::move(identity),
    .parent = parent_index,
    .id = id,
  });
  return { owner_, index };
}

auto SceneBuilder::Place(const SceneNode node, const NodeTransform& transform)
  -> SceneBuilder&
{
  nodes_.at(Require(node)).transform = transform;
  return *this;
}

auto SceneBuilder::Flags(const SceneNode node, const NodeFlags flags)
  -> SceneBuilder&
{
  auto record = world::NodeRecord {};
  record.node_flags = flags.values;
  record.inherited_flags = flags.inherited;
  if (!world::HasCanonicalNodeFlags(record)) {
    throw std::invalid_argument("a cook never writes these node flags");
  }
  nodes_.at(Require(node)).flags = flags;
  return *this;
}

auto SceneBuilder::IdOf(const SceneNode node) const -> data::NodeId
{
  return nodes_.at(Require(node)).id;
}

auto SceneBuilder::Renderable(
  const SceneNode node, const data::AssetKey& geometry) -> SceneBuilder&
{
  const auto index = Require(node);
  if (std::ranges::contains(
        renderables_, index, &world::RenderableRecord::node_index)) {
    throw std::invalid_argument("a node renders one geometry at most");
  }
  renderables_.push_back({ .node_index = index, .geometry_key = geometry });
  return *this;
}

auto SceneBuilder::PerspectiveCamera(
  const SceneNode node, world::PerspectiveCameraRecord camera) -> SceneBuilder&
{
  const auto index = Require(node);
  if (std::ranges::contains(
        OwnersOf(perspective_cameras_, orthographic_cameras_), index)) {
    throw std::invalid_argument("a node holds one camera at most");
  }
  camera.node_index = index;
  perspective_cameras_.push_back(camera);
  return *this;
}

auto SceneBuilder::OrthographicCamera(
  const SceneNode node, world::OrthographicCameraRecord camera) -> SceneBuilder&
{
  const auto index = Require(node);
  if (std::ranges::contains(
        OwnersOf(perspective_cameras_, orthographic_cameras_), index)) {
    throw std::invalid_argument("a node holds one camera at most");
  }
  camera.node_index = index;
  orthographic_cameras_.push_back(camera);
  return *this;
}

auto SceneBuilder::DirectionalLight(
  const SceneNode node, world::DirectionalLightRecord light) -> SceneBuilder&
{
  auto lights = OwnersOf(directional_lights_, point_lights_, spot_lights_);
  AddOwned(directional_lights_, lights, Require(node), light, "light");
  return *this;
}

auto SceneBuilder::PointLight(
  const SceneNode node, world::PointLightRecord light) -> SceneBuilder&
{
  auto lights = OwnersOf(directional_lights_, point_lights_, spot_lights_);
  AddOwned(point_lights_, lights, Require(node), light, "light");
  return *this;
}

auto SceneBuilder::SpotLight(const SceneNode node, world::SpotLightRecord light)
  -> SceneBuilder&
{
  auto lights = OwnersOf(directional_lights_, point_lights_, spot_lights_);
  AddOwned(spot_lights_, lights, Require(node), light, "light");
  return *this;
}

auto SceneBuilder::LocalFogVolume(
  const SceneNode node, world::LocalFogVolumeRecord volume) -> SceneBuilder&
{
  volume.node_index = Require(node);
  local_fog_volumes_.push_back(volume);
  return *this;
}

auto SceneBuilder::Override(const SceneNode node,
  const data::schema::OverrideId id, const world::OverrideValue value,
  const data::SurfaceId part, const base::Sha256Digest layout_revision)
  -> SceneBuilder&
{
  overrides_.push_back({
    .node = Require(node),
    .path = {},
    .id = id,
    .part = part,
    .layout_revision = layout_revision,
    .value = value,
  });
  return *this;
}

auto SceneBuilder::LodPolicy(const SceneNode node,
  const data::pak::geometry::LodPolicyRecord policy) -> SceneBuilder&
{
  const auto index = Require(node);
  auto value = world::OverrideValue {};
  value.as_record_index = world::OverrideRecordIndex {
    static_cast<uint32_t>(lod_policies_.size()),
  };
  lod_policies_.push_back(policy);
  overrides_.push_back({
    .node = index,
    .path = {},
    .id = data::schema::OverrideId::kGeometryLodPolicy,
    .part = {},
    .layout_revision = {},
    .value = value,
  });
  return *this;
}

auto SceneBuilder::Instance(const SceneNode node, const data::AssetKey& scene)
  -> SceneBuilder&
{
  const auto index = Require(node);
  if (scene.IsNil()) {
    throw std::invalid_argument("an instance names a scene");
  }
  if (std::ranges::contains(
        instances_, index, &world::SceneInstanceRecord::node_index)) {
    throw std::invalid_argument("a node holds one instance at most");
  }
  instances_.push_back({ .node_index = index, .scene_key = scene });
  return *this;
}

auto SceneBuilder::Change(const SceneNode root, std::vector<data::NodeId> path,
  const data::schema::OverrideId id, const world::OverrideValue value,
  const data::SurfaceId part, const base::Sha256Digest layout_revision)
  -> SceneBuilder&
{
  if (path.empty()) {
    throw std::invalid_argument("a change names a node below an instance");
  }
  overrides_.push_back({
    .node = Require(root),
    .path = std::move(path),
    .id = id,
    .part = part,
    .layout_revision = layout_revision,
    .value = value,
  });
  return *this;
}

auto SceneBuilder::ScriptChange(const SceneNode root,
  std::vector<data::NodeId> path, const data::ScriptSlotId& slot,
  ScriptParam param) -> SceneBuilder&
{
  if (path.empty()) {
    throw std::invalid_argument("a change names a node below an instance");
  }
  script_changes_.push_back({
    .node = Require(root),
    .path = std::move(path),
    .slot = slot,
    .param = std::move(param),
  });
  return *this;
}

auto SceneBuilder::StartingInputContext(
  const data::AssetKey& context, const int32_t priority) -> SceneBuilder&
{
  if (context.IsNil()
    || std::ranges::contains(
      contexts_, context, &world::StartingInputContextRecord::context_key)) {
    throw std::invalid_argument("a scene starts with distinct contexts");
  }
  contexts_.push_back({ .context_key = context, .priority = priority });
  return *this;
}

auto SceneBuilder::StartingCamera(const SceneNode node) -> SceneBuilder&
{
  starting_camera_ = Require(node);
  starting_camera_path_.clear();
  return *this;
}

auto SceneBuilder::StartingCamera(
  const SceneNode root, std::vector<data::NodeId> path) -> SceneBuilder&
{
  if (path.empty()) {
    throw std::invalid_argument("a camera path names a node below an instance");
  }
  starting_camera_ = Require(root);
  starting_camera_path_ = std::move(path);
  return *this;
}

auto SceneBuilder::ModelScene() -> SceneBuilder&
{
  flags_ = world::SceneAssetFlags::kModelScene;
  return *this;
}

auto SceneBuilder::ScriptSlot(const SceneNode node,
  const data::AssetKey& script, std::vector<ScriptParam> params,
  const int32_t execution_order) -> SceneSlot
{
  const auto index = Require(node);
  if (script.IsNil()) {
    throw std::invalid_argument("a slot runs a script");
  }
  const auto position = std::ranges::count(slots_, index, &SlotEntry::node);
  const auto id = data::ScriptSlotId::FromStableIdentity(
    nodes_.at(index).identity + "/slot/" + std::to_string(position));
  slots_.push_back({
    .node = index,
    .id = id,
    .script = script,
    .params = std::move(params),
    .execution_order = execution_order,
  });
  return SceneSlot { id };
}

auto SceneBuilder::Texture(const ResourceIndexT row)
  -> data::ResourceReferenceIndex
{
  return BindTexture(data::TextureTarget { row });
}

auto SceneBuilder::ExternalTexture(const data::AssetKey& texture)
  -> data::ResourceReferenceIndex
{
  return BindTexture(data::TextureTarget { texture });
}

auto SceneBuilder::BindTexture(const data::TextureTarget& texture)
  -> data::ResourceReferenceIndex
{
  // A cook binds each texture once, in the order the scene names them; the
  // references bind them in the same order, so a binding's index is its
  // position.
  auto found = std::ranges::find(textures_, texture);
  if (found == textures_.end()) {
    textures_.push_back(texture);
    found = std::prev(textures_.end());
  }
  return data::ResourceReferenceIndex {
    static_cast<uint32_t>(found - textures_.begin()),
  };
}

auto SceneBuilder::AddEnvironment(std::vector<std::byte> record)
  -> SceneBuilder&
{
  auto header = world::SceneEnvironmentSystemRecordHeader {};
  std::memcpy(&header, record.data(), sizeof(header));
  if (!world::IsValidEnvironmentRecordSize(header)) {
    throw std::invalid_argument("a cook never writes this environment record");
  }
  environment_.push_back(std::move(record));
  return *this;
}

auto SceneBuilder::Bytes() const -> std::vector<std::byte>
{
  using content::import::internal::NodePathTable;

  auto strings = data::DescriptorStringsBuilder {};
  auto nodes = std::vector<world::NodeRecord> {};
  nodes.reserve(nodes_.size());
  for (const auto& entry : nodes_) {
    auto record = world::NodeRecord {};
    record.node_id = entry.id;
    record.scene_name_offset = strings.Add(entry.name);
    record.parent_index = entry.parent;
    record.node_flags = entry.flags.values;
    record.inherited_flags = entry.flags.inherited;
    std::ranges::copy(
      entry.transform.translation, std::begin(record.translation));
    std::ranges::copy(entry.transform.rotation, std::begin(record.rotation));
    std::ranges::copy(entry.transform.scale, std::begin(record.scale));
    nodes.push_back(record);
  }

  auto desc = world::SceneAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  desc.header.version = world::kSceneAssetVersion;
  desc.header.name_offset = strings.Add(name_);
  desc.flags = flags_;

  auto paths = NodePathTable {};
  const auto ref_of
    = [&paths](const world::SceneNodeIndexT node,
        const std::vector<data::NodeId>& path) -> world::NodeRef {
    return world::NodeRef {
      .node_index = node,
      .node_path = path.empty() ? world::kNoNodePath : paths.Intern(path),
    };
  };

  auto overrides = std::vector<world::OverrideRecord> {};
  for (const auto& entry : overrides_) {
    overrides.push_back({
      .target = ref_of(entry.node, entry.path),
      .override_id = entry.id,
      .part_id = entry.part,
      .layout_revision = entry.layout_revision,
      .value = entry.value,
    });
  }
  std::ranges::sort(overrides, [](const auto& lhs, const auto& rhs) -> bool {
    return std::tie(lhs.target, lhs.override_id, lhs.part_id)
      < std::tie(rhs.target, rhs.override_id, rhs.part_id);
  });

  const auto write_param
    = [&](const ScriptParam& param) -> script::ScriptParamRecord {
    auto record = script::ScriptParamRecord {};
    record.key_offset = strings.Add(param.key);
    std::visit(
      [&](const auto& value) -> void {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
          record.type = script::ScriptParamType::kBool;
          record.value.as_bool = value;
        } else if constexpr (std::is_same_v<T, int32_t>) {
          record.type = script::ScriptParamType::kInt32;
          record.value.as_int32 = value;
        } else if constexpr (std::is_same_v<T, float>) {
          record.type = script::ScriptParamType::kFloat;
          record.value.as_float = value;
        } else if constexpr (std::is_same_v<T, std::string>) {
          record.type = script::ScriptParamType::kString;
          record.value.as_string_offset = strings.Add(value);
        } else if constexpr (std::is_same_v<T, std::array<float, 2>>) {
          record.type = script::ScriptParamType::kVec2;
          std::ranges::copy(value, std::begin(record.value.as_vec));
        } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
          record.type = script::ScriptParamType::kVec3;
          std::ranges::copy(value, std::begin(record.value.as_vec));
        } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
          record.type = script::ScriptParamType::kVec4;
          std::ranges::copy(value, std::begin(record.value.as_vec));
        } else if constexpr (std::is_same_v<T, ScriptAssetValue>) {
          record.type = script::ScriptParamType::kAsset;
          record.value.as_asset
            = { .key = value.key, .asset_type = value.type };
        } else {
          record.type = script::ScriptParamType::kNode;
          record.value.as_node = ref_of(Require(value.node), value.path);
        }
      },
      param.value);
    return record;
  };

  // Slots are grouped by node, in node order; each parameter array follows
  // the slot table.
  auto slot_entries = slots_;
  std::ranges::stable_sort(slot_entries, {}, &SlotEntry::node);
  auto slots = std::vector<script::ScriptSlotRecord> {};
  auto params = std::vector<script::ScriptParamRecord> {};
  auto components = std::vector<script::ScriptingComponentRecord> {};
  for (const auto& entry : slot_entries) {
    if (components.empty() || components.back().node_index != entry.node) {
      components.push_back({
        .node_index = entry.node,
        .flags = script::ScriptingComponentFlags::kNone,
        .slot_start_index = static_cast<uint32_t>(slots.size()),
        .slot_count = 0U,
      });
    }
    ++components.back().slot_count;
    slots.push_back({
      .slot_id = entry.id,
      .script_asset_key = entry.script,
      .params_array_offset = params.size(),
      .params_count = static_cast<uint32_t>(entry.params.size()),
      .execution_order = entry.execution_order,
      .flags = script::ScriptSlotFlags::kNone,
    });
    for (const auto& param : entry.params) {
      params.push_back(write_param(param));
    }
  }

  struct KeyedChange final {
    script::ScriptParamOverrideRecord record;
    std::string key;
  };
  auto keyed = std::vector<KeyedChange> {};
  for (const auto& change : script_changes_) {
    keyed.push_back({
      .record = {
        .target = ref_of(change.node, change.path),
        .slot_id = change.slot,
        .param = write_param(change.param),
      },
      .key = change.param.key,
    });
  }
  std::ranges::sort(keyed, [](const auto& lhs, const auto& rhs) -> bool {
    return std::tie(lhs.record.target, lhs.record.slot_id, lhs.key)
      < std::tie(rhs.record.target, rhs.record.slot_id, rhs.key);
  });
  auto changes = std::vector<script::ScriptParamOverrideRecord> {};
  for (const auto& change : keyed) {
    changes.push_back(change.record);
  }

  desc.starting_camera = starting_camera_ == world::kNoStartingCamera
    ? world::NodeRef { .node_index = world::kNoStartingCamera }
    : ref_of(starting_camera_, starting_camera_path_);

  auto renderables = renderables_;
  SortByNode(renderables);
  auto perspective = perspective_cameras_;
  SortByNode(perspective);
  auto orthographic = orthographic_cameras_;
  SortByNode(orthographic);
  auto directional = directional_lights_;
  SortByNode(directional);
  auto point = point_lights_;
  SortByNode(point);
  auto spot = spot_lights_;
  SortByNode(spot);
  auto instances = instances_;
  SortByNode(instances);
  auto fog = local_fog_volumes_;
  std::ranges::stable_sort(fog, [](const auto& lhs, const auto& rhs) -> bool {
    return std::tie(lhs.node_index, lhs.sort_priority)
      < std::tie(rhs.node_index, rhs.sort_priority);
  });

  using data::ComponentType;
  auto tables = std::vector<Table> {};
  AddTable<world::RenderableRecord>(
    tables, ComponentType::kRenderable, renderables);
  AddTable<world::OverrideRecord>(tables, ComponentType::kOverride, overrides);
  AddTable<data::pak::geometry::LodPolicyRecord>(
    tables, ComponentType::kOverrideLodPolicy, lod_policies_);
  AddTable<world::LocalFogVolumeRecord>(
    tables, ComponentType::kLocalFogVolume, fog);
  AddTable<world::PerspectiveCameraRecord>(
    tables, ComponentType::kPerspectiveCamera, perspective);
  AddTable<world::OrthographicCameraRecord>(
    tables, ComponentType::kOrthographicCamera, orthographic);
  AddTable<world::DirectionalLightRecord>(
    tables, ComponentType::kDirectionalLight, directional);
  AddTable<world::PointLightRecord>(tables, ComponentType::kPointLight, point);
  AddTable<world::SpotLightRecord>(tables, ComponentType::kSpotLight, spot);
  AddTable<world::SceneInstanceRecord>(
    tables, ComponentType::kSceneInstance, instances);
  AddTable<world::StartingInputContextRecord>(
    tables, ComponentType::kStartingInputContext, contexts_);
  AddTable<script::ScriptingComponentRecord>(
    tables, ComponentType::kScripting, components);
  AddTable<script::ScriptParamOverrideRecord>(
    tables, ComponentType::kScriptParamOverride, changes);
  AddTable<world::NodePathRecord>(
    tables, ComponentType::kNodePath, paths.Records());
  AddTable<world::NodePathElement>(
    tables, ComponentType::kNodePathElement, paths.Elements());
  std::ranges::stable_sort(tables, {}, [](const Table& table) -> uint32_t {
    return static_cast<uint32_t>(table.type);
  });

  auto offset = uint64_t { sizeof(world::SceneAssetDesc) };
  desc.nodes = {
    .offset = offset,
    .count = static_cast<uint32_t>(nodes.size()),
    .entry_size = sizeof(world::NodeRecord),
  };
  offset += nodes.size() * sizeof(world::NodeRecord);
  desc.header.strings = {
    .offset = static_cast<data::pak::core::StringTableOffsetT>(offset),
    .size = strings.Size(),
  };
  offset += strings.Size();
  auto directory = std::vector<world::SceneComponentTableDesc> {};
  if (!tables.empty()) {
    desc.component_table_directory_offset = offset;
    desc.component_table_count = static_cast<uint32_t>(tables.size());
    offset += tables.size() * sizeof(world::SceneComponentTableDesc);
    for (const auto& table : tables) {
      directory.push_back({
        .component_type = static_cast<uint32_t>(table.type),
        .table = {
          .offset = offset,
          .count = table.count,
          .entry_size = table.entry_size,
        },
      });
      offset += table.bytes.size();
    }
  }
  if (!slots.empty()) {
    desc.script_slots = {
      .offset = offset,
      .count = static_cast<uint32_t>(slots.size()),
      .entry_size = sizeof(script::ScriptSlotRecord),
    };
    const auto parameter_base
      = offset + (slots.size() * sizeof(script::ScriptSlotRecord));
    for (auto& slot : slots) {
      slot.params_array_offset = slot.params_count == 0U ? 0U
                                                         : parameter_base
          + (slot.params_array_offset * sizeof(script::ScriptParamRecord));
    }
  }

  auto bytes = std::vector<std::byte> {};
  Append(bytes, std::as_bytes(std::span(&desc, 1U)));
  Append(bytes, std::as_bytes(std::span(nodes)));
  Append(bytes, strings.Bytes());
  Append(bytes, std::as_bytes(std::span(directory)));
  for (const auto& table : tables) {
    Append(bytes, table.bytes);
  }
  Append(bytes, std::as_bytes(std::span(slots)));
  Append(bytes, std::as_bytes(std::span(params)));
  // A cooked scene always ends with its environment block, empty or not.
  auto environment = world::SceneEnvironmentBlockHeader {
    .byte_size = sizeof(world::SceneEnvironmentBlockHeader),
    .systems_count = static_cast<uint32_t>(environment_.size()),
  };
  for (const auto& record : environment_) {
    environment.byte_size += static_cast<uint32_t>(record.size());
  }
  Append(bytes, std::as_bytes(std::span(&environment, 1U)));
  for (const auto& record : environment_) {
    Append(bytes, record);
  }
  data::StampDescriptorHash(bytes);

  try {
    static_cast<void>(
      data::SceneAsset(data::AssetKey::FromVirtualPath("/" + name_ + ".oscene"),
        std::span<const std::byte>(bytes)));
  } catch (const std::exception& refused) {
    throw std::logic_error(
      "the reader refuses the scene '" + name_ + "': " + refused.what());
  }
  return bytes;
}

auto SceneBuilder::References() const -> data::AssetReferences
{
  auto references = content::import::AssetReferenceBuilder {};
  for (const auto& texture : textures_) {
    static_cast<void>(references.AddTexture(texture));
  }
  for (const auto& renderable : renderables_) {
    references.AddAsset(renderable.geometry_key, data::AssetType::kGeometry);
  }
  for (const auto& entry : overrides_) {
    const auto* property = data::schema::FindOverrideProperty(entry.id);
    if (property != nullptr
      && property->type == data::schema::ValueType::kAsset) {
      references.AddAsset(entry.value.as_asset, property->asset_type);
    }
  }
  for (const auto& instance : instances_) {
    references.AddAsset(instance.scene_key, data::AssetType::kScene);
  }
  for (const auto& context : contexts_) {
    references.AddAsset(
      context.context_key, data::AssetType::kInputMappingContext);
  }
  const auto add_param = [&references](const ScriptParam& param) -> void {
    if (const auto* asset = std::get_if<ScriptAssetValue>(&param.value)) {
      references.AddAsset(asset->key, asset->type);
    }
  };
  for (const auto& slot : slots_) {
    references.AddAsset(slot.script, data::AssetType::kScript);
    std::ranges::for_each(slot.params, add_param);
  }
  for (const auto& change : script_changes_) {
    add_param(change.param);
  }
  return std::move(references).Build();
}

auto SceneBuilder::Build(const data::AssetKey& key) const
  -> std::shared_ptr<data::SceneAsset>
{
  return std::make_shared<data::SceneAsset>(key, Bytes());
}

auto SceneBuilder::Holds(const SceneNode node) const noexcept -> bool
{
  return node.Owner() == owner_ && node.Index() < nodes_.size();
}

auto SceneBuilder::Require(const SceneNode node) const -> world::SceneNodeIndexT
{
  if (!Holds(node)) {
    throw std::invalid_argument("the node is not a node of '" + name_ + "'");
  }
  return node.Index();
}

auto SceneBuilder::DefaultPerspectiveCamera() -> world::PerspectiveCameraRecord
{
  return world::MakeDefaultRecord<world::PerspectiveCameraRecord>();
}

auto SceneBuilder::DefaultOrthographicCamera()
  -> world::OrthographicCameraRecord
{
  return world::MakeDefaultRecord<world::OrthographicCameraRecord>();
}

auto SceneBuilder::DefaultDirectionalLight() -> world::DirectionalLightRecord
{
  return world::MakeDefaultRecord<world::DirectionalLightRecord>();
}

auto SceneBuilder::DefaultPointLight() -> world::PointLightRecord
{
  return world::MakeDefaultRecord<world::PointLightRecord>();
}

auto SceneBuilder::DefaultSpotLight() -> world::SpotLightRecord
{
  return world::MakeDefaultRecord<world::SpotLightRecord>();
}

auto SceneBuilder::DefaultLocalFogVolume() -> world::LocalFogVolumeRecord
{
  return world::MakeDefaultRecord<world::LocalFogVolumeRecord>();
}

} // namespace oxygen::cooker::testing
