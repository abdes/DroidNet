//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <format>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Core/Bindless/Generated.Heaps.D3D12.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>

namespace oxygen::graphics::d3d12 {

namespace {
  // Parsed heap-key info extracted from a string key like "TYPE:cpu|gpu".
  struct ParsedKey {
    D3D12_DESCRIPTOR_HEAP_TYPE type;
    bool shader_visible;
    std::string normalized_key;
  };

  // Descriptor index range for overlap validation.
  struct RangeInfo {
    std::string key;
    bindless::Handle::UnderlyingType begin;
    bindless::Handle::UnderlyingType end; // exclusive
  };

  // Local helper to build a normalized heap key string.
  static auto BuildHeapKeyLocal(
    D3D12_DESCRIPTOR_HEAP_TYPE type, bool shader_visible) -> std::string
  {
    std::string type_str;
    switch (type) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
      type_str = "CBV_SRV_UAV";
      break;
    case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
      type_str = "SAMPLER";
      break;
    case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
      type_str = "RTV";
      break;
    case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
      type_str = "DSV";
      break;
    default:
      type_str = "UNKNOWN";
      break;
    }
    return std::format("{}:{}", type_str, shader_visible ? "gpu" : "cpu");
  }

  // Parse and validate a heap key. Ensures:
  // - format TYPE:VIS with VIS in {cpu,gpu}
  // - TYPE in {CBV_SRV_UAV,SAMPLER,RTV,DSV}
  // - RTV/DSV are never shader visible
  // - normalized format matches BuildHeapKey result
  static auto ParseHeapKeyOrThrow(const std::string& heap_key) -> ParsedKey
  {
    const auto sep_pos = heap_key.find(':');
    if (sep_pos == std::string::npos) {
      throw std::runtime_error(
        "Invalid heap key format (missing ':'): " + heap_key);
    }

    const std::string type_str = heap_key.substr(0, sep_pos);
    const std::string vis_str = heap_key.substr(sep_pos + 1);

    bool key_shader_visible = false;
    if (vis_str == "cpu") {
      key_shader_visible = false;
    } else if (vis_str == "gpu") {
      key_shader_visible = true;
    } else {
      throw std::runtime_error(
        "Invalid heap key visibility segment (expected 'cpu' or 'gpu'): "
        + heap_key);
    }

    D3D12_DESCRIPTOR_HEAP_TYPE heap_type;
    if (type_str == "CBV_SRV_UAV") {
      heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    } else if (type_str == "SAMPLER") {
      heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    } else if (type_str == "RTV") {
      heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    } else if (type_str == "DSV") {
      heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    } else {
      throw std::runtime_error("Invalid heap key type segment: " + type_str);
    }

    if ((heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV
          || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
      && key_shader_visible) {
      throw std::runtime_error(
        "Heap key declares GPU visibility for RTV/DSV which is not supported: "
        + heap_key);
    }

    const std::string normalized
      = BuildHeapKeyLocal(heap_type, key_shader_visible);
    if (normalized != heap_key) {
      throw std::runtime_error(
        "Heap key not normalized or unsupported format: '" + heap_key
        + "' (expected '" + normalized + "')");
    }

    return ParsedKey { heap_type, key_shader_visible, normalized };
  }

  // Parse and validate a heap JSON entry, given the key-implied visibility.
  // Returns the HeapDescription and its base_index.
  static auto ParseEntryOrThrow(const nlohmann::json& cfg,
    const std::string& heap_key, const bool key_shader_visible)
    -> std::pair<HeapDescription, bindless::Handle>
  {
    if (!cfg.is_object()) {
      throw std::runtime_error(
        "Heap entry must be an object with required fields: '" + heap_key
        + "'");
    }

    if (!cfg.contains("shader_visible")
      || !cfg["shader_visible"].is_boolean()) {
      throw std::runtime_error(
        "Heap entry missing boolean 'shader_visible' field: '" + heap_key
        + "'");
    }
    const bool shader_visible = cfg["shader_visible"].get<bool>();
    if (shader_visible != key_shader_visible) {
      throw std::runtime_error(
        "Mismatch between key visibility and JSON field 'shader_visible' for "
        "heap: "
        + heap_key);
    }

    if (!cfg.contains("capacity") || !cfg["capacity"].is_number_integer()) {
      throw std::runtime_error(
        "Heap entry 'capacity' must be an integer: '" + heap_key + "'");
    }
    const auto capacity_value = cfg["capacity"].get<long long>();
    constexpr auto kMaxValue = bindless::kMaxCapacity.get();
    if (capacity_value < 0 || capacity_value > kMaxValue) {
      throw std::runtime_error("Heap entry 'capacity' must be > 0 and < {} '"
        + heap_key + "'" + std::to_string(kMaxValue));
    }

    const auto capacity = bindless::Capacity {
      static_cast<bindless::Capacity::UnderlyingType>(capacity_value),
    };

    const bool allow_growth = cfg.value("allow_growth", false);
    const float growth_factor = cfg.value("growth_factor", 0.0f);
    const uint32_t max_growth_iterations
      = cfg.value("max_growth_iterations", 0u);

    bindless::Handle base_index { 0 };
    if (cfg.contains("base_index")) {
      if (!cfg["base_index"].is_number_integer()) {
        throw std::runtime_error(
          "Heap entry 'base_index' must be an integer: '" + heap_key + "'");
      }
      const auto base_value = cfg["base_index"].get<long long>();
      if (base_value < 0 || base_value > kMaxValue) {
        throw std::runtime_error(
          "Heap entry 'base_index' must be > 0 and < {} '" + heap_key + "'"
          + std::to_string(kMaxValue));
      }
      base_index = bindless::Handle {
        static_cast<bindless::Handle::UnderlyingType>(base_value),
      };
    }

    HeapDescription desc {};
    if (shader_visible) {
      desc.shader_visible_capacity = capacity;
    } else {
      desc.cpu_visible_capacity = capacity;
    }
    desc.allow_growth = allow_growth;
    desc.growth_factor = growth_factor;
    desc.max_growth_iterations = max_growth_iterations;

    return { desc, base_index };
  }

  // Validate that no descriptor index ranges overlap across heaps.
  static void ValidateNoOverlapsOrThrow(const std::vector<RangeInfo>& ranges)
  {
    for (size_t i = 0; i < ranges.size(); ++i) {
      for (size_t j = i + 1; j < ranges.size(); ++j) {
        const auto& a = ranges[i];
        const auto& b = ranges[j];
        const bool overlap = (a.begin < b.end) && (b.begin < a.end);
        if (overlap) {
          throw std::runtime_error(
            std::string("Overlapping descriptor index ranges between '") + a.key
            + "' and '" + b.key + "'");
        }
      }
    }
  }
} // namespace

D3D12HeapAllocationStrategy::D3D12HeapAllocationStrategy(dx::IDevice* device)
{
  // Device is unused when loading from generated JSON. Keep parameter for API
  // stability.
  (void)device;
  InitFromJson(oxygen::engine::binding::kD3D12HeapStrategyJson);
}

D3D12HeapAllocationStrategy::D3D12HeapAllocationStrategy(
  dx::IDevice* device, const ConfigProvider& provider)
{
  (void)device;
  InitFromJson(provider.GetJson());
}

auto D3D12HeapAllocationStrategy::GetHeapKey(const ResourceViewType view_type,
  const DescriptorVisibility visibility) const -> std::string
{
  // Enforce only valid ResourceViewType and DescriptorVisibility combinations
  if (!IsValid(view_type) || !IsValid(visibility)) {
    throw std::runtime_error("Invalid ResourceViewType or DescriptorVisibility "
                             "for D3D12HeapAllocationStrategy::GetHeapKey");
  }

  const auto heap_type = GetHeapType(view_type);
  const bool can_be_shader_visible
    = // Only CBV_SRV_UAV and SAMPLER can be shader-visible
    (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
      || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

  // RTV/DSV can only be CPU-only
  if ((heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV
        || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    && visibility == DescriptorVisibility::kShaderVisible) {
    throw std::runtime_error("RTV/DSV cannot be shader-visible in "
                             "D3D12HeapAllocationStrategy::GetHeapKey");
  }

  // Only CBV_SRV_UAV and SAMPLER can be shader-visible
  if (!can_be_shader_visible
    && visibility == DescriptorVisibility::kShaderVisible) {
    throw std::runtime_error(
      "Only CBV_SRV_UAV and SAMPLER can be shader-visible in "
      "D3D12HeapAllocationStrategy::GetHeapKey");
  }

  const bool shader_visible
    = visibility == DescriptorVisibility::kShaderVisible;
  return BuildHeapKey(heap_type, shader_visible);
}

auto D3D12HeapAllocationStrategy::GetHeapDescription(
  const std::string& heap_key) const -> const HeapDescription&
{
  const auto it = heap_descriptions_.find(heap_key);
  if (it == heap_descriptions_.end()) {
    throw std::runtime_error(
      fmt::format("Invalid D3D12 heap key: {}", heap_key));
  }
  return it->second;
}

auto D3D12HeapAllocationStrategy::GetHeapBaseIndex(
  const ResourceViewType view_type, const DescriptorVisibility visibility) const
  -> bindless::Handle
{
  const auto key = GetHeapKey(view_type, visibility);
  const auto it = heap_base_indices_.find(key);
  if (it == heap_base_indices_.end()) {
    DLOG_F(WARNING, "No base index found for heap key: {}, using 0", key);
    return bindless::Handle { 0 };
  }
  return it->second;
}

auto D3D12HeapAllocationStrategy::GetHeapType(
  const ResourceViewType view_type) noexcept -> D3D12_DESCRIPTOR_HEAP_TYPE
{
  switch (view_type) {
  case ResourceViewType::kTexture_SRV:
  case ResourceViewType::kTexture_UAV:
  case ResourceViewType::kTypedBuffer_SRV:
  case ResourceViewType::kTypedBuffer_UAV:
  case ResourceViewType::kStructuredBuffer_SRV:
  case ResourceViewType::kStructuredBuffer_UAV:
  case ResourceViewType::kRawBuffer_SRV:
  case ResourceViewType::kRawBuffer_UAV:
  case ResourceViewType::kConstantBuffer:
  case ResourceViewType::kRayTracingAccelStructure:
    return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  case ResourceViewType::kSampler:
  case ResourceViewType::kSamplerFeedbackTexture_UAV:
    return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  case ResourceViewType::kTexture_RTV:
    return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  case ResourceViewType::kTexture_DSV:
    return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  case ResourceViewType::kNone:
  case ResourceViewType::kMaxResourceViewType:
  default: // NOLINT(clang-diagnostic-covered-switch-default)
    ABORT_F("Illegal ResourceViewType `{}` used to GetHeapType()",
      nostd::to_string(view_type));
  }
}

auto D3D12HeapAllocationStrategy::BuildHeapKey(
  const D3D12_DESCRIPTOR_HEAP_TYPE type, const bool shader_visible)
  -> std::string
{
  // Delegate to the local helper to keep normalization logic in one place.
  return BuildHeapKeyLocal(type, shader_visible);
}

auto D3D12HeapAllocationStrategy::EmbeddedConfigProvider::Instance() noexcept
  -> const EmbeddedConfigProvider&
{
  static EmbeddedConfigProvider instance;
  return instance;
}

auto D3D12HeapAllocationStrategy::EmbeddedConfigProvider::GetJson()
  const noexcept -> std::string_view
{
  return oxygen::engine::binding::kD3D12HeapStrategyJson;
}

void D3D12HeapAllocationStrategy::InitFromJson(std::string_view json_text)
{
  using nlohmann::json;

  const auto spec = json::parse(json_text);
  if (!spec.contains("heaps") || !spec["heaps"].is_object()) {
    throw std::runtime_error("D3D12 heap strategy JSON missing 'heaps' object");
  }

  const auto& heaps = spec["heaps"];
  std::vector<RangeInfo> ranges;
  for (auto it = heaps.begin(); it != heaps.end(); ++it) {
    const std::string heap_key = it.key();
    const auto& cfg = it.value();
    const ParsedKey parsed = ParseHeapKeyOrThrow(heap_key);
    const auto [desc, base_index]
      = ParseEntryOrThrow(cfg, heap_key, parsed.shader_visible);

    heap_descriptions_[heap_key] = desc;
    heap_base_indices_[heap_key] = base_index;

    // Record range for overlap check (ignore zero-length ranges)
    const auto u_capacity = parsed.shader_visible
      ? desc.shader_visible_capacity.get()
      : desc.cpu_visible_capacity.get();
    const auto end_index = bindless::Handle { base_index.get() + u_capacity };
    if (u_capacity > 0) {
      ranges.push_back(
        RangeInfo { heap_key, base_index.get(), end_index.get() });
    }
  }

  // Verify no overlapping ranges between any configured heaps
  ValidateNoOverlapsOrThrow(ranges);

  DLOG_F(INFO,
    "Initialized D3D12HeapAllocationStrategy from JSON with {} heap "
    "configurations",
    heap_descriptions_.size());
}

} // namespace oxygen::graphics::d3d12
