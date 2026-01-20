//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen;
namespace co = oxygen::co;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

struct MaterialUvTransformDesc {
  float uv_scale[2] = { 1.0f, 1.0f };
  float uv_offset[2] = { 0.0f, 0.0f };
  float uv_rotation_radians = 0.0f;
  uint8_t uv_set = 0;
};

auto MakeShaderRequest(ShaderType stage, std::string source_path,
  std::string entry_point, std::string defines = {}) -> ShaderRequest
{
  return ShaderRequest {
    .shader_type = static_cast<uint8_t>(stage),
    .source_path = std::move(source_path),
    .entry_point = std::move(entry_point),
    .defines = std::move(defines),
    .shader_hash = 0,
  };
}

auto MakeRequest() -> ImportRequest
{
  ImportRequest request;
  request.source_path = "Test.fbx";
  return request;
}

auto MakeBaseItem() -> MaterialPipeline::WorkItem
{
  MaterialPipeline::WorkItem item;
  item.source_id = "mat0";
  item.material_name = "Material_0";
  item.storage_material_name = "Material_0";
  item.request = MakeRequest();
  item.shader_requests = {
    MakeShaderRequest(
      ShaderType::kVertex, "Passes/Forward/ForwardMesh_VS.hlsl", "VS"),
    MakeShaderRequest(
      ShaderType::kPixel, "Passes/Forward/ForwardMesh_PS.hlsl", "PS"),
  };
  return item;
}

auto ReadMaterialDesc(const std::vector<std::byte>& bytes)
  -> data::pak::MaterialAssetDesc
{
  data::pak::MaterialAssetDesc desc {};
  if (bytes.size() < sizeof(desc)) {
    return desc;
  }
  std::memcpy(&desc, bytes.data(), sizeof(desc));
  return desc;
}

auto ReadShaderRefs(const std::vector<std::byte>& bytes, size_t count)
  -> std::vector<data::pak::ShaderReferenceDesc>
{
  std::vector<data::pak::ShaderReferenceDesc> refs;
  const size_t offset = sizeof(data::pak::MaterialAssetDesc);
  const size_t total = count * sizeof(data::pak::ShaderReferenceDesc);
  if (bytes.size() < offset + total) {
    return refs;
  }

  refs.resize(count);
  std::memcpy(refs.data(), bytes.data() + offset, total);
  return refs;
}

auto ReadUvTransform(const data::pak::MaterialAssetDesc& desc)
  -> MaterialUvTransformDesc
{
  MaterialUvTransformDesc out {};
  out.uv_scale[0] = desc.uv_scale[0];
  out.uv_scale[1] = desc.uv_scale[1];
  out.uv_offset[0] = desc.uv_offset[0];
  out.uv_offset[1] = desc.uv_offset[1];
  out.uv_rotation_radians = desc.uv_rotation_radians;
  out.uv_set = desc.uv_set;
  return out;
}

auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
  std::string_view code) -> bool
{
  return std::any_of(diagnostics.begin(), diagnostics.end(),
    [&](const ImportDiagnostic& diag) { return diag.code == code; });
}

auto CountDiagnosticsWithCode(const std::vector<ImportDiagnostic>& diagnostics,
  std::string_view code) -> size_t
{
  return static_cast<size_t>(
    std::count_if(diagnostics.begin(), diagnostics.end(),
      [&](const ImportDiagnostic& diag) { return diag.code == code; }));
}

auto ExpectedShaderStages(const std::vector<ShaderRequest>& requests)
  -> uint32_t
{
  uint32_t stages = 0;
  for (const auto& request : requests) {
    const uint32_t bit = 1u << request.shader_type;
    stages |= bit;
  }
  return stages;
}

auto ZeroContentHash(std::vector<std::byte> bytes) -> std::vector<std::byte>
{
  constexpr size_t kOffset = offsetof(data::pak::MaterialAssetDesc, header)
    + offsetof(data::pak::AssetHeader, content_hash);
  if (bytes.size() >= kOffset + sizeof(uint64_t)) {
    uint64_t zero = 0;
    std::memcpy(bytes.data() + kOffset, &zero, sizeof(zero));
  }
  return bytes;
}

