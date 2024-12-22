//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Base/logging.h"
#include "Oxygen/Base/win_errors.h"
#include "Oxygen/Renderers/Direct3d12/api_export.h"
#include "Oxygen/Renderers/Direct3d12/detail/resources.h"

namespace oxygen::renderer::d3d12 {

  OXYGEN_D3D12_API [[nodiscard]] ID3D12Device12* GetMainDevice();
  OXYGEN_D3D12_API [[nodiscard]] auto CurrentFrameIndex() -> size_t;

  constexpr struct
  {
    D3D12_HEAP_PROPERTIES default_hep_props
    {
      .Type = D3D12_HEAP_TYPE_DEFAULT,
      .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
      .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
      .CreationNodeMask = 0,
      .VisibleNodeMask = 0,
    };
  } kHeapProperties;

  inline auto ToNarrow(const std::wstring& wide_string) -> std::string
  {
    if (wide_string.empty()) return {};

    const int size_needed = WideCharToMultiByte(
      CP_UTF8,
      0,
      wide_string.data(),
      static_cast<int>(wide_string.size()),
      nullptr,
      0,
      nullptr,
      nullptr);
    std::string utf8_string(size_needed, 0);

    WideCharToMultiByte(
      CP_UTF8,
      0,
      wide_string.data(),
      static_cast<int>(wide_string.size()),
      utf8_string.data(),
      size_needed,
      nullptr,
      nullptr);

    return utf8_string;
  }

  inline void NameObject(ID3D12Object* const object, const std::wstring& name)
  {
#ifdef _DEBUG
    CheckResult(object->SetName(name.c_str()));
    LOG_F(1, "+D3D12 named object created: {}", ToNarrow(name));
#endif
  }

  template<typename T>
  void ObjectRelease(T*& resource) noexcept
  {
    if (resource) {
      resource->Release();
      resource = nullptr;
    }
  }

  template<typename T>
  void DeferredObjectRelease(T*& resource) noexcept
  {
    if (resource) {
      auto& instance = detail::DeferredResourceReleaseTracker::Instance();
      try {
        instance.DeferRelease(resource);
      }
      catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to defer release of resource: {}", e.what());
        resource->Release();
      }
      resource = nullptr;
    }
  }

}  // namespace oxygen::renderer::d3d12
