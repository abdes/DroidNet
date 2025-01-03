//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinDisposable.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Graphics/Direct3d12/D3D12MemAlloc.h"
#include "Oxygen/Graphics/Direct3d12/D3DResource.h"
#include "Oxygen/Graphics/Direct3d12/Graphics.h"

namespace oxygen::renderer::d3d12 {

/**
 * Buffer's usage flags. The buffer usage must be determined upon creation.
 */
enum class BufferUsageFlags : uint8_t {
  kNone = OXYGEN_FLAG(0),

  kIndexBuffer = OXYGEN_FLAG(1),
  kVertexBuffer = OXYGEN_FLAG(2),
  kConstantBuffer = OXYGEN_FLAG(3),
  kReadonlyStruct = OXYGEN_FLAG(4),
  kWritableStruct = OXYGEN_FLAG(5),
  kReadonlyBuffer = OXYGEN_FLAG(6),
  kWritableBuffer = OXYGEN_FLAG(7),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(BufferUsageFlags);

/**
 * Description of renderer's buffer
 */
struct BufferDesc : public CommonResourceDesc {
  // buffer access mode
  ResourceAccessMode mode { ResourceAccessMode::kImmutable };

  // buffer usage flags
  BufferUsageFlags usage { BufferUsageFlags::kNone };

  // buffer size in bytes
  uint32_t size { 0u };

  // structure size in bytes (only applicable for structured buffers)
  uint32_t struct_size = { 0u };
};

struct BufferInitInfo {
  D3D12MA::ALLOCATION_DESC alloc_desc;
  D3D12_RESOURCE_DESC resource_desc;
  D3D12_RESOURCE_STATES initial_state;
  uint64_t size_in_bytes;
};

class Buffer
  : public D3DResource,
    public Mixin<Buffer, Curry<MixinNamed, const char*>::mixin, MixinDisposable, MixinInitialize>
{
 public:
  Buffer()
    : Mixin("Buffer")
  {
  }
  ~Buffer() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Buffer);
  OXYGEN_MAKE_NON_MOVEABLE(Buffer);

  [[nodiscard]] auto GetResource() const -> ID3D12Resource* override { return resource_; }

 private:
  void OnInitialize(const BufferInitInfo& init_info)
  {
    if (this->self().ShouldRelease()) {
      const auto msg = fmt::format("{} OnInitialize() called twice without calling Release()", this->self().ObjectName());
      LOG_F(ERROR, "{}", msg);
      throw std::runtime_error(msg);
    }
    try {
      // Create the buffer resource using D3D12MemAlloc
      D3D12MA::Allocation* allocation;
      HRESULT hr = graphics::d3d12::detail::GetAllocator().CreateResource(
        &init_info.alloc_desc,
        &init_info.resource_desc,
        init_info.initial_state,
        nullptr,
        &allocation,
        IID_PPV_ARGS(&resource_));

      if (FAILED(hr)) {
        throw std::runtime_error("Failed to create buffer resource");
      }

      allocation_ = allocation;

      /*InitializeTexture(init_info);*/
      this->self().ShouldRelease(true);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to initialize {}: {}", this->self().ObjectName(), e.what());
      throw;
    }
  }
  template <typename Base, typename... CtorArgs>
  friend class MixinInitialize; //< Allow access to OnInitialize.

  void OnRelease() noexcept
  {
    /*ReleaseTexture();*/
    this->self().IsInitialized(false);
  }
  template <typename Base>
  friend class MixinDisposable; //< Allow access to OnRelease.

 private:
  D3D12MA::Allocation* allocation_ { nullptr };
  ID3D12Resource* resource_ { nullptr };
};

} // namespace oxygen::renderer::d3d12
