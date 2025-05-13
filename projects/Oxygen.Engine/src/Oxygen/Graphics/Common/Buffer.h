//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/NativeObject.h>

namespace oxygen::graphics {

//! Specifies the intended usage(s) of a buffer resource.
enum class BufferUsage : uint32_t {
    kNone = 0,
    kVertex = 1 << 0,
    kIndex = 1 << 1,
    kConstant = 1 << 2,
    kStorage = 1 << 3,
    kIndirect = 1 << 4,
    kAccelStruct = 1 << 5, //!< For ray tracing
};
OXYGEN_DEFINE_FLAGS_OPERATORS(BufferUsage)

//! Specifies the memory domain for a buffer resource.
enum class BufferMemory : uint32_t {
    kDeviceLocal = 0, //!< GPU only
    kUpload = 1, //!< CPU to GPU
    kReadBack = 2, //!< GPU to CPU
};

//! Describes the properties of a buffer resource.
struct BufferDesc {
    size_t size = 0;
    BufferUsage usage = BufferUsage::kNone;
    BufferMemory memory = BufferMemory::kDeviceLocal;
    uint32_t stride = 0; //!< For structured buffers

    std::string debug_name = "Buffer";
};

/*!
 Buffer is a backend-agnostic abstraction for GPU buffer resources.

 Key concepts:
  - Buffers are created and managed by the Renderer, and are intended to be used in a bindless, modern rendering pipeline.
  - Usage is specified via BufferUsage flags, allowing a single class to represent vertex, index, constant, storage, and other buffer types.
  - Memory domain is controlled by BufferMemory, supporting device-local, upload, and readback buffers for efficient data transfer.
  - The API is designed for per-frame resource management and is compatible with D3D12 and Vulkan, leveraging modern GPU features.

 Usage guidelines:
  - Always create buffers through the Renderer to ensure correct synchronization with the frame lifecycle and resource management.
  - Use Map/Unmap for CPU access to buffer memory, and Update for convenience when uploading data.
  - Query buffer properties via GetDesc, GetSize, GetUsage, and GetMemoryType.
  - Use GetNativeResource for backend interop, but prefer backend-agnostic APIs for most operations.

 Implementation rationale:
  - Unified buffer abstraction simplifies resource management and aligns with modern graphics APIs.
  - Enum flags and descriptors provide flexibility and extensibility for future buffer types and usages.
  - Consistent naming and API surface with Texture and other resource classes for ease of use and maintainability.
*/
class Buffer : public Composition, public Named {
public:
    explicit Buffer(std::string_view name)
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~Buffer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Buffer)
    OXYGEN_DEFAULT_MOVABLE(Buffer)

    //! Maps the buffer memory for CPU access.
    /*! \param offset Byte offset to start mapping.
        \param size Number of bytes to map (0 = entire buffer).
        \return Pointer to mapped memory.
    */
    virtual auto Map(size_t offset = 0, size_t size = 0) -> void* = 0;

    //! Unmaps the buffer memory from CPU access.
    virtual void Unmap() = 0;

    //! Updates the buffer contents from CPU memory.
    /*! \param data Pointer to source data.
        \param size Number of bytes to copy.
        \param offset Byte offset in the buffer to update.
    */
    virtual void Update(const void* data, size_t size, size_t offset = 0) = 0;

    //! Returns the size of the buffer in bytes.
    [[nodiscard]] virtual auto GetSize() const noexcept -> size_t = 0;

    //! Returns the usage flags of the buffer.
    [[nodiscard]] virtual auto GetUsage() const noexcept -> BufferUsage = 0;

    //! Returns the memory type of the buffer.
    [[nodiscard]] virtual auto GetMemoryType() const noexcept -> BufferMemory = 0;

    //! Returns true if the buffer is currently mapped.
    [[nodiscard]] virtual auto IsMapped() const noexcept -> bool = 0;

    //! Returns the buffer descriptor.
    [[nodiscard]] virtual auto GetDesc() const noexcept -> BufferDesc = 0;

    //! Returns the native backend resource handle.
    [[nodiscard]] virtual auto GetNativeResource() const -> NativeObject = 0;

    //! Returns the buffer's name.
    [[nodiscard]] auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    //! Sets the buffer's name.
    void SetName(std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }
};

} // namespace oxygen::graphics
