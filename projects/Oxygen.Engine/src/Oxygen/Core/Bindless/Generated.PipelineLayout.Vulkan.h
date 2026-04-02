//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// clang-format off

// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Meta/Bindless.yaml
// Source-Version: 2.0.0
// Schema-Version: 2.0.0
// Tool: BindlessCodeGen 1.2.2
// Generated: 2026-04-02 21:10:18

#pragma once

#include <array>
#include <cstdint>

namespace oxygen::bindless::generated::vulkan {

enum class DescriptorSet : uint32_t {
  kBindlessMain = 0,
  kCount = 1,
};

enum class Binding : uint32_t {
  kTexturesBinding = 0,
  kGlobalBuffersBinding = 1,
  kSamplersBinding = 2,
  kViewConstantsBinding = 3,
  kCount = 4,
};

enum class LayoutEntry : uint32_t {
  kBindlessMain = 0,
  kRootConstants = 1,
  kCount = 2,
};

enum class LayoutEntryKind : uint8_t {
  DescriptorSet = 0,
  PushConstants = 1,
};

enum class DescriptorType : uint8_t {
  kSampledImage = 0,
  kStorageBuffer = 1,
  kSampler = 2,
  kUniformBuffer = 3,
  kCount = 4,
};

struct DescriptorSetDesc {
  DescriptorSet token;
  const char* id;
  uint32_t set;
};

struct BindingDesc {
  Binding token;
  const char* id;
  DescriptorSet descriptor_set;
  uint32_t binding;
  DescriptorType descriptor_type;
  uint32_t descriptor_count;
  bool variable_count;
  bool update_after_bind;
};

struct DomainBindingDesc {
  const char* domain_id;
  Binding binding;
  uint32_t array_element_base;
  uint32_t capacity;
};

struct PipelineLayoutEntryDesc {
  LayoutEntry token;
  const char* id;
  LayoutEntryKind kind;
  uint32_t descriptor_set_index;
  uint32_t size_bytes;
  const char* stages;
};

inline constexpr std::array<DescriptorSetDesc, 1>
  kDescriptorSetTable = {{
  DescriptorSetDesc{ DescriptorSet::kBindlessMain, "bindless_main", 0U },
}};

inline constexpr std::array<BindingDesc, 4> kBindingTable = {{
  BindingDesc{ Binding::kTexturesBinding, "textures_binding", DescriptorSet::kBindlessMain, 0U, DescriptorType::kSampledImage, 0U, true, true },
  BindingDesc{ Binding::kGlobalBuffersBinding, "global_buffers_binding", DescriptorSet::kBindlessMain, 1U, DescriptorType::kStorageBuffer, 0U, true, true },
  BindingDesc{ Binding::kSamplersBinding, "samplers_binding", DescriptorSet::kBindlessMain, 2U, DescriptorType::kSampler, 256U, false, false },
  BindingDesc{ Binding::kViewConstantsBinding, "view_constants_binding", DescriptorSet::kBindlessMain, 3U, DescriptorType::kUniformBuffer, 1U, false, false },
}};

inline constexpr std::array<DomainBindingDesc, 4>
  kDomainBindingTable = {{
  DomainBindingDesc{ "textures", Binding::kTexturesBinding, 35816U, 65536U },
  DomainBindingDesc{ "srv_global", Binding::kGlobalBuffersBinding, 1U, 32768U },
  DomainBindingDesc{ "materials", Binding::kGlobalBuffersBinding, 32769U, 3047U },
  DomainBindingDesc{ "samplers", Binding::kSamplersBinding, 0U, 256U },
}};

inline constexpr std::array<PipelineLayoutEntryDesc, 2>
  kPipelineLayoutTable = {{
  PipelineLayoutEntryDesc{ LayoutEntry::kBindlessMain, "BindlessMain", LayoutEntryKind::DescriptorSet, 0U, 0U, "" },
  PipelineLayoutEntryDesc{ LayoutEntry::kRootConstants, "RootConstants", LayoutEntryKind::PushConstants, 4294967295U, 8U, "ALL" },
}};

} // namespace oxygen::bindless::generated::vulkan
// clang-format on
