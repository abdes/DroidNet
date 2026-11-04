//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>

//! What tests read glTF documents with: inline buffers, and adapter sinks that
//! keep the work items.
namespace oxygen::content::import::test {

class GeometrySink final : public adapters::GeometryWorkItemSink {
public:
  GeometrySink() = default;
  ~GeometrySink() override = default;
  OXYGEN_MAKE_NON_COPYABLE(GeometrySink)
  OXYGEN_MAKE_NON_MOVABLE(GeometrySink)

  auto Consume(MeshBuildPipeline::WorkItem item) -> bool override
  {
    items.push_back(std::move(item));
    return true;
  }

  std::vector<MeshBuildPipeline::WorkItem> items;
};

class TextureSink final : public adapters::TextureWorkItemSink {
public:
  TextureSink() = default;
  ~TextureSink() override = default;
  OXYGEN_MAKE_NON_COPYABLE(TextureSink)
  OXYGEN_MAKE_NON_MOVABLE(TextureSink)

  auto Consume(TexturePipeline::WorkItem item) -> bool override
  {
    items.push_back(std::move(item));
    return true;
  }

  std::vector<TexturePipeline::WorkItem> items;
};

[[nodiscard]] inline auto Base64(const std::span<const unsigned char> bytes)
  -> std::string
{
  constexpr auto kAlphabet = std::string_view {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
  };
  auto out = std::string {};
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const auto remaining = bytes.size() - i;
    const auto b0 = static_cast<uint32_t>(bytes[i]);
    const auto b1 = remaining > 1 ? static_cast<uint32_t>(bytes[i + 1]) : 0U;
    const auto b2 = remaining > 2 ? static_cast<uint32_t>(bytes[i + 2]) : 0U;
    const auto triple = (b0 << 16U) | (b1 << 8U) | b2;
    out.push_back(kAlphabet.at((triple >> 18U) & 0x3FU));
    out.push_back(kAlphabet.at((triple >> 12U) & 0x3FU));
    out.push_back(remaining > 1 ? kAlphabet.at((triple >> 6U) & 0x3FU) : '=');
    out.push_back(remaining > 2 ? kAlphabet.at(triple & 0x3FU) : '=');
  }
  return out;
}

[[nodiscard]] inline auto DataUri(const std::span<const unsigned char> bytes)
  -> std::string
{
  return "data:application/octet-stream;base64," + Base64(bytes);
}

//! Appends `bytes` to `buffer` at a four-byte boundary; returns the offset.
inline auto AppendAligned(std::vector<unsigned char>& buffer,
  const std::span<const std::byte> bytes) -> std::size_t
{
  while (buffer.size() % 4U != 0U) {
    buffer.push_back(0);
  }
  const auto offset = buffer.size();
  for (const auto byte : bytes) {
    buffer.push_back(std::to_integer<unsigned char>(byte));
  }
  return offset;
}

//! What reading a document gives: its parse, its geometry build and the work
//! items that build emitted.
struct GltfRead final {
  adapters::GltfAdapter::ParseResult parse;
  adapters::WorkItemStreamResult build;
  std::vector<MeshBuildPipeline::WorkItem> items;
};

//! Parses `document` from memory and builds its geometry, if it parses.
[[nodiscard]] inline auto ReadGltfDocument(const std::string& document)
  -> GltfRead
{
  NamingService naming;
  auto input = adapters::AdapterInput {};
  input.source_id_prefix = "document.gltf";
  input.request.source_path = "document.gltf";
  input.naming_service = observer_ptr { &naming };
  auto adapter = adapters::GltfAdapter {};
  auto read = GltfRead {
    .parse = adapter.Parse(
      std::as_bytes(std::span(document.data(), document.size())), input),
    .build = {},
    .items = {},
  };
  if (!read.parse.success) {
    return read;
  }
  auto sink = GeometrySink {};
  read.build
    = adapter.BuildWorkItems(adapters::GeometryWorkTag {}, sink, input);
  if (read.build.success) {
    read.items = std::move(sink.items);
  }
  return read;
}

} // namespace oxygen::content::import::test
