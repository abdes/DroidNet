//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <d3d12.h>
#include <d3dcommon.h>

#include <Oxygen/Graphics/Common/GraphicsCapabilities.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

//! What a D3D12 device supports, queried once when the device is created.
/*!
 The first four fields are what `DeviceRequirements` can demand; the tiers
 after them describe optional features the engine does not require.
*/
struct DeviceFeatures {
  D3D_FEATURE_LEVEL feature_level { D3D_FEATURE_LEVEL_11_0 };
  D3D_SHADER_MODEL shader_model { D3D_SHADER_MODEL_5_1 };
  D3D12_RESOURCE_BINDING_TIER resource_binding_tier {
    D3D12_RESOURCE_BINDING_TIER_1,
  };
  bool wave_ops { false };
  MemoryArchitecture memory_architecture { MemoryArchitecture::kDedicated };
  D3D12_RAYTRACING_TIER raytracing_tier {
    D3D12_RAYTRACING_TIER_NOT_SUPPORTED,
  };
  D3D12_VARIABLE_SHADING_RATE_TIER variable_shading_rate_tier {
    D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED,
  };
  D3D12_MESH_SHADER_TIER mesh_shader_tier {
    D3D12_MESH_SHADER_TIER_NOT_SUPPORTED,
  };
  D3D12_SAMPLER_FEEDBACK_TIER sampler_feedback_tier {
    D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED,
  };
};

//! What the engine needs from a device; an adapter whose device falls short
//! is never selected.
struct DeviceRequirements {
  D3D_FEATURE_LEVEL min_feature_level { D3D_FEATURE_LEVEL_12_0 };
  //! Shaders read the draw's first instance from `SV_StartInstanceLocation`.
  D3D_SHADER_MODEL min_shader_model { D3D_SHADER_MODEL_6_8 };
  //! Bindless shaders index `ResourceDescriptorHeap` and
  //! `SamplerDescriptorHeap`.
  D3D12_RESOURCE_BINDING_TIER min_resource_binding_tier {
    D3D12_RESOURCE_BINDING_TIER_3,
  };
  //! Exposure, occlusion and shadow analysis shaders use wave intrinsics.
  bool wave_ops { true };
};

//! One requirement of `DeviceRequirements`.
enum class DeviceRequirement : std::uint8_t {
  kFeatureLevel,
  kShaderModel,
  kResourceBindingTier,
  kWaveOps,
};

//! The requirements `features` does not meet, in declaration order; empty
//! when the device qualifies.
OXGN_D3D12_NDAPI auto MissingRequirements(const DeviceFeatures& features,
  const DeviceRequirements& requirements) -> std::vector<DeviceRequirement>;

//! What `requirement` demands and what the device has, e.g.
//! `shader model 6.8 (device has 6.6)`.
OXGN_D3D12_NDAPI auto DescribeShortfall(DeviceRequirement requirement,
  const DeviceFeatures& features, const DeviceRequirements& requirements)
  -> std::string;

//! A one-line summary, e.g. `FL 12_2, SM 6.8, tier 3, wave ops, dedicated,
//! RT 1.1, VRS 2, mesh 1, sampler 1.0`; optional features the device lacks
//! are left out.
OXGN_D3D12_NDAPI auto to_string(const DeviceFeatures& features) -> std::string;

//! `6.8` for `D3D_SHADER_MODEL_6_8`.
OXGN_D3D12_NDAPI auto ShaderModelName(D3D_SHADER_MODEL shader_model)
  -> std::string;

//! `1.1` for `D3D12_RAYTRACING_TIER_1_1`; empty when unsupported.
OXGN_D3D12_NDAPI auto RaytracingTierName(D3D12_RAYTRACING_TIER tier)
  -> std::string;

//! `1.0` for `D3D12_SAMPLER_FEEDBACK_TIER_1_0`; empty when unsupported.
OXGN_D3D12_NDAPI auto SamplerFeedbackTierName(D3D12_SAMPLER_FEEDBACK_TIER tier)
  -> std::string;

//! The mesh shader tier as a number, e.g. 1; 0 when unsupported.
OXGN_D3D12_NDAPI auto MeshShaderTierNumber(D3D12_MESH_SHADER_TIER tier)
  -> std::uint32_t;

//! The feature level as D3D names it, e.g. `12_2`.
/*!
 A named function rather than a `to_string` overload: `D3D_FEATURE_LEVEL` is a
 global enum, so argument-dependent lookup would never find an overload here.
*/
OXGN_D3D12_NDAPI auto FeatureLevelName(D3D_FEATURE_LEVEL feature_level)
  -> std::string_view;

//! Query what `device` supports.
/*!
 Each query that fails leaves its capability unsupported, so a runtime that
 predates a feature reports it as absent instead of failing.
*/
OXGN_D3D12_NDAPI auto QueryDeviceFeatures(ID3D12Device& device)
  -> DeviceFeatures;

} // namespace oxygen::graphics::d3d12