//=== Fixtures
//===--------------------------------------------------------------//

class MaterialPipelineBasicTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

class MaterialPipelineOrmTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

class MaterialPipelineUvTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

class MaterialPipelineShaderTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//=== Basic Behavior Tests
//===-----------------------------------------------------//

//! Verify content hash covers descriptor bytes and shader refs.
NOLINT_TEST_F(
  MaterialPipelineBasicTest, Collect_ComputesContentHashFromDescriptorBytes)
{
  // Arrange
  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MaterialPipeline pipeline(pool,
      MaterialPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .use_thread_pool = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeBaseItem());
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return co::kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());

  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
  auto zeroed = ZeroContentHash(result.cooked->descriptor_bytes);
  const auto expected_hash = util::ComputeContentHash(
    std::span<const std::byte>(zeroed.data(), zeroed.size()));

  EXPECT_EQ(desc.header.content_hash, expected_hash);
}

//=== ORM Policy Tests
//===----------------------------------------------------------//

//! Verify auto ORM packing sets the packed flag and indices.
NOLINT_TEST_F(MaterialPipelineOrmTest, Collect_AutoOrmPacked_SetsFlags)
{
  // Arrange
  auto item = MakeBaseItem();
  item.orm_policy = OrmPolicy::kAuto;
  item.textures.metallic = MaterialTextureBinding {
    .index = 7,
    .assigned = true,
    .source_id = "orm",
    .uv_set = 0,
    .uv_transform = {},
  };
  item.textures.roughness = item.textures.metallic;
  item.textures.ambient_occlusion = item.textures.metallic;

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MaterialPipeline pipeline(pool,
      MaterialPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .use_thread_pool = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return co::kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);

  EXPECT_NE(desc.flags & data::pak::kMaterialFlag_GltfOrmPacked, 0u);
  EXPECT_EQ(desc.flags & data::pak::kMaterialFlag_NoTextureSampling, 0u);
  EXPECT_EQ(desc.metallic_texture, 7u);
  EXPECT_EQ(desc.roughness_texture, 7u);
  EXPECT_EQ(desc.ambient_occlusion_texture, 7u);
}

//! Verify force-packed ORM emits an error when inputs are incompatible.
NOLINT_TEST_F(MaterialPipelineOrmTest, Collect_ForcePacked_Invalid_EmitsError)
{
  // Arrange
  auto item = MakeBaseItem();
  item.orm_policy = OrmPolicy::kForcePacked;
  item.textures.metallic = MaterialTextureBinding {
    .index = 4,
    .assigned = true,
    .source_id = "metal",
    .uv_set = 0,
    .uv_transform = {},
  };
  item.textures.roughness = MaterialTextureBinding {
    .index = 5,
    .assigned = true,
    .source_id = "rough",
    .uv_set = 0,
    .uv_transform = {},
  };
  item.textures.ambient_occlusion = item.textures.metallic;

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MaterialPipeline pipeline(pool,
      MaterialPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .use_thread_pool = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return co::kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "material.orm_policy"));
}

//=== UV Transform Tests
//===---------------------------------------------------------//

