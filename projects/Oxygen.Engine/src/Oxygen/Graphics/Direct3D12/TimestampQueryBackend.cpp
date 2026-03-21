//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/TimestampQueryBackend.h>

using oxygen::graphics::d3d12::TimestampQueryBackend;
using oxygen::windows::ThrowOnFailed;

TimestampQueryBackend::TimestampQueryBackend(
  const Graphics& graphics, const uint32_t initial_capacity_queries)
  : graphics_(graphics)
{
  if (initial_capacity_queries > 0U) {
    const auto ok = RecreateResources(initial_capacity_queries);
    CHECK_F(ok, "Failed to initialize D3D12 timestamp query backend");
  }
}

TimestampQueryBackend::~TimestampQueryBackend() { ReleaseResources(); }

auto TimestampQueryBackend::EnsureCapacity(const uint32_t required_query_count)
  -> bool
{
  if (required_query_count == 0U || required_query_count <= capacity_queries_) {
    return true;
  }

  return RecreateResources(required_query_count);
}

auto TimestampQueryBackend::WriteTimestamp(
  graphics::CommandRecorder& recorder, const uint32_t query_slot) -> bool
{
  if (query_heap_ == nullptr || query_slot >= capacity_queries_) {
    return false;
  }

  auto& d3d12_recorder = static_cast<CommandRecorder&>(recorder);
  auto* command_list = d3d12_recorder.GetD3D12CommandList();
  if (command_list == nullptr) {
    return false;
  }

  command_list->EndQuery(
    query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query_slot);
  return true;
}

auto TimestampQueryBackend::RecordResolve(graphics::CommandRecorder& recorder,
  const uint32_t used_query_slots) -> bool
{
  if (query_heap_ == nullptr || readback_resource_ == nullptr) {
    return false;
  }
  if (used_query_slots == 0U || used_query_slots > capacity_queries_) {
    return used_query_slots == 0U;
  }

  auto& d3d12_recorder = static_cast<CommandRecorder&>(recorder);
  auto* command_list = d3d12_recorder.GetD3D12CommandList();
  if (command_list == nullptr) {
    return false;
  }

  command_list->ResolveQueryData(query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
    0U, used_query_slots, readback_resource_.Get(), 0U);
  return true;
}

auto TimestampQueryBackend::GetResolvedTicks() const -> std::span<const uint64_t>
{
  if (mapped_ticks_ == nullptr || capacity_queries_ == 0U) {
    return {};
  }

  return { mapped_ticks_, capacity_queries_ };
}

auto TimestampQueryBackend::RecreateResources(
  const uint32_t capacity_queries) -> bool
{
  DCHECK_GT_F(capacity_queries, 0U);

  ReleaseResources();

  auto* device = graphics_.GetCurrentDevice();
  DCHECK_NOTNULL_F(device);

  D3D12_QUERY_HEAP_DESC query_heap_desc {};
  query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  query_heap_desc.Count = capacity_queries;
  query_heap_desc.NodeMask = 0U;

  try {
    ThrowOnFailed(device->CreateQueryHeap(
                    &query_heap_desc, IID_PPV_ARGS(&query_heap_)),
      "Failed to create D3D12 timestamp query heap");

    const auto buffer_size
      = static_cast<uint64_t>(capacity_queries) * sizeof(uint64_t);

    D3D12_HEAP_PROPERTIES heap_props {};
    heap_props.Type = D3D12_HEAP_TYPE_READBACK;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props.CreationNodeMask = 1U;
    heap_props.VisibleNodeMask = 1U;

    D3D12_RESOURCE_DESC resource_desc {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0U;
    resource_desc.Width = buffer_size;
    resource_desc.Height = 1U;
    resource_desc.DepthOrArraySize = 1U;
    resource_desc.MipLevels = 1U;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1U;
    resource_desc.SampleDesc.Quality = 0U;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowOnFailed(device->CreateCommittedResource(&heap_props,
                    D3D12_HEAP_FLAG_NONE, &resource_desc,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&readback_resource_)),
      "Failed to create D3D12 timestamp readback resource");

    void* mapped = nullptr;
    ThrowOnFailed(readback_resource_->Map(0U, nullptr, &mapped),
      "Failed to map D3D12 timestamp readback resource");
    mapped_ticks_ = static_cast<uint64_t*>(mapped);
    std::fill_n(mapped_ticks_, capacity_queries, uint64_t { 0 });

    capacity_queries_ = capacity_queries;
    return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "TimestampQueryBackend recreate failed: {}", ex.what());
    ReleaseResources();
    return false;
  }
}

auto TimestampQueryBackend::ReleaseResources() noexcept -> void
{
  if (readback_resource_ != nullptr && mapped_ticks_ != nullptr) {
    readback_resource_->Unmap(0U, nullptr);
  }
  mapped_ticks_ = nullptr;
  readback_resource_.Reset();
  query_heap_.Reset();
  capacity_queries_ = 0U;
}
