//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <d3d12.h>
#include <d3dcommon.h>
#include <fmt/format.h>
#include <winerror.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/GraphicsCapabilities.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceFeatures.h>

namespace oxygen::graphics::d3d12 {

namespace {

  constexpr auto kAllRequirements = std::array {
    DeviceRequirement::kFeatureLevel,
    DeviceRequirement::kShaderModel,
    DeviceRequirement::kResourceBindingTier,
    DeviceRequirement::kWaveOps,
  };

  //! Highest first, so the first one the runtime accepts is the device's.
  constexpr auto kFeatureLevels = std::array {
    D3D_FEATURE_LEVEL_12_2,
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
  };

  //! Highest first: a runtime rejects shader models newer than it knows, so
  //! the query walks down until one is accepted.
  constexpr auto kShaderModels = std::array {
    D3D_SHADER_MODEL_6_9,
    D3D_SHADER_MODEL_6_8,
    D3D_SHADER_MODEL_6_7,
    D3D_SHADER_MODEL_6_6,
    D3D_SHADER_MODEL_6_5,
    D3D_SHADER_MODEL_6_4,
    D3D_SHADER_MODEL_6_3,
    D3D_SHADER_MODEL_6_2,
    D3D_SHADER_MODEL_6_1,
    D3D_SHADER_MODEL_6_0,
    D3D_SHADER_MODEL_5_1,
  };

  //! `data`, filled by the device; none when the query fails. Callers pass
  //! valid initial values: some option structs hold enums without a zero
  //! enumerator.
  template <typename Data>
  auto QueryFeature(ID3D12Device& device, const D3D12_FEATURE feature,
    Data data) -> std::optional<Data>
  {
    if (FAILED(device.CheckFeatureSupport(feature, &data, sizeof(data)))) {
      return std::nullopt;
    }
    return data;
  }

  //! A projection that copies `member` out of queried feature data.
  /*!
   `std::optional::transform` with a bare member pointer would yield a
   reference, which an optional cannot hold.
  */
  template <typename Data, typename Value>
  constexpr auto Read(Value Data::* member)
  {
    return [member](const Data& data) -> Value { return data.*member; };
  }

  auto QueryFeatureLevel(ID3D12Device& device) -> D3D_FEATURE_LEVEL
  {
    return QueryFeature(device, D3D12_FEATURE_FEATURE_LEVELS,
      D3D12_FEATURE_DATA_FEATURE_LEVELS {
        .NumFeatureLevels = static_cast<UINT>(kFeatureLevels.size()),
        .pFeatureLevelsRequested = kFeatureLevels.data(),
        .MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0,
      })
      .transform(
        Read(&D3D12_FEATURE_DATA_FEATURE_LEVELS::MaxSupportedFeatureLevel))
      .value_or(D3D_FEATURE_LEVEL_11_0);
  }

  auto QueryShaderModel(ID3D12Device& device) -> D3D_SHADER_MODEL
  {
    for (const auto requested : kShaderModels) {
      if (const auto data = QueryFeature(device, D3D12_FEATURE_SHADER_MODEL,
            D3D12_FEATURE_DATA_SHADER_MODEL {
              .HighestShaderModel = requested,
            })) {
        return data->HighestShaderModel;
      }
    }
    return D3D_SHADER_MODEL_5_1;
  }

  auto QueryMemoryArchitecture(ID3D12Device& device) -> MemoryArchitecture
  {
    return QueryFeature(
      device, D3D12_FEATURE_ARCHITECTURE1, D3D12_FEATURE_DATA_ARCHITECTURE1 {})
      .transform([](const D3D12_FEATURE_DATA_ARCHITECTURE1& architecture) {
        if (architecture.CacheCoherentUMA != FALSE) {
          return MemoryArchitecture::kCacheCoherentUnified;
        }
        return architecture.UMA != FALSE ? MemoryArchitecture::kUnified
                                         : MemoryArchitecture::kDedicated;
      })
      .value_or(MemoryArchitecture::kDedicated);
  }

  auto Meets(const DeviceRequirement requirement,
    const DeviceFeatures& features, const DeviceRequirements& requirements)
    -> bool
  {
    switch (requirement) {
    case DeviceRequirement::kFeatureLevel:
      return features.feature_level >= requirements.min_feature_level;
    case DeviceRequirement::kShaderModel:
      return features.shader_model >= requirements.min_shader_model;
    case DeviceRequirement::kResourceBindingTier:
      return features.resource_binding_tier
        >= requirements.min_resource_binding_tier;
    case DeviceRequirement::kWaveOps:
      return !requirements.wave_ops || features.wave_ops;
    }
    return false;
  }