//! Verify UV extension is populated when all assigned slots share a transform.
NOLINT_TEST_F(MaterialPipelineUvTest, Collect_SharedTransform_WritesExtension)
{
  // Arrange
  auto item = MakeBaseItem();
  item.textures.base_color = MaterialTextureBinding {
    .index = 2,
    .assigned = true,
    .source_id = "base",
    .uv_set = 2,
    .uv_transform = { { 2.0f, 2.0f }, { 0.25f, 0.5f }, 0.1f },
  };

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MaterialPipeline pipeline(pool,
      MaterialPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .use_thread_pool = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return co::kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
  const auto uv = ReadUvTransform(desc);

  EXPECT_EQ(uv.uv_set, 2u);
  EXPECT_FLOAT_EQ(uv.uv_scale[0], 2.0f);
  EXPECT_FLOAT_EQ(uv.uv_scale[1], 2.0f);
  EXPECT_FLOAT_EQ(uv.uv_offset[0], 0.25f);
  EXPECT_FLOAT_EQ(uv.uv_offset[1], 0.5f);
  EXPECT_FLOAT_EQ(uv.uv_rotation_radians, 0.1f);
}

//! Verify mismatched UV transforms use the first assigned transform.
NOLINT_TEST_F(MaterialPipelineUvTest, Collect_MismatchedTransforms_UsesFirst)
{
  // Arrange
  auto item = MakeBaseItem();
  item.textures.base_color = MaterialTextureBinding {
    .index = 2,
    .assigned = true,
    .source_id = "base",
    .uv_set = 0,
    .uv_transform = { { 2.0f, 2.0f }, { 0.0f, 0.0f }, 0.0f },
  };
  item.textures.normal = MaterialTextureBinding {
    .index = 3,
    .assigned = true,
    .source_id = "normal",
    .uv_set = 1,
    .uv_transform = {},
  };

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MaterialPipeline pipeline(pool,
      MaterialPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .use_thread_pool = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return co::kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
  const auto uv = ReadUvTransform(desc);

  EXPECT_EQ(uv.uv_set, 0u);
  EXPECT_FLOAT_EQ(uv.uv_scale[0], 2.0f);
  EXPECT_FLOAT_EQ(uv.uv_scale[1], 2.0f);
  EXPECT_FLOAT_EQ(uv.uv_offset[0], 0.0f);
  EXPECT_FLOAT_EQ(uv.uv_offset[1], 0.0f);
  EXPECT_FLOAT_EQ(uv.uv_rotation_radians, 0.0f);
}

//=== Shader Reference Tests
//===----------------------------------------------------//

//! Verify shader stages are encoded and ordered by stage bit index.
NOLINT_TEST_F(MaterialPipelineShaderTest, Collect_ShaderStagesOrderedByBitIndex)
{
  // Arrange
  auto item = MakeBaseItem();
  item.shader_requests = {
    MakeShaderRequest(
      ShaderType::kPixel, "Passes/Forward/ForwardMesh_PS.hlsl", "PS"),
    MakeShaderRequest(
      ShaderType::kVertex, "Passes/Forward/ForwardMesh_VS.hlsl", "VS"),
  };
  const auto expected_stages = ExpectedShaderStages(item.shader_requests);

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MaterialPipeline pipeline(pool,
      MaterialPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .use_thread_pool = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return co::kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());

  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
  EXPECT_EQ(desc.shader_stages, expected_stages);

  const auto ref_count = std::popcount(desc.shader_stages);
  const auto refs = ReadShaderRefs(result.cooked->descriptor_bytes, ref_count);
  ASSERT_EQ(refs.size(), ref_count);
  EXPECT_EQ(refs[0].shader_type, static_cast<uint8_t>(ShaderType::kVertex));
  EXPECT_EQ(refs[1].shader_type, static_cast<uint8_t>(ShaderType::kPixel));
}

//! Verify overlong shader strings emit truncation warnings.
NOLINT_TEST_F(
  MaterialPipelineShaderTest, Collect_OverlongShaderStrings_EmitWarnings)
{
  // Arrange
  auto item = MakeBaseItem();
  item.shader_requests = {
    MakeShaderRequest(ShaderType::kVertex, std::string(200, 's'),
      std::string(80, 'e'), std::string(300, 'd')),
  };

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MaterialPipeline pipeline(pool,
      MaterialPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .use_thread_pool = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return co::kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  EXPECT_EQ(CountDiagnosticsWithCode(
              result.diagnostics, "material.shader_ref_truncated"),
    3u);
}

} // namespace
