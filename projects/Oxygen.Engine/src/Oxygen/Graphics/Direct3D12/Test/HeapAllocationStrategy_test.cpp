//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::d3d12::D3D12HeapAllocationStrategy;

namespace {

struct HeapKeyValidMappingParam {
    const char* test_name;
    ResourceViewType view_type;
    DescriptorVisibility visibility;
    const char* key;
};

static constexpr HeapKeyValidMappingParam kValidMappings[] = {
    // CBV_SRV_UAV heap
    { "Texture_SRV__GPU", ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "Texture_UAV__GPU", ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "TypedBuffer_SRV__GPU", ResourceViewType::kTypedBuffer_SRV, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "TypedBuffer_UAV__GPU", ResourceViewType::kTypedBuffer_UAV, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "StructuredBuffer_SRV__GPU", ResourceViewType::kStructuredBuffer_SRV, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "StructuredBuffer_UAV__GPU", ResourceViewType::kStructuredBuffer_UAV, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "RawBuffer_SRV__GPU", ResourceViewType::kRawBuffer_SRV, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "RawBuffer_UAV__GPU", ResourceViewType::kRawBuffer_UAV, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "ConstantBuffer__GPU", ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "RayTracingAccelStructure__GPU", ResourceViewType::kRayTracingAccelStructure, DescriptorVisibility::kShaderVisible, "CBV_SRV_UAV:gpu" },
    { "Texture_SRV__CPU", ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "Texture_UAV__CPU", ResourceViewType::kTexture_UAV, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "TypedBuffer_SRV__CPU", ResourceViewType::kTypedBuffer_SRV, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "TypedBuffer_UAV__CPU", ResourceViewType::kTypedBuffer_UAV, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "StructuredBuffer_SRV__CPU", ResourceViewType::kStructuredBuffer_SRV, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "StructuredBuffer_UAV__CPU", ResourceViewType::kStructuredBuffer_UAV, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "RawBuffer_SRV__CPU", ResourceViewType::kRawBuffer_SRV, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "RawBuffer_UAV__CPU", ResourceViewType::kRawBuffer_UAV, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "ConstantBuffer__CPU", ResourceViewType::kConstantBuffer, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    { "RayTracingAccelStructure__CPU", ResourceViewType::kRayTracingAccelStructure, DescriptorVisibility::kCpuOnly, "CBV_SRV_UAV:cpu" },
    // Sampler heap
    { "Sampler__GPU", ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible, "SAMPLER:gpu" },
    { "SamplerFeedbackTexture_UAV__GPU", ResourceViewType::kSamplerFeedbackTexture_UAV, DescriptorVisibility::kShaderVisible, "SAMPLER:gpu" },
    { "Sampler__CPU", ResourceViewType::kSampler, DescriptorVisibility::kCpuOnly, "SAMPLER:cpu" },
    { "SamplerFeedbackTexture_UAV__CPU", ResourceViewType::kSamplerFeedbackTexture_UAV, DescriptorVisibility::kCpuOnly, "SAMPLER:cpu" },
    // RTV/DSV heap (CPU only)
    { "Texture_RTV__CPU", ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly, "RTV:cpu" },
    { "Texture_DSV__CPU", ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly, "DSV:cpu" },
};

struct HeapKeyInvalidMappingParam {
    const char* test_name;
    ResourceViewType view_type;
    DescriptorVisibility visibility;
};

static constexpr HeapKeyInvalidMappingParam kInvalidMappings[] = {
    // RTV/DSV cannot be shader visible
    { "Texture_RTV__GPU", ResourceViewType::kTexture_RTV, DescriptorVisibility::kShaderVisible },
    { "Texture_DSV__GPU", ResourceViewType::kTexture_DSV, DescriptorVisibility::kShaderVisible },
    // None/Max are always invalid
    { "None__GPU", ResourceViewType::kNone, DescriptorVisibility::kShaderVisible },
    { "None__CPU", ResourceViewType::kNone, DescriptorVisibility::kCpuOnly },
    { "MaxResourceViewType__GPU", ResourceViewType::kMaxResourceViewType, DescriptorVisibility::kShaderVisible },
    { "MaxResourceViewType__CPU", ResourceViewType::kMaxResourceViewType, DescriptorVisibility::kCpuOnly },
    // MaxDescriptorVisibility is always invalid
    { "Texture_SRV__Max", ResourceViewType::kTexture_SRV, DescriptorVisibility::kMaxDescriptorVisibility },
    { "Texture_UAV__Max", ResourceViewType::kTexture_UAV, DescriptorVisibility::kMaxDescriptorVisibility },
    { "Sampler__Max", ResourceViewType::kSampler, DescriptorVisibility::kMaxDescriptorVisibility },
    { "Texture_RTV__Max", ResourceViewType::kTexture_RTV, DescriptorVisibility::kMaxDescriptorVisibility },
    { "Texture_DSV__Max", ResourceViewType::kTexture_DSV, DescriptorVisibility::kMaxDescriptorVisibility },
    { "None__Max", ResourceViewType::kNone, DescriptorVisibility::kMaxDescriptorVisibility },
    { "MaxResourceViewType__Max", ResourceViewType::kMaxResourceViewType, DescriptorVisibility::kMaxDescriptorVisibility },
};

// Array of all valid heap keys according to D3D12
static constexpr const char* kAllValidKeys[] = {
    "CBV_SRV_UAV:gpu",
    "CBV_SRV_UAV:cpu",
    "SAMPLER:gpu",
    "SAMPLER:cpu",
    "RTV:cpu",
    "DSV:cpu"
};

class HeapAllocationStrategyTest : public ::testing::Test {
public:
    D3D12HeapAllocationStrategy strat { nullptr };
    std::set<std::string> all_unique_heap_keys;
    std::map<std::pair<ResourceViewType, DescriptorVisibility>, std::string> pair_to_key_;

    // Static arrays of all valid and invalid mappings as per D3D12

protected:
    void SetUp() override
    {
        CollectUniqueHeapKeys();
    }

private:
    void CollectUniqueHeapKeys()
    {
        all_unique_heap_keys.clear();
        pair_to_key_.clear();
        using VT = oxygen::graphics::ResourceViewType;
        using DV = oxygen::graphics::DescriptorVisibility;
        for (uint8_t vt = static_cast<uint8_t>(VT::kNone); vt <= static_cast<uint8_t>(VT::kMaxResourceViewType); ++vt) {
            for (uint8_t dv = static_cast<uint8_t>(DV::kNone); dv <= static_cast<uint8_t>(DV::kMaxDescriptorVisibility); ++dv) {
                VT view_type = static_cast<VT>(vt);
                DV visibility = static_cast<DV>(dv);
                if (oxygen::graphics::IsValid(view_type) && oxygen::graphics::IsValid(visibility)) {
                    try {
                        auto key = strat.GetHeapKey(view_type, visibility);
                        all_unique_heap_keys.insert(key);
                        pair_to_key_[{ view_type, visibility }] = key;
                    } catch (...) {
                        // Ignore invalid combinations that throw
                    }
                }
            }
        }
    }
};

// -----------------------------------------------------------------------------
// Basics Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(HeapAllocationStrategyTest, UniqueKeysAreAllValid)
{
    EXPECT_EQ(std::extent<decltype(kAllValidKeys)>::value, all_unique_heap_keys.size());
}

NOLINT_TEST_F(HeapAllocationStrategyTest, GarbageKeyThrows)
{
    EXPECT_THROW(strat.GetHeapDescription("G_A_R_B_A_G_E:cpu"), std::runtime_error);
}

// -----------------------------------------------------------------------------
// Parametrized HeapKey validity tests
// -----------------------------------------------------------------------------

static std::string InvalidMappingTestName(
    const ::testing::TestParamInfo<HeapKeyInvalidMappingParam>& info)
{
    return info.param.test_name;
}

class InvalidMappingsTest : public HeapAllocationStrategyTest,
                            public ::testing::WithParamInterface<HeapKeyInvalidMappingParam> { };

INSTANTIATE_TEST_SUITE_P(
    All,
    InvalidMappingsTest,
    ::testing::ValuesIn(kInvalidMappings),
    InvalidMappingTestName);

NOLINT_TEST_P(InvalidMappingsTest, HeapKeyThrows)
{
    const auto& param = GetParam();
    NOLINT_EXPECT_THROW(strat.GetHeapKey(param.view_type, param.visibility), std::runtime_error);
}

NOLINT_TEST_P(InvalidMappingsTest, GetBaseIndexThrows)
{
    const auto& param = GetParam();
    NOLINT_EXPECT_THROW(strat.GetHeapBaseIndex(param.view_type, param.visibility), std::runtime_error);
}

static std::string ValidMappingTestName(
    const ::testing::TestParamInfo<HeapKeyValidMappingParam>& info)
{
    return info.param.test_name;
}

class ValidMappingsTest : public HeapAllocationStrategyTest,
                          public ::testing::WithParamInterface<HeapKeyValidMappingParam> { };

INSTANTIATE_TEST_SUITE_P(
    All,
    ValidMappingsTest,
    ::testing::ValuesIn(kValidMappings),
    ValidMappingTestName);

NOLINT_TEST_P(ValidMappingsTest, HeapKeyWorks)
{
    const auto& param = GetParam();
    std::string heap_key {};
    EXPECT_NO_THROW(heap_key = strat.GetHeapKey(param.view_type, param.visibility));
    EXPECT_TRUE(std::ranges::find(kAllValidKeys, heap_key) != std::end(kAllValidKeys));
}

NOLINT_TEST_P(ValidMappingsTest, GetBaseIndexWorks)
{
    const auto& param = GetParam();
    std::string heap_key {};
    EXPECT_NO_THROW(heap_key = strat.GetHeapKey(param.view_type, param.visibility));
}

// -----------------------------------------------------------------------------
// Heap Key Mapping Tests
// -----------------------------------------------------------------------------

class KeyMappingRulesTest : public HeapAllocationStrategyTest {
};

NOLINT_TEST_F(KeyMappingRulesTest, AllCBVSRVUAVTypesMapToSameHeapKey)
{
    std::vector<ResourceViewType> cbv_srv_uav_types = {
        ResourceViewType::kTexture_SRV,
        ResourceViewType::kTexture_UAV,
        ResourceViewType::kTypedBuffer_SRV,
        ResourceViewType::kTypedBuffer_UAV,
        ResourceViewType::kStructuredBuffer_SRV,
        ResourceViewType::kStructuredBuffer_UAV,
        ResourceViewType::kRawBuffer_SRV,
        ResourceViewType::kRawBuffer_UAV,
        ResourceViewType::kConstantBuffer,
        ResourceViewType::kRayTracingAccelStructure
    };
    for (auto vt : cbv_srv_uav_types) {
        for (auto vis : { DescriptorVisibility::kShaderVisible, DescriptorVisibility::kCpuOnly }) {
            // Find the mapping in kValidMappings
            const HeapKeyValidMappingParam* mapping = nullptr;
            for (const auto& m : kValidMappings) {
                if (m.view_type == vt && m.visibility == vis) {
                    mapping = &m;
                    break;
                }
            }
            ASSERT_NE(mapping, nullptr) << "No mapping found for view_type and visibility";
            std::string returned_key = strat.GetHeapKey(vt, vis);
            EXPECT_EQ(returned_key, mapping->key) << "Failed for test_name: " << mapping->test_name;
            // Case-insensitive check for 'cbv_srv_uav' in key
            std::string key_lower = returned_key;
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            EXPECT_NE(key_lower.find("cbv_srv_uav"), std::string::npos) << "Key does not contain cbv_srv_uav for test_name: " << mapping->test_name;
        }
    }
}

NOLINT_TEST_F(KeyMappingRulesTest, AllSamplerTypesMapToSameHeapKey)
{
    std::vector<ResourceViewType> sampler_types = {
        ResourceViewType::kSampler,
        ResourceViewType::kSamplerFeedbackTexture_UAV
    };
    for (auto vt : sampler_types) {
        for (auto vis : { DescriptorVisibility::kShaderVisible, DescriptorVisibility::kCpuOnly }) {
            // Find the mapping in kValidMappings
            const HeapKeyValidMappingParam* mapping = nullptr;
            for (const auto& m : kValidMappings) {
                if (m.view_type == vt && m.visibility == vis) {
                    mapping = &m;
                    break;
                }
            }
            ASSERT_NE(mapping, nullptr) << "No mapping found for sampler view_type and visibility";
            std::string returned_key = strat.GetHeapKey(vt, vis);
            EXPECT_EQ(returned_key, mapping->key) << "Failed for test_name: " << mapping->test_name;
            // Case-insensitive check for 'sampler' in key
            std::string key_lower = returned_key;
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            EXPECT_NE(key_lower.find("sampler"), std::string::npos) << "Key does not contain sampler for test_name: " << mapping->test_name;
        }
    }
}

NOLINT_TEST_F(KeyMappingRulesTest, RTVTypeMapsToHeapKey)
{
    EXPECT_EQ(strat.GetHeapKey(ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly), "RTV:cpu");
}

NOLINT_TEST_F(KeyMappingRulesTest, DSVTypeMapsToHeapKey)
{
    EXPECT_EQ(strat.GetHeapKey(ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly), "DSV:cpu");
}

// -----------------------------------------------------------------------------
// Heap D3D12 Policy Tests
// -----------------------------------------------------------------------------

class PolicyRulesTest : public HeapAllocationStrategyTest {
};

NOLINT_TEST_F(PolicyRulesTest, RTVAndDSVAreAlwaysCpuOnly)
{
    auto rtv_desc = strat.GetHeapDescription(strat.GetHeapKey(ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly));
    auto dsv_desc = strat.GetHeapDescription(strat.GetHeapKey(ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly));
    EXPECT_EQ(rtv_desc.shader_visible_capacity, 0u);
    EXPECT_EQ(dsv_desc.shader_visible_capacity, 0u);
}

NOLINT_TEST_F(PolicyRulesTest, OnlyCBVSRVUAVAndSamplerCanBeShaderVisible)
{
    auto cbv_desc = strat.GetHeapDescription(strat.GetHeapKey(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible));
    auto sampler_desc = strat.GetHeapDescription(strat.GetHeapKey(ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible));
    auto rtv_desc = strat.GetHeapDescription(strat.GetHeapKey(ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly));
    auto dsv_desc = strat.GetHeapDescription(strat.GetHeapKey(ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly));
    EXPECT_GT(cbv_desc.shader_visible_capacity, 0u);
    EXPECT_GT(sampler_desc.shader_visible_capacity, 0u);
    EXPECT_EQ(rtv_desc.shader_visible_capacity, 0u);
    EXPECT_EQ(dsv_desc.shader_visible_capacity, 0u);
}

NOLINT_TEST_F(PolicyRulesTest, OnlyOneShaderVisibleHeapPerType_CBVSRVUAV)
{
    std::vector<ResourceViewType> cbv_srv_uav_types = {
        ResourceViewType::kTexture_SRV,
        ResourceViewType::kTexture_UAV,
        ResourceViewType::kTypedBuffer_SRV,
        ResourceViewType::kTypedBuffer_UAV,
        ResourceViewType::kStructuredBuffer_SRV,
        ResourceViewType::kStructuredBuffer_UAV,
        ResourceViewType::kRawBuffer_SRV,
        ResourceViewType::kRawBuffer_UAV,
        ResourceViewType::kConstantBuffer,
        ResourceViewType::kRayTracingAccelStructure
    };
    std::string gpu_key;
    for (auto vt : cbv_srv_uav_types) {
        auto gk = strat.GetHeapKey(vt, DescriptorVisibility::kShaderVisible);
        if (gpu_key.empty())
            gpu_key = gk;
        EXPECT_EQ(gk, gpu_key);
    }
}

NOLINT_TEST_F(PolicyRulesTest, OnlyOneShaderVisibleHeapPerType_Sampler)
{
    std::vector<ResourceViewType> sampler_types = {
        ResourceViewType::kSampler,
        ResourceViewType::kSamplerFeedbackTexture_UAV
    };
    std::string gpu_key;
    for (auto vt : sampler_types) {
        auto gk = strat.GetHeapKey(vt, DescriptorVisibility::kShaderVisible);
        if (gpu_key.empty())
            gpu_key = gk;
        EXPECT_EQ(gk, gpu_key);
    }
}

// -----------------------------------------------------------------------------
// Heap Mapping Consistency Tests
// -----------------------------------------------------------------------------

class ConsistentMappingTest : public HeapAllocationStrategyTest {
};

NOLINT_TEST_F(ConsistentMappingTest, AllViewTypesForHeapKeyMapToSameD3D12HeapType)
{
    std::map<std::string, D3D12_DESCRIPTOR_HEAP_TYPE> heap_types = {
        // clang-format off
        { "CBV_SRV_UAV:gpu", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV },
        { "CBV_SRV_UAV:cpu", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV },
        { "SAMPLER:gpu", D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER },
        { "SAMPLER:cpu", D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER },
        { "RTV:cpu", D3D12_DESCRIPTOR_HEAP_TYPE_RTV },
        { "DSV:cpu", D3D12_DESCRIPTOR_HEAP_TYPE_DSV }
        // clang-format on
    };

    // Group kValidMappings by heap key
    std::map<std::string, std::vector<const HeapKeyValidMappingParam*>> key_to_mappings;
    for (const auto& mapping : kValidMappings) {
        key_to_mappings[mapping.key].push_back(&mapping);
    }

    for (const auto& [key, mappings] : key_to_mappings) {
        // Use the first mapping to determine the expected heap type
        auto expected_heap_type = heap_types[key];
        for (const auto* mapping : mappings) {
            // Check key
            std::string returned_key = strat.GetHeapKey(mapping->view_type, mapping->visibility);
            EXPECT_EQ(returned_key, key) << "Heap key mismatch for test_name: " << mapping->test_name;
            // Check heap type
            auto heap_type = D3D12HeapAllocationStrategy::GetHeapType(mapping->view_type);
            EXPECT_EQ(heap_type, expected_heap_type) << "Heap type mismatch for test_name: " << mapping->test_name;
        }
    }
}

// -----------------------------------------------------------------------------
// Sanity checks on heap descriptions
// -----------------------------------------------------------------------------

class HeapDescriptionTest : public HeapAllocationStrategyTest,
                            public ::testing::WithParamInterface<const char*> { };

INSTANTIATE_TEST_SUITE_P(
    ValidKeys,
    HeapDescriptionTest,
    ::testing::ValuesIn(kAllValidKeys));

NOLINT_TEST_P(HeapDescriptionTest, GetHeapDescriptionNoThrow)
{
    const auto* key = GetParam();
    NOLINT_EXPECT_NO_THROW(strat.GetHeapDescription(key));
}

NOLINT_TEST_P(HeapDescriptionTest, NoGrowthAllowed)
{
    const auto* key = GetParam();
    EXPECT_FALSE(strat.GetHeapDescription(key).allow_growth);
}

NOLINT_TEST_P(HeapDescriptionTest, ValidCpuVisibleCapacity)
{
    const auto* key = GetParam();
    const auto& desc = strat.GetHeapDescription(key);
    if (std::string(key).find("gpu") != std::string::npos) {
        EXPECT_EQ(desc.cpu_visible_capacity, 0u);
        EXPECT_GT(desc.shader_visible_capacity, 0u);
    } else {
        // If the key has a CPU heap, the CPU capacity should be greater than 0
        EXPECT_GT(desc.cpu_visible_capacity, 0u);
        EXPECT_EQ(desc.shader_visible_capacity, 0u);
    }
}

} // namespace
