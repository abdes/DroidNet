//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfDocumentChecks.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import::internal {

namespace {

  //! The shape of a run of elements: the first at `offset`, each `stride` from
  //! the last.
  struct ElementRun final {
    cgltf_size offset = 0;
    cgltf_size stride = 0;
    cgltf_size count = 0;
    cgltf_size element_size = 0;

    //! Whether the run lies inside `capacity` bytes. No sum here can wrap.
    [[nodiscard]] constexpr auto FitsIn(
      const cgltf_size capacity) const noexcept -> bool
    {
      if (count == 0U) {
        return true;
      }
      if (offset > capacity || element_size > capacity - offset) {
        return false;
      }
      const auto room = capacity - offset - element_size;
      return stride == 0U || count - 1U <= room / stride;
    }

    [[nodiscard]] auto Describe() const -> std::string
    {
      return std::to_string(count) + " elements of "
        + std::to_string(element_size) + " bytes, " + std::to_string(stride)
        + " apart, at offset " + std::to_string(offset);
    }
  };

  //! Reports a refused object and answers false, the check's result.
  auto Refuse(std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view source_id, std::string object_path,
    std::string message) -> bool
  {
    diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = std::string(import_codes::kGltfValidationFailed),
      .message = "The glTF document is invalid: " + std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::move(object_path),
    });
    return false;
  }

  [[nodiscard]] auto CheckBufferView(const cgltf_buffer_view& view,
    const std::string& path, const serio::ByteCount limit,
    const std::string_view source_id,
    std::vector<ImportDiagnostic>& diagnostics) -> bool
  {
    if (view.buffer == nullptr) {
      return Refuse(diagnostics, source_id, path, "it names no buffer");
    }
    const auto extent = ElementRun {
      .offset = view.offset,
      .stride = 0,
      .count = 1,
      .element_size = view.size,
    };
    if (!extent.FitsIn(view.buffer->size)) {
      return Refuse(diagnostics, source_id, path,
        "it takes " + std::to_string(view.size) + " bytes at offset "
          + std::to_string(view.offset) + " of a buffer of "
          + std::to_string(view.buffer->size));
    }
    if ((view.has_meshopt_compression != 0)
      && serio::ByteCount { view.size } > limit) {
      return Refuse(diagnostics, source_id, path,
        "it decodes to " + std::to_string(view.size)
          + " bytes, more than the source limit of "
          + std::to_string(limit.get()));
    }
    return true;
  }

  [[nodiscard]] auto CheckSparse(const cgltf_accessor& accessor,
    const cgltf_size element_size, const std::string& path,
    const std::string_view source_id,
    std::vector<ImportDiagnostic>& diagnostics) -> bool
  {
    const auto& sparse = accessor.sparse;
    if (sparse.indices_buffer_view == nullptr
      || sparse.values_buffer_view == nullptr) {
      return Refuse(diagnostics, source_id, path,
        "its sparse indices or values name no buffer view");
    }
    if (sparse.count > accessor.count) {
      return Refuse(diagnostics, source_id, path,
        "it replaces " + std::to_string(sparse.count) + " of its "
          + std::to_string(accessor.count) + " elements");
    }
    const auto index_size = cgltf_component_size(sparse.indices_component_type);
    const auto indices = ElementRun {
      .offset = sparse.indices_byte_offset,
      .stride = index_size,
      .count = sparse.count,
      .element_size = index_size,
    };
    if (!indices.FitsIn(sparse.indices_buffer_view->size)) {
      return Refuse(diagnostics, source_id, path,
        "its sparse indices, " + indices.Describe() + ", leave a view of "
          + std::to_string(sparse.indices_buffer_view->size) + " bytes");
    }
    // cgltf steps through the values by the accessor's stride.
    const auto values = ElementRun {
      .offset = sparse.values_byte_offset,
      .stride = accessor.stride,
      .count = sparse.count,
      .element_size = element_size,
    };
    if (!values.FitsIn(sparse.values_buffer_view->size)) {
      return Refuse(diagnostics, source_id, path,
        "its sparse values, " + values.Describe() + ", leave a view of "
          + std::to_string(sparse.values_buffer_view->size) + " bytes");
    }
    return true;
  }

  [[nodiscard]] auto CheckAccessor(const cgltf_accessor& accessor,
    const std::string& path, const serio::ByteCount limit,
    const std::string_view source_id,
    std::vector<ImportDiagnostic>& diagnostics) -> bool
  {
    if (accessor.count > std::numeric_limits<uint32_t>::max()) {
      return Refuse(diagnostics, source_id, path,
        "it has " + std::to_string(accessor.count)
          + " elements; a mesh indexes at most 2^32-1");
    }
    const auto element_size
      = cgltf_calc_size(accessor.type, accessor.component_type);
    const auto run = ElementRun {
      .offset = accessor.offset,
      .stride = accessor.stride,
      .count = accessor.count,
      .element_size = element_size,
    };
    if (accessor.buffer_view != nullptr) {
      if (!run.FitsIn(accessor.buffer_view->size)) {
        return Refuse(diagnostics, source_id, path,
          "it declares " + run.Describe() + ", which leave a view of "
            + std::to_string(accessor.buffer_view->size) + " bytes");
      }
    } else if (element_size != 0U
      && accessor.count > limit.get() / element_size) {
      return Refuse(diagnostics, source_id, path,
        "it has no buffer view and declares " + run.Describe()
          + ", more than the source limit of " + std::to_string(limit.get())
          + " bytes");
    }
    return (accessor.is_sparse == 0)
      || CheckSparse(accessor, element_size, path, source_id, diagnostics);
  }

  //! Replaces every byte `text` cannot read as UTF-8 with `_`; true if any was.
  auto RepairText(char* const text) -> bool
  {
    const auto length = std::char_traits<char>::length(text);
    const auto bytes = std::span(text, length);
    const auto view = std::string_view(text, length);
    if (string_utils::IsValidUtf8(view)) {
      return false;
    }
    constexpr auto kLongestSequence = std::size_t { 4 };
    auto at = std::size_t { 0 };
    while (at < length) {
      auto sequence = std::size_t { 0 };
      for (auto size = std::size_t { 1 };
        size <= kLongestSequence && at + size <= length && sequence == 0U;
        ++size) {
        if (string_utils::IsValidUtf8(view.substr(at, size))) {
          sequence = size;
        }
      }
      if (sequence == 0U) {
        bytes[at] = '_';
        sequence = 1U;
      }
      at += sequence;
    }
    return true;
  }

} // namespace

