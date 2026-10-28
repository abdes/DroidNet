//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <meshoptimizer.h>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfMeshopt.h>

namespace oxygen::content::import::internal {
namespace {

  //! Decodes `compressed` into `decoded` as its mode says; false on failure.
  [[nodiscard]] auto Decode(const cgltf_meshopt_compression& compression,
    const std::span<const unsigned char> compressed, void* decoded) -> bool
  {
    switch (compression.mode) {
    case cgltf_meshopt_compression_mode_attributes:
      return meshopt_decodeVertexBuffer(decoded, compression.count,
               compression.stride, compressed.data(), compressed.size())
        == 0;
    case cgltf_meshopt_compression_mode_triangles:
      return meshopt_decodeIndexBuffer(decoded, compression.count,
               compression.stride, compressed.data(), compressed.size())
        == 0;
    case cgltf_meshopt_compression_mode_indices:
      return meshopt_decodeIndexSequence(decoded, compression.count,
               compression.stride, compressed.data(), compressed.size())
        == 0;
    default:
      return false;
    }
  }

  //! Reverses the filter `compression` names over `decoded`, in place.
  auto Unfilter(const cgltf_meshopt_compression& compression, void* decoded)
    -> void
  {
    switch (compression.filter) {
    case cgltf_meshopt_compression_filter_octahedral:
      meshopt_decodeFilterOct(decoded, compression.count, compression.stride);
      break;
    case cgltf_meshopt_compression_filter_quaternion:
      meshopt_decodeFilterQuat(decoded, compression.count, compression.stride);
      break;
    case cgltf_meshopt_compression_filter_exponential:
      meshopt_decodeFilterExp(decoded, compression.count, compression.stride);
      break;
    default:
      break;
    }
  }

} // namespace

auto DecodeMeshoptBufferViews(cgltf_data& data,
  const std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
  -> bool
{
  const auto views = std::span(data.buffer_views, data.buffer_views_count);
  for (auto&& [index, view] : std::views::enumerate(views)) {
    if (view.has_meshopt_compression == 0) {
      continue;
    }
    const auto fail = [&](const std::string_view reason) -> bool {
      diagnostics.push_back({
        .severity = ImportSeverity::kError,
        .code = "gltf.meshopt_decode_failed",
        .message = "Buffer view " + std::to_string(index)
          + " is compressed with EXT_meshopt_compression and "
          + std::string(reason),
        .source_path = std::string(source_id),
        .object_path = "/bufferViews/" + std::to_string(index),
      });
      return false;
    };

    const auto& compression = view.meshopt_compression;
    const auto* buffer = compression.buffer;
    if (buffer == nullptr || buffer->data == nullptr) {
      return fail("its compressed buffer is not loaded");
    }
    const auto bytes = std::span(
      static_cast<const unsigned char*>(buffer->data), buffer->size);
    if (compression.offset > bytes.size()
      || compression.size > bytes.size() - compression.offset) {
      return fail("its compressed bytes lie outside their buffer");
    }

    // cgltf_validate matched stride times count to the view's size, but that
    // product can wrap; the decoders write the unwrapped length.
    if (compression.stride == 0
      || compression.count
        > std::numeric_limits<std::size_t>::max() / compression.stride) {
      return fail("its stride and count do not describe a buffer");
    }
    const auto decoded_size = compression.stride * compression.count;
    void* decoded = data.memory.alloc_func(data.memory.user_data, decoded_size);
    if (decoded == nullptr) {
      return fail("its decoded bytes could not be allocated");
    }
    // The document owns the bytes from here on; cgltf_free releases them.
    view.data = decoded;
    if (!Decode(compression,
          bytes.subspan(compression.offset, compression.size), decoded)) {
      return fail("its bytes do not decode");
    }
    Unfilter(compression, decoded);
  }
  return true;
}

} // namespace oxygen::content::import::internal
