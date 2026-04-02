//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class DescriptorAllocator;

enum class DescriptorAllocationKind : uint8_t {
  kInvalid = 0,
  kRaw,
  kBindless,
};

//! Unified descriptor allocation handle used by both raw and bindless paths.
/*!
 Carries the allocator back-reference plus the stable slot identity for a
 descriptor allocation. Raw allocations model explicit descriptor heap entries
 (RTV/DSV or ad-hoc shader-visible descriptors). Bindless allocations model
 domain-owned slots keyed by a generated `DomainToken`.

 The handle is move-only and follows RAII semantics: destroying a valid handle
 returns it to the originating allocator.
*/
class DescriptorAllocationHandle {
  friend class DescriptorAllocator;

public:
  static constexpr uint32_t kBindlessDomainShift = 20U;
  static constexpr uint32_t kBindlessLocalSlotMask
    = (1U << kBindlessDomainShift) - 1U;

  DescriptorAllocationHandle() noexcept = default;
  OXGN_GFX_API ~DescriptorAllocationHandle();

  OXYGEN_MAKE_NON_COPYABLE(DescriptorAllocationHandle)

  OXGN_GFX_API DescriptorAllocationHandle(
    DescriptorAllocationHandle&& other) noexcept;
  OXGN_GFX_API auto operator=(DescriptorAllocationHandle&& other) noexcept
    -> DescriptorAllocationHandle&;

  [[nodiscard]] constexpr auto operator==(
    const DescriptorAllocationHandle& other) const noexcept
  {
    return allocator_ == other.allocator_ && handle_ == other.handle_
      && view_type_ == other.view_type_ && visibility_ == other.visibility_
      && kind_ == other.kind_ && domain_ == other.domain_;
  }

  [[nodiscard]] constexpr auto operator!=(
    const DescriptorAllocationHandle& other) const noexcept
  {
    return !(*this == other);
  }

  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    const auto properly_allocated
      = allocator_ != nullptr && handle_ != kInvalidBindlessHeapIndex;
    assert(!properly_allocated || oxygen::graphics::IsValid(view_type_));
    assert(!properly_allocated || oxygen::graphics::IsValid(visibility_));
    return properly_allocated;
  }

  [[nodiscard]] constexpr auto IsBindless() const noexcept
  {
    return kind_ == DescriptorAllocationKind::kBindless;
  }

  [[nodiscard]] constexpr auto IsRaw() const noexcept
  {
    return kind_ == DescriptorAllocationKind::kRaw;
  }

  [[nodiscard]] constexpr auto GetKind() const noexcept { return kind_; }

  [[nodiscard]] constexpr auto GetBindlessHandle() const noexcept
  {
    return handle_;
  }

  [[nodiscard]] constexpr auto GetViewType() const noexcept
  {
    return view_type_;
  }

  [[nodiscard]] constexpr auto GetVisibility() const noexcept
  {
    return visibility_;
  }

  [[nodiscard]] constexpr auto GetAllocator() const noexcept
  {
    return allocator_;
  }

  [[nodiscard]] constexpr auto GetDomain() const noexcept
    -> bindless::DomainToken
  {
    return domain_;
  }

  [[nodiscard]] constexpr auto GetLocalSlot() const noexcept -> uint32_t
  {
    if (!IsBindless()) {
      return handle_.get();
    }
    return handle_.get() & kBindlessLocalSlotMask;
  }

  [[nodiscard]] static constexpr auto PackBindlessSlot(
    bindless::DomainToken domain, const uint32_t local_slot) noexcept
    -> bindless::HeapIndex
  {
    return bindless::HeapIndex {
      (static_cast<uint32_t>(domain.get()) << kBindlessDomainShift)
        | (local_slot & kBindlessLocalSlotMask),
    };
  }

  OXGN_GFX_API auto Release() noexcept -> void;
  OXGN_GFX_API auto Invalidate() noexcept -> void;

protected:
  OXGN_GFX_API DescriptorAllocationHandle(bindless::HeapIndex index,
    ResourceViewType view_type, DescriptorVisibility visibility,
    DescriptorAllocationKind kind,
    bindless::DomainToken domain
    = bindless::generated::kInvalidDomainToken) noexcept;

  OXGN_GFX_API DescriptorAllocationHandle(DescriptorAllocator* allocator,
    bindless::HeapIndex index, ResourceViewType view_type,
    DescriptorVisibility visibility, DescriptorAllocationKind kind,
    bindless::DomainToken domain
    = bindless::generated::kInvalidDomainToken) noexcept;

private:
  OXGN_GFX_API auto InvalidateInternal(bool moved) noexcept -> void;

  DescriptorAllocator* allocator_ { nullptr };
  bindless::HeapIndex handle_ { kInvalidBindlessHeapIndex };
  ResourceViewType view_type_ { ResourceViewType::kNone };
  DescriptorVisibility visibility_ { DescriptorVisibility::kNone };
  DescriptorAllocationKind kind_ { DescriptorAllocationKind::kInvalid };
  bindless::DomainToken domain_ { bindless::generated::kInvalidDomainToken };
};

using RawDescriptorHandle = DescriptorAllocationHandle;
using BindlessHandle = DescriptorAllocationHandle;

OXGN_GFX_API auto to_string(const DescriptorAllocationHandle& handle)
  -> std::string;

} // namespace oxygen::graphics
