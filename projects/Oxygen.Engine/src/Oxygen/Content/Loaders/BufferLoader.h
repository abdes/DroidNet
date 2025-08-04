//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <Oxygen/Content/Loaders/Helpers.h>

namespace oxygen::content::loaders {

//! Loads a buffer resource from a PAK file stream.
inline auto LoadBufferResource(LoaderContext context)
  -> std::unique_ptr<data::BufferResource>
{
  LOG_SCOPE_F(1, "Load Buffer Resource");
  LOG_F(2, "offline mode   : {}", context.offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  using data::pak::BufferResourceDesc;

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading buffer resource ({}): {}", field,
          result.error().message()));
    }
  };

  // Read BufferResourceDesc from the stream
  auto pack = reader.ScopedAlignment(1);
  auto result = reader.Read<BufferResourceDesc>();
  check_result(result, "BufferResourceDesc");
  const auto& desc = result.value();

  auto buf_format = static_cast<Format>(desc.element_format);
  auto flags = static_cast<data::BufferResource::UsageFlags>(desc.usage_flags);
  LOG_F(1, "data offset    : {}", desc.data_offset);
  LOG_F(1, "data size      : {}", desc.size_bytes);
  LOG_F(2, "element format : {}", nostd::to_string(buf_format));
  LOG_F(2, "usage flags    : {}", nostd::to_string(flags));
  LOG_F(2, "element stride : {}", desc.element_stride);

  std::vector<uint8_t> data_buffer(desc.size_bytes);
  if (desc.size_bytes > 0) {
    constexpr std::size_t buf_index
      = IndexOf<data::BufferResource, ResourceTypeList>::value;
    auto& data_reader = *std::get<buf_index>(context.data_readers);

    check_result(data_reader.Seek(desc.data_offset), "Buffer Data");
    // Create a span of std::byte over the same memory
    std::span<std::byte> byte_view(
      reinterpret_cast<std::byte*>(data_buffer.data()), data_buffer.size());
    auto data_result = data_reader.ReadBlobInto(byte_view);
    check_result(data_result, "Buffer Data");
  }

  return std::make_unique<data::BufferResource>(desc, std::move(data_buffer));
}

static_assert(oxygen::content::LoadFunction<decltype(LoadBufferResource)>);

//! Unload function for BufferResource.
inline auto UnloadBufferResource(
  std::shared_ptr<data::BufferResource> /*resource*/, AssetLoader& /*loader*/,
  bool offline) noexcept -> void
{
  if (offline) {
    return;
  }
  // TODO: cleanup GPU resources for the buffer.
  (void)0; // Placeholder for future GPU resource cleanup
}

static_assert(oxygen::content::UnloadFunction<decltype(UnloadBufferResource),
  data::BufferResource>);

} // namespace oxygen::content::loaders
