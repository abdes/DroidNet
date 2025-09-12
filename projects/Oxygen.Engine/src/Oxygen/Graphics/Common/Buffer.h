//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Concepts.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics {

class DescriptorHandle;

//! Specifies the intended usage(s) of a buffer resource.
enum class BufferUsage : uint32_t { // NOLINT(performance-enum-size)
  kNone = 0,
  kVertex = 1 << 0,
  kIndex = 1 << 1,
  kConstant = 1 << 2,
  kStorage = 1 << 3,
  kIndirect = 1 << 4,
  kRayTracingAccelStructures = 1 << 5, //!< For ray tracing
};
OXYGEN_DEFINE_FLAGS_OPERATORS(BufferUsage)

//! Specifies the memory domain for a buffer resource.
enum class BufferMemory : uint32_t { // NOLINT(performance-enum-size)
  kDeviceLocal = 0, //!< GPU only
  kUpload = 1, //!< CPU to GPU
  kReadBack = 2, //!< GPU to CPU
};

//! Describes the properties of a buffer resource.
struct BufferDesc {
  uint64_t size_bytes = 0;
  BufferUsage usage = BufferUsage::kNone;
  BufferMemory memory = BufferMemory::kDeviceLocal;

  std::string debug_name = "Buffer";
};

// --- BufferRange definition ---
struct BufferRange {
  uint64_t offset_bytes = 0;
  uint64_t size_bytes = ~0ull;

  BufferRange() = default;

  BufferRange(const uint64_t _offset_bytes, const uint64_t _size_bytes)
    : offset_bytes(_offset_bytes)
    , size_bytes(_size_bytes)
  {
  }

  [[nodiscard]] auto Resolve(const BufferDesc& desc) const -> BufferRange
  {
    BufferRange result;
    result.offset_bytes = (std::min)(offset_bytes, desc.size_bytes);
    if (size_bytes == 0) {
      result.size_bytes = desc.size_bytes - result.offset_bytes;
    } else {
      result.size_bytes
        = (std::min)(size_bytes, desc.size_bytes - result.offset_bytes);
    }
    return result;
  }

  [[nodiscard]] constexpr auto IsEntireBuffer(const BufferDesc& desc) const
    -> bool
  {
    return (offset_bytes == 0)
      && (size_bytes == ~0ull || size_bytes == desc.size_bytes);
  }

  constexpr auto operator==(const BufferRange& other) const -> bool
  {
    return offset_bytes == other.offset_bytes && size_bytes == other.size_bytes;
  }
};

struct BufferViewDescription {
  ResourceViewType view_type { ResourceViewType::kConstantBuffer };
  DescriptorVisibility visibility { DescriptorVisibility::kShaderVisible };
  Format format { Format::kUnknown };
  BufferRange range {};
  uint32_t stride = 0;
  auto operator==(const BufferViewDescription&) const -> bool = default;

  // Returns true if this view describes a typed buffer (format != kUnknown)
  [[nodiscard]] auto IsTypedBuffer() const noexcept -> bool
  {
    return format != Format::kUnknown;
  }

  // Returns true if this view describes a structured buffer (stride != 0)
  [[nodiscard]] auto IsStructuredBuffer() const noexcept -> bool
  {
    return stride != 0;
  }

  // Returns true if this view describes a raw buffer (format == kUnknown &&
  // stride == 0)
  [[nodiscard]] auto IsRawBuffer() const noexcept -> bool
  {
    return format == Format::kUnknown && stride == 0;
  }
};

} // namespace oxygen::graphics

// Hash specialization for BufferViewDescription.
template <> struct std::hash<oxygen::graphics::BufferViewDescription> {
  auto operator()(
    const oxygen::graphics::BufferViewDescription& s) const noexcept
    -> std::size_t
  {
    size_t hash = 0;
    oxygen::HashCombine(hash, s.view_type);
    oxygen::HashCombine(hash, s.visibility);
    oxygen::HashCombine(hash, s.format);
    oxygen::HashCombine(hash, s.range.offset_bytes);
    oxygen::HashCombine(hash, s.range.size_bytes);
    oxygen::HashCombine(hash, s.stride);
    return hash;
  }
};

