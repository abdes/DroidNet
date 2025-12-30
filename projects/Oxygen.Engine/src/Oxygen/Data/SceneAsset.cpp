//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/SceneAsset.h>

#include <limits>

namespace oxygen::data {

SceneAsset::SceneAsset(AssetKey key, std::span<const std::byte> data)
  : Asset(key)
  , data_(data)
{
  ParseAndValidate();
}

SceneAsset::SceneAsset(AssetKey key, std::vector<std::byte> data)
  : Asset(key)
  , owned_data_(std::make_shared<std::vector<std::byte>>(std::move(data)))
  , data_(owned_data_->data(), owned_data_->size())
{
  ParseAndValidate();
}

auto SceneAsset::GetNodes() const noexcept -> std::span<const pak::NodeRecord>
{
  return { nodes_ptr_, node_count_ };
}

auto SceneAsset::GetNode(pak::SceneNodeIndexT index) const noexcept
  -> const pak::NodeRecord&
{
  DCHECK_LT_F(index, node_count_);
  return nodes_ptr_[index];
}

auto SceneAsset::GetNodeName(const pak::NodeRecord& node) const noexcept
  -> std::string_view
{
  if (node.scene_name_offset >= string_table_size_) {
    return {};
  }

  const char* begin = string_table_ptr_ + node.scene_name_offset;
  const char* end = string_table_ptr_ + string_table_size_;
  const auto it = std::find(begin, end, '\0');
  return { begin, static_cast<size_t>(it - begin) };
}

auto SceneAsset::GetRootNode() const noexcept -> const pak::NodeRecord&
{
  DCHECK_GT_F(node_count_, 0);
  return nodes_ptr_[0];
}

auto SceneAsset::ParseAndValidate() -> void
{
  if (data_.size() < sizeof(pak::SceneAssetDesc)) {
    throw std::runtime_error("SceneAsset data too small for header");
  }

  std::memcpy(&desc_, data_.data(), sizeof(pak::SceneAssetDesc));

  auto range_ok
    = [](const size_t offset, const size_t size, const size_t total) {
        return offset <= total && size <= (total - offset);
      };

  // Validate Node Table
  if (desc_.nodes.count > 0) {
    const size_t nodes_bytes
      = static_cast<size_t>(desc_.nodes.count) * sizeof(pak::NodeRecord);
    if (!range_ok(desc_.nodes.offset, nodes_bytes, data_.size())) {
      throw std::runtime_error("SceneAsset node table out of bounds");
    }
    if (desc_.nodes.entry_size != sizeof(pak::NodeRecord)) {
      throw std::runtime_error("SceneAsset node record size mismatch");
    }
  }

  // Validate String Table
  if (desc_.scene_strings.size > 0) {
    if (!range_ok(
          desc_.scene_strings.offset, desc_.scene_strings.size, data_.size())) {
      throw std::runtime_error("SceneAsset string table out of bounds");
    }

    // Minimal runtime-safety invariant: offset 0 must refer to empty string.
    const auto bytes
      = data_.subspan(desc_.scene_strings.offset, desc_.scene_strings.size);
    if (!bytes.empty() && bytes[0] != std::byte { 0 }) {
      throw std::runtime_error(
        "SceneAsset string table must start with a NUL byte");
    }
  }

  // Validate Component Directory
  if (desc_.component_table_count > 0) {
    const size_t dir_bytes = static_cast<size_t>(desc_.component_table_count)
      * sizeof(pak::SceneComponentTableDesc);
    if (!range_ok(
          desc_.component_table_directory_offset, dir_bytes, data_.size())) {
      throw std::runtime_error("SceneAsset component directory out of bounds");
    }

    const auto dir_span
      = data_.subspan(desc_.component_table_directory_offset, dir_bytes);

    component_tables_.clear();
    component_tables_.reserve(desc_.component_table_count);
    for (uint32_t i = 0; i < desc_.component_table_count; ++i) {
      const auto entry_bytes = dir_span.subspan(
        static_cast<size_t>(i) * sizeof(pak::SceneComponentTableDesc),
        sizeof(pak::SceneComponentTableDesc));
      pak::SceneComponentTableDesc entry {};
      std::memcpy(&entry, entry_bytes.data(), sizeof(entry));

      if (entry.table.count == 0) {
        continue;
      }

      const size_t table_bytes
        = static_cast<size_t>(entry.table.count) * entry.table.entry_size;
      if (!range_ok(entry.table.offset, table_bytes, data_.size())) {
        throw std::runtime_error("SceneAsset component table out of bounds");
      }

      const auto type = static_cast<ComponentType>(entry.component_type);
      if (type == ComponentType::kRenderable
        && entry.table.entry_size != sizeof(pak::RenderableRecord)) {
        throw std::runtime_error("SceneAsset renderable record size mismatch");
      }
      if (type == ComponentType::kPerspectiveCamera
        && entry.table.entry_size != sizeof(pak::PerspectiveCameraRecord)) {
        throw std::runtime_error(
          "SceneAsset perspective camera record size mismatch");
      }
      if (type == ComponentType::kOrthographicCamera
        && entry.table.entry_size != sizeof(pak::OrthographicCameraRecord)) {
        throw std::runtime_error(
          "SceneAsset orthographic camera record size mismatch");
      }

      component_tables_.push_back({ .type = type,
        .offset = entry.table.offset,
        .count = entry.table.count,
        .entry_size = entry.table.entry_size });
    }
  }

  // Cache node and string table views.
  node_count_ = desc_.nodes.count;
  nodes_ptr_ = node_count_ == 0 ? nullptr
                                : std::bit_cast<const pak::NodeRecord*>(
                                    data_.subspan(desc_.nodes.offset).data());

  string_table_size_ = desc_.scene_strings.size;
  string_table_ptr_ = string_table_size_ == 0
    ? nullptr
    : std::bit_cast<const char*>(
        data_.subspan(desc_.scene_strings.offset).data());
}

} // namespace oxygen::data