auto CheckDeclaredExtents(const cgltf_data& data, const serio::ByteCount limit,
  const std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
  -> bool
{
  for (const auto& [index, view] : std::views::enumerate(
         std::span(data.buffer_views, data.buffer_views_count))) {
    if (!CheckBufferView(view, "/bufferViews/" + std::to_string(index), limit,
          source_id, diagnostics)) {
      return false;
    }
  }
  for (const auto& [index, accessor] :
    std::views::enumerate(std::span(data.accessors, data.accessors_count))) {
    if (!CheckAccessor(accessor, "/accessors/" + std::to_string(index), limit,
          source_id, diagnostics)) {
      return false;
    }
  }
  return true;
}

auto NormalizeDocumentText(cgltf_data& data, const std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) -> void
{
  auto repaired = std::size_t { 0 };
  const auto repair = [&repaired](char* const text) -> void {
    if (text == nullptr) {
      return;
    }
    cgltf_decode_string(text);
    if (RepairText(text)) {
      ++repaired;
    }
  };
  const auto repair_names
    = [&repair](auto* const objects, const cgltf_size count) -> void {
    for (auto& object : std::span(objects, count)) {
      repair(object.name);
    }
  };
  repair(data.asset.copyright);
  repair(data.asset.generator);
  repair(data.asset.version);
  repair(data.asset.min_version);
  repair_names(data.meshes, data.meshes_count);
  repair_names(data.materials, data.materials_count);
  repair_names(data.accessors, data.accessors_count);
  repair_names(data.buffer_views, data.buffer_views_count);
  repair_names(data.buffers, data.buffers_count);
  repair_names(data.images, data.images_count);
  repair_names(data.textures, data.textures_count);
  repair_names(data.samplers, data.samplers_count);
  repair_names(data.skins, data.skins_count);
  repair_names(data.cameras, data.cameras_count);
  repair_names(data.lights, data.lights_count);
  repair_names(data.nodes, data.nodes_count);
  repair_names(data.scenes, data.scenes_count);
  repair_names(data.animations, data.animations_count);
  repair_names(data.variants, data.variants_count);
  for (auto& buffer : std::span(data.buffers, data.buffers_count)) {
    repair(buffer.uri);
  }
  for (auto& image : std::span(data.images, data.images_count)) {
    repair(image.uri);
    repair(image.mime_type);
  }
  for (auto* const name :
    std::span(data.extensions_used, data.extensions_used_count)) {
    repair(name);
  }
  for (auto* const name :
    std::span(data.extensions_required, data.extensions_required_count)) {
    repair(name);
  }
  if (repaired != 0U) {
    diagnostics.push_back({
      .severity = ImportSeverity::kWarning,
      .code = std::string(import_codes::kGltfTextNotUtf8),
      .message = std::to_string(repaired)
        + " strings of the glTF document are not UTF-8; their invalid bytes "
          "are replaced with '_'",
      .source_path = std::string(source_id),
      .object_path = {},
    });
  }
}

auto CheckNodeDepth(const cgltf_data& data, const std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  // A node's height is the most nodes on a path down from it, itself included;
  // zero means it is not yet measured.
  auto heights = std::vector<uint32_t>(data.nodes_count, 0U);
  auto on_path = std::vector<bool>(data.nodes_count, false);
  struct Frame final {
    cgltf_size node = 0;
    cgltf_size next_child = 0;
  };
  auto path = std::vector<Frame> {};
  const auto nodes = std::span(data.nodes, data.nodes_count);
  for (cgltf_size start = 0; start < data.nodes_count; ++start) {
    if (heights.at(start) != 0U) {
      continue;
    }
    path.push_back({ .node = start });
    on_path.at(start) = true;
    while (!path.empty()) {
      auto& frame = path.back();
      const auto& node = nodes[frame.node];
      const auto children = std::span(node.children, node.children_count);
      if (frame.next_child < children.size()) {
        const auto child
          = cgltf_node_index(&data, children[frame.next_child++]);
        if (on_path.at(child)) {
          return Refuse(diagnostics, source_id,
            "/nodes/" + std::to_string(child),
            "the node is its own descendant");
        }
        if (heights.at(child) == 0U) {
          on_path.at(child) = true;
          path.push_back({ .node = child });
        }
        continue;
      }
      auto below = uint32_t { 0 };
      for (const auto* child : children) {
        below = (std::max)(below, heights.at(cgltf_node_index(&data, child)));
      }
      const auto height = below + 1U;
      if (height > kMaxModelNodeDepth) {
        diagnostics.push_back({
          .severity = ImportSeverity::kError,
          .code = std::string(import_codes::kModelNodeDepthExceeded),
          .message = "The node hierarchy is deeper than "
            + std::to_string(kMaxModelNodeDepth) + " levels",
          .source_path = std::string(source_id),
          .object_path = "/nodes/" + std::to_string(frame.node),
        });
        return false;
      }
      heights.at(frame.node) = height;
      on_path.at(frame.node) = false;
      path.pop_back();
    }
  }
  return true;
}

} // namespace oxygen::content::import::internal
