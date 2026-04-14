//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Tracy/D3D12.h>

#include <cstring>
#include <memory>

#include <tracy/TracyD3D12.hpp>

namespace oxygen::tracy::d3d12 {
namespace {

  auto ToContext(const ContextHandle context) -> TracyD3D12Ctx
  {
    return static_cast<TracyD3D12Ctx>(context);
  }

} // namespace

auto CreateContext(ID3D12Device* device, ID3D12CommandQueue* queue)
  -> ContextHandle
{
  if (device == nullptr || queue == nullptr) {
    return nullptr;
  }
  return ::tracy::CreateD3D12Context(device, queue);
}

auto DestroyContext(const ContextHandle context) -> void
{
  if (context != nullptr) {
    ::tracy::DestroyD3D12Context(ToContext(context));
  }
}

auto AdvanceContextFrame(const ContextHandle context) -> void
{
  if (context != nullptr) {
    ToContext(context)->NewFrame();
  }
}

auto CollectContext(const ContextHandle context) -> void
{
  if (context != nullptr) {
    ToContext(context)->Collect();
  }
}

auto NameContext(const ContextHandle context, const std::string_view name)
  -> void
{
  if (context != nullptr) {
    ToContext(context)->Name(name.data(), static_cast<uint16_t>(name.size()));
  }
}

auto BeginZone(const std::span<std::byte> storage, const ContextHandle context,
  ID3D12GraphicsCommandList* command_list, const std::source_location callsite,
  const std::string_view name) -> bool
{
  using ::tracy::D3D12ZoneScope;

  if (context == nullptr || command_list == nullptr
    || storage.size_bytes() < sizeof(D3D12ZoneScope)) {
    return false;
  }

  std::construct_at(reinterpret_cast<D3D12ZoneScope*>(storage.data()),
    ToContext(context), static_cast<uint32_t>(callsite.line()),
    callsite.file_name(), std::strlen(callsite.file_name()),
    callsite.function_name(), std::strlen(callsite.function_name()),
    name.data(), name.size(), command_list, true);
  return true;
}

auto EndZone(const std::span<std::byte> storage) -> void
{
  using ::tracy::D3D12ZoneScope;

  if (storage.size_bytes() < sizeof(D3D12ZoneScope)) {
    return;
  }

  std::destroy_at(reinterpret_cast<D3D12ZoneScope*>(storage.data()));
}

} // namespace oxygen::tracy::d3d12
