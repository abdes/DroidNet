//===-----------------------------------------------------------oxygen::bindless::HeapIndex-----------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Headless/Bindless/AllocationStrategy.h>

namespace oxygen::graphics::headless::bindless {

AllocationStrategy::AllocationStrategy()
{
  // Compile-time heap configuration: name, cpu capacity, shader-visible
  // capacity. Use a constexpr array so the registration order is stable and
  // repeatable across runs.
  struct HeapConfig {
    const char* name;
    uint32_t cpu_capacity;
    uint32_t shader_capacity;
  };

  // Explicit fixed-size array makes the compile-time size obvious to readers
  // and tools.
  constexpr std::array heap_configs = {
    // clang-format off
    HeapConfig{.name="Texture_SRV", .cpu_capacity=32768u, .shader_capacity=32768u},
    HeapConfig{.name="Texture_UAV", .cpu_capacity=16384u, .shader_capacity=16384u},
    HeapConfig{.name="TypedBuffer_SRV", .cpu_capacity=8192u, .shader_capacity=8192u},
    HeapConfig{.name="TypedBuffer_UAV", .cpu_capacity=8192u, .shader_capacity=8192u},
    HeapConfig{.name="StructuredBuffer_SRV", .cpu_capacity=8192u, .shader_capacity=8192u},
    HeapConfig{.name="StructuredBuffer_UAV", .cpu_capacity=8192u, .shader_capacity=8192u},
    HeapConfig{.name="RawBuffer_SRV", .cpu_capacity=8192u, .shader_capacity=8192u},
    HeapConfig{.name="RawBuffer_UAV", .cpu_capacity=8192u, .shader_capacity=8192u},
    HeapConfig{.name="ConstantBuffer", .cpu_capacity=8192u, .shader_capacity=8192u},
    HeapConfig{.name="Sampler", .cpu_capacity=4096u, .shader_capacity=4096u},
    HeapConfig{.name="SamplerFeedbackTexture_UAV", .cpu_capacity=256u, .shader_capacity=256u},
    HeapConfig{.name="RayTracingAccelStructure", .cpu_capacity=256u, .shader_capacity=256u},
    // RTV/DSV are CPU-only; shader_capacity intentionally zero.
    HeapConfig{.name="Texture_DSV", .cpu_capacity=1024u, .shader_capacity=0u},
    HeapConfig{.name="Texture_RTV", .cpu_capacity=1024u, .shader_capacity=0u},
    // clang-format on
  };

  // Register heaps in the specified, deterministic order.
  for (const auto& [name, cpu_capacity, gou_capacity] : heap_configs) {
    this->heaps_.emplace(std::string(name),
      HeapDescription::WithCapacity(cpu_capacity, gou_capacity));
  }

  // Compute contiguous base indices so that heaps occupy non-overlapping
  // ranges in the bindless index space. Use the same constexpr order to
  // ensure deterministic layout across runs. If a heap's shader-visible
  // capacity is zero, do not create a corresponding gpu key (no shader-visible
  // domain is created for that view type).
  oxygen::bindless::HeapIndex::UnderlyingType current = 0;
  for (const auto& cfg : heap_configs) {
    const std::string view_str(cfg.name);
    const auto& desc = this->heaps_.at(view_str);

    // CPU-only key
    {
      const std::string key = view_str + ":cpu";
      const auto capacity = desc.cpu_visible_capacity;
      this->heap_base_indices_[key] = oxygen::bindless::HeapIndex { current };
      current += capacity.get();
    }

    // Shader-visible key: only create if capacity > 0
    if (desc.shader_visible_capacity.get() > 0) {
      const std::string key = view_str + ":gpu";
      const auto capacity = desc.shader_visible_capacity;
      this->heap_base_indices_[key] = oxygen::bindless::HeapIndex { current };
      current += capacity.get();
    }
  }

  DLOG_F(INFO, "Headless Descriptor strategy initialized with {} heap keys",
    this->heap_base_indices_.size());
}

auto AllocationStrategy::GetHeapKey(ResourceViewType view_type,
  DescriptorVisibility visibility) const -> std::string
{
  // Reuse the naming used by DefaultDescriptorAllocationStrategy
  using enum ResourceViewType;
  std::string view_type_str;
  switch (view_type) { // NOLINT(clang-diagnostic-switch-enum)
  case kTexture_SRV:
    view_type_str = "Texture_SRV";
    break;
  case kTexture_UAV:
    view_type_str = "Texture_UAV";
    break;
  case kTypedBuffer_SRV:
    view_type_str = "TypedBuffer_SRV";
    break;
  case kTypedBuffer_UAV:
    view_type_str = "TypedBuffer_UAV";
    break;
  case kStructuredBuffer_UAV:
    view_type_str = "StructuredBuffer_UAV";
    break;
  case kStructuredBuffer_SRV:
    view_type_str = "StructuredBuffer_SRV";
    break;
  case kRawBuffer_SRV:
    view_type_str = "RawBuffer_SRV";
    break;
  case kRawBuffer_UAV:
    view_type_str = "RawBuffer_UAV";
    break;
  case kConstantBuffer:
    view_type_str = "ConstantBuffer";
    break;
  case kSampler:
    view_type_str = "Sampler";
    break;
  case kSamplerFeedbackTexture_UAV:
    view_type_str = "SamplerFeedbackTexture_UAV";
    break;
  case kRayTracingAccelStructure:
    view_type_str = "RayTracingAccelStructure";
    break;
  case kTexture_DSV:
    view_type_str = "Texture_DSV";
    break;
  case kTexture_RTV:
    view_type_str = "Texture_RTV";
    break;
  default:
    view_type_str = "__Unknown__";
    break;
  }

  std::string visibility_str
    = visibility == DescriptorVisibility::kCpuOnly ? "cpu" : "gpu";
  if (view_type_str == "__Unknown__") {
    return "__Unknown__:__Unknown__";
  }
  return view_type_str + ":" + visibility_str;
}

auto AllocationStrategy::GetHeapDescription(const std::string& heap_key) const
  -> const HeapDescription&
{
  const auto pos = heap_key.find(':');
  if (pos == std::string::npos) {
    throw std::runtime_error("Invalid heap key");
  }
  const auto view_part = heap_key.substr(0, pos);
  if (const auto it = heaps_.find(view_part); it != heaps_.end()) {
    return it->second;
  }
  throw std::runtime_error("Heap not found: " + view_part);
}

auto AllocationStrategy::GetHeapBaseIndex(const ResourceViewType view_type,
  const DescriptorVisibility visibility) const -> oxygen::bindless::HeapIndex
{
  const auto key = GetHeapKey(view_type, visibility);
  if (const auto it = heap_base_indices_.find(key);
    it != heap_base_indices_.end()) {
    return it->second;
  }
  return oxygen::bindless::HeapIndex { 0 };
}

} // namespace oxygen::graphics::headless::bindless
