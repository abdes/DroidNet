//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Meta/Bindless.yaml
// Source-Version: 2.0.0
// Schema-Version: 2.0.0
// Tool: BindlessCodeGen 1.2.2
// Generated: 2026-10-13 18:02:51

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

inline constexpr std::array<DescriptorSetDesc, 1> kDescriptorSetTable = {
  {
    DescriptorSetDesc {
      .token = DescriptorSet::kBindlessMain,
      .id = "bindless_main",
      .set = 0U,
    },
  },
};

inline constexpr std::array<BindingDesc, 4> kBindingTable = {
  {
    BindingDesc {
      .token = Binding::kTexturesBinding,
      .id = "textures_binding",
      .descriptor_set = DescriptorSet::kBindlessMain,
      .binding = 0U,
      .descriptor_type = DescriptorType::kSampledImage,
      .descriptor_count = 0U,
      .variable_count = true,
      .update_after_bind = true,
    },
    BindingDesc {
      .token = Binding::kGlobalBuffersBinding,
      .id = "global_buffers_binding",
      .descriptor_set = DescriptorSet::kBindlessMain,
      .binding = 1U,
      .descriptor_type = DescriptorType::kStorageBuffer,
      .descriptor_count = 0U,
      .variable_count = true,
      .update_after_bind = true,
    },
    BindingDesc {
      .token = Binding::kSamplersBinding,
      .id = "samplers_binding",
      .descriptor_set = DescriptorSet::kBindlessMain,
      .binding = 2U,
      .descriptor_type = DescriptorType::kSampler,
      .descriptor_count = 256U,
      .variable_count = false,
      .update_after_bind = false,
    },
    BindingDesc {
      .token = Binding::kViewConstantsBinding,
      .id = "view_constants_binding",
      .descriptor_set = DescriptorSet::kBindlessMain,
      .binding = 3U,
      .descriptor_type = DescriptorType::kUniformBuffer,
      .descriptor_count = 1U,
      .variable_count = false,
      .update_after_bind = false,
    },
  },
};

inline constexpr std::array<DomainBindingDesc, 4> kDomainBindingTable = {
  {
    DomainBindingDesc {
      .domain_id = "textures",
      .binding = Binding::kTexturesBinding,
      .array_element_base = 35816U,
      .capacity = 65536U,
    },
    DomainBindingDesc {
      .domain_id = "srv_global",
      .binding = Binding::kGlobalBuffersBinding,
      .array_element_base = 1U,
      .capacity = 32768U,
    },
    DomainBindingDesc {
      .domain_id = "materials",
      .binding = Binding::kGlobalBuffersBinding,
      .array_element_base = 32769U,
      .capacity = 3047U,
    },
    DomainBindingDesc {
      .domain_id = "samplers",
      .binding = Binding::kSamplersBinding,
      .array_element_base = 0U,
      .capacity = 256U,
    },
  },
};

inline constexpr std::array<PipelineLayoutEntryDesc, 2> kPipelineLayoutTable = {
  {
    PipelineLayoutEntryDesc {
      .token = LayoutEntry::kBindlessMain,
      .id = "BindlessMain",
      .kind = LayoutEntryKind::DescriptorSet,
      .descriptor_set_index = 0U,
      .size_bytes = 0U,
      .stages = "",
    },
    PipelineLayoutEntryDesc {
      .token = LayoutEntry::kRootConstants,
      .id = "RootConstants",
      .kind = LayoutEntryKind::PushConstants,
      .descriptor_set_index = 4294967295U,
      .size_bytes = 8U,
      .stages = "ALL",
    },
  },
};

} // namespace oxygen::bindless::generated::vulkan