namespace oxygen::graphics {

/*!
 Buffer is a backend-agnostic abstraction for GPU buffer resources.

 Key concepts:
  - Buffers are created and managed by the Renderer, and are intended to be used
    in a bindless, modern rendering pipeline.
  - Usage is specified via BufferUsage flags, allowing a single class to
    represent vertex, index, constant, storage, and other buffer types.
  - Memory domain is controlled by BufferMemory, supporting device-local,
    upload, and read-back buffers for efficient data transfer.
  - The API is designed for per-frame resource management.
*/
class Buffer : public Composition, public Named {
public:
  using ViewDescriptionT = BufferViewDescription;

  explicit Buffer(std::string_view name) { AddComponent<ObjectMetadata>(name); }

  ~Buffer() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Buffer)
  OXYGEN_DEFAULT_MOVABLE(Buffer)

  //! Returns the buffer descriptor.
  OXGN_GFX_NDAPI virtual auto GetDescriptor() const noexcept -> BufferDesc = 0;

  //! Returns the native backend resource handle.
  OXGN_GFX_NDAPI virtual auto GetNativeResource() const -> NativeResource = 0;

  OXGN_GFX_NDAPI virtual auto GetNativeView(const DescriptorHandle& view_handle,
    const BufferViewDescription& view_desc) const -> NativeView;

  //! Maps the buffer memory for CPU access.
  /*! \param offset Byte offset to start mapping.
      \param size Number of bytes to map (0 = entire buffer).
      \return Pointer to mapped memory.
  */
  virtual auto Map(uint64_t offset = 0, uint64_t size = 0) -> void* = 0;

  //! Un-maps the buffer memory from CPU access.
  virtual auto UnMap() -> void = 0;

  //! Updates the buffer contents from CPU memory.
  /*! \param data Pointer to source data.
      \param size Number of bytes to copy.
      \param offset Byte offset in the buffer to update.
  */
  virtual auto Update(const void* data, uint64_t size, uint64_t offset = 0)
    -> void
    = 0;

  //! Returns the size of the buffer in bytes.
  [[nodiscard]] virtual auto GetSize() const noexcept -> uint64_t = 0;

  //! Returns the usage flags of the buffer.
  [[nodiscard]] virtual auto GetUsage() const noexcept -> BufferUsage = 0;

  //! Returns the memory type of the buffer.
  [[nodiscard]] virtual auto GetMemoryType() const noexcept -> BufferMemory = 0;

  //! Returns true if the buffer is currently mapped.
  [[nodiscard]] virtual auto IsMapped() const noexcept -> bool = 0;

  //! Returns the buffer's name.
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return GetComponent<ObjectMetadata>().GetName();
  }

  //! Sets the buffer's name.
  auto SetName(const std::string_view name) noexcept -> void override
  {
    GetComponent<ObjectMetadata>().SetName(name);
  }

  /// Returns the GPU virtual address of the buffer, if supported by the API
  [[nodiscard]] virtual auto GetGPUVirtualAddress() const -> uint64_t = 0;

protected:
  /**
   * @brief Get a constant buffer view (CBV) for this buffer
   *
   * Creates or returns a cached constant buffer view for the specified range of
   * this buffer. The returned view is suitable for binding as a constant buffer
   * in shader programs.
   *
   * @param range The range of the buffer to view. Defaults to the entire
   * buffer.
   * @return NativeView The constant buffer view as a native object
   */
  [[nodiscard]] virtual auto CreateConstantBufferView(
    const DescriptorHandle& view_handle, const BufferRange& range = {}) const
    -> NativeView
    = 0;

  //! Returns a shader resource view (SRV) for this buffer.
  [[nodiscard]] virtual auto CreateShaderResourceView(
    const DescriptorHandle& view_handle, Format format, BufferRange range = {},
    uint32_t stride = 0) const -> NativeView
    = 0;

  //! Returns an unordered access view (UAV) for this buffer.
  [[nodiscard]] virtual auto CreateUnorderedAccessView(
    const DescriptorHandle& view_handle, Format format, BufferRange range = {},
    uint32_t stride = 0) const -> NativeView
    = 0;
};

// Ensure Buffer satisfies ResourceWithViews
static_assert(oxygen::graphics::ResourceWithViews<Buffer>,
  "Buffer must satisfy ResourceWithViews");

} // namespace oxygen::graphics