  //! `1.1` for tier value 11: D3D numbers these tiers major * 10 + minor.
  auto DecimalTierName(const unsigned tier) -> std::string
  {
    return fmt::format("{}.{}", tier / 10U, tier % 10U);
  }

} // namespace

auto MissingRequirements(const DeviceFeatures& features,
  const DeviceRequirements& requirements) -> std::vector<DeviceRequirement>
{
  return kAllRequirements
    | std::views::filter([&](const DeviceRequirement requirement) {
        return !Meets(requirement, features, requirements);
      })
    | std::ranges::to<std::vector>();
}

auto DescribeShortfall(const DeviceRequirement requirement,
  const DeviceFeatures& features, const DeviceRequirements& requirements)
  -> std::string
{
  switch (requirement) {
  case DeviceRequirement::kFeatureLevel:
    return fmt::format("feature level {} (device has {})",
      FeatureLevelName(requirements.min_feature_level),
      FeatureLevelName(features.feature_level));
  case DeviceRequirement::kShaderModel:
    return fmt::format("shader model {} (device has {})",
      ShaderModelName(requirements.min_shader_model),
      ShaderModelName(features.shader_model));
  case DeviceRequirement::kResourceBindingTier:
    return fmt::format("resource binding tier {} (device has {})",
      static_cast<unsigned>(requirements.min_resource_binding_tier),
      static_cast<unsigned>(features.resource_binding_tier));
  case DeviceRequirement::kWaveOps:
    return "wave operations";
  }
  return "__NotSupported__";
}

auto to_string(const DeviceFeatures& features) -> std::string
{
  auto text = fmt::format("FL {}, SM {}, tier {}{}, {}",
    FeatureLevelName(features.feature_level),
    ShaderModelName(features.shader_model),
    static_cast<unsigned>(features.resource_binding_tier),
    features.wave_ops ? ", wave ops" : "",
    nostd::to_string(features.memory_architecture));
  if (const auto ray_tracing = RaytracingTierName(features.raytracing_tier);
    !ray_tracing.empty()) {
    text += fmt::format(", RT {}", ray_tracing);
  }
  if (features.variable_shading_rate_tier
    != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
    text += fmt::format(
      ", VRS {}", static_cast<unsigned>(features.variable_shading_rate_tier));
  }
  if (const auto mesh = MeshShaderTierNumber(features.mesh_shader_tier);
    mesh != 0U) {
    text += fmt::format(", mesh {}", mesh);
  }
  if (const auto sampler
    = SamplerFeedbackTierName(features.sampler_feedback_tier);
    !sampler.empty()) {
    text += fmt::format(", sampler {}", sampler);
  }
  return text;
}

auto ShaderModelName(const D3D_SHADER_MODEL shader_model) -> std::string
{
  // The major and minor versions are the high and low nibbles.
  const auto value = static_cast<unsigned>(shader_model);
  return fmt::format("{}.{}", value >> 4U, value & 0xFU);
}

auto RaytracingTierName(const D3D12_RAYTRACING_TIER tier) -> std::string
{
  if (tier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
    return {};
  }
  return DecimalTierName(static_cast<unsigned>(tier));
}

auto SamplerFeedbackTierName(const D3D12_SAMPLER_FEEDBACK_TIER tier)
  -> std::string
{
  if (tier == D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED) {
    return {};
  }
  // D3D numbers these tiers major * 100 + minor * 10, e.g. 90 for 0.9.
  return DecimalTierName(static_cast<unsigned>(tier) / 10U);
}

auto MeshShaderTierNumber(const D3D12_MESH_SHADER_TIER tier) -> std::uint32_t
{
  // D3D numbers mesh shader tiers major * 10.
  return static_cast<std::uint32_t>(tier) / 10U;
}

auto FeatureLevelName(const D3D_FEATURE_LEVEL feature_level) -> std::string_view
{
  switch (feature_level) { // NOLINT(clang-diagnostic-switch-enum)
  case D3D_FEATURE_LEVEL_12_2:
    return "12_2";
  case D3D_FEATURE_LEVEL_12_1:
    return "12_1";
  case D3D_FEATURE_LEVEL_12_0:
    return "12_0";
  case D3D_FEATURE_LEVEL_11_1:
    return "11_1";
  case D3D_FEATURE_LEVEL_11_0:
    return "11_0";
  default:
    return "below 11_0";
  }
}

auto QueryDeviceFeatures(ID3D12Device& device) -> DeviceFeatures
{
  const auto options = QueryFeature(device, D3D12_FEATURE_D3D12_OPTIONS,
    D3D12_FEATURE_DATA_D3D12_OPTIONS {
      .ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1,
      .ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1,
    });
  const auto options1 = QueryFeature(
    device, D3D12_FEATURE_D3D12_OPTIONS1, D3D12_FEATURE_DATA_D3D12_OPTIONS1 {});
  const auto options5 = QueryFeature(
    device, D3D12_FEATURE_D3D12_OPTIONS5, D3D12_FEATURE_DATA_D3D12_OPTIONS5 {});
  const auto options6 = QueryFeature(
    device, D3D12_FEATURE_D3D12_OPTIONS6, D3D12_FEATURE_DATA_D3D12_OPTIONS6 {});
  const auto options7 = QueryFeature(
    device, D3D12_FEATURE_D3D12_OPTIONS7, D3D12_FEATURE_DATA_D3D12_OPTIONS7 {});

  return DeviceFeatures {
    .feature_level = QueryFeatureLevel(device),
    .shader_model = QueryShaderModel(device),
    .resource_binding_tier = options
      .transform(Read(&D3D12_FEATURE_DATA_D3D12_OPTIONS::ResourceBindingTier))
      .value_or(D3D12_RESOURCE_BINDING_TIER_1),
    .wave_ops = options1
      .transform([](const D3D12_FEATURE_DATA_D3D12_OPTIONS1& data) {
        return data.WaveOps != FALSE;
      })
      .value_or(false),
    .memory_architecture = QueryMemoryArchitecture(device),
    .raytracing_tier = options5
      .transform(Read(&D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier))
      .value_or(D3D12_RAYTRACING_TIER_NOT_SUPPORTED),
    .variable_shading_rate_tier = options6
      .transform(
        Read(&D3D12_FEATURE_DATA_D3D12_OPTIONS6::VariableShadingRateTier))
      .value_or(D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED),
    .mesh_shader_tier = options7
      .transform(Read(&D3D12_FEATURE_DATA_D3D12_OPTIONS7::MeshShaderTier))
      .value_or(D3D12_MESH_SHADER_TIER_NOT_SUPPORTED),
    .sampler_feedback_tier = options7
      .transform(Read(&D3D12_FEATURE_DATA_D3D12_OPTIONS7::SamplerFeedbackTier))
      .value_or(D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED),
  };
}

} // namespace oxygen::graphics::d3d12
