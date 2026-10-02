//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen;
namespace co = co;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

struct MaterialUvTransformDesc {
  float uv_scale[2] = { 1.0F, 1.0F };
  float uv_offset[2] = { 0.0F, 0.0F };
  float uv_rotation_radians = 0.0F;
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
  item.material.name = "Material_0";
  item.material.storage_name = "Material_0";
  item.request = MakeRequest();
  item.material.shader_requests = {
    MakeShaderRequest(ShaderType::kVertex,
      "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl", "VS"),
    MakeShaderRequest(ShaderType::kPixel,
      "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl", "PS"),
  };
  return item;
}

auto ReadMaterialDesc(const std::vector<std::byte>& bytes)
  -> data::pak::render::MaterialAssetDesc
{
  data::pak::render::MaterialAssetDesc desc {};
  if (bytes.size() < sizeof(desc)) {
    return desc;
  }
  std::memcpy(&desc, bytes.data(), sizeof(desc));
  return desc;
}

auto ReadShaderRefs(const std::vector<std::byte>& bytes, size_t count)
  -> std::vector<data::pak::render::ShaderReferenceDesc>
{
  std::vector<data::pak::render::ShaderReferenceDesc> refs;
  const size_t offset = sizeof(data::pak::render::MaterialAssetDesc);
  const size_t total = count * sizeof(data::pak::render::ShaderReferenceDesc);
  if (bytes.size() < offset + total) {
    return refs;
  }

  refs.resize(count);
  std::memcpy(refs.data(), bytes.data() + offset, total);
  return refs;
}

auto ReadUvTransform(const data::pak::render::MaterialAssetDesc& desc)
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
    [&](const ImportDiagnostic& diag) -> bool { return diag.code == code; });
}

auto CountDiagnosticsWithCode(const std::vector<ImportDiagnostic>& diagnostics,
  std::string_view code) -> size_t
{
  return static_cast<size_t>(
    std::count_if(diagnostics.begin(), diagnostics.end(),
      [&](const ImportDiagnostic& diag) -> bool { return diag.code == code; }));
}

auto ExpectedShaderStages(const std::vector<ShaderRequest>& requests)
  -> uint32_t
{
  uint32_t stages = 0;
  for (const auto& request : requests) {
    const uint32_t bit = 1U << request.shader_type;
    stages |= bit;
  }
  return stages;
}

auto ZeroContentHash(std::vector<std::byte> bytes) -> std::vector<std::byte>
{
  constexpr size_t kOffset
    = offsetof(data::pak::render::MaterialAssetDesc, header)
    + offsetof(data::pak::core::AssetHeader, content_hash);
  constexpr size_t kHashSize = sizeof(data::pak::core::ContentHashDigest);
  if (bytes.size() >= kOffset + kHashSize) {
    std::array<std::byte, kHashSize> zeros {};
    std::memcpy(bytes.data() + kOffset, zeros.data(), zeros.size());
  }
  return bytes;
}

//=== Fixtures
//===--------------------------------------------------------------//

class MaterialPipelineBasicTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

class MaterialPipelineOrmTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

class MaterialPipelineUvTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

class MaterialPipelineShaderTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

//=== Basic Behavior Tests
//===-----------------------------------------------------//

//! Verify content hash covers descriptor bytes and shader refs.
NOLINT_TEST_F(
  MaterialPipelineBasicTest, CollectComputesContentHashFromDescriptorBytes)
{
  // Arrange
  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&] -> co::Co<> {
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
  if (!result.cooked.has_value()) {
    FAIL() << "Expected result.cooked to contain a value";
  }

  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
  auto zeroed = ZeroContentHash(result.cooked->descriptor_bytes);
  const auto expected_hash = util::ComputeContentSha256(
    std::span<const std::byte>(zeroed.data(), zeroed.size()));

  EXPECT_EQ(desc.header.content_hash, expected_hash);
}

NOLINT_TEST_F(MaterialPipelineBasicTest, RejectsInvalidCompiledEmission)
{
  co::ThreadPool pool(loop_, 2);
  co::Run(loop_, [&] -> co::Co<> {
    MaterialPipeline pipeline(pool,
      MaterialPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .use_thread_pool = true,
      });
    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      for (const auto value : std::array {
             -1.0F,
             65505.0F,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
           }) {
        auto item = MakeBaseItem();
        item.material.inputs.emissive_factor[0] = value;
        co_await pipeline.Submit(std::move(item));
        const auto result = co_await pipeline.Collect();
        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.cooked.has_value());
        EXPECT_TRUE(HasDiagnosticCode(
          result.diagnostics, "material.emissive_factor_range"));
      }
      pipeline.Close();
      co_return co::kJoin;
    };
  });
}

//=== ORM Policy Tests
//===----------------------------------------------------------//

//! Verify auto ORM packing sets the packed flag and indices.
NOLINT_TEST_F(MaterialPipelineOrmTest, CollectAutoOrmPackedSetsFlags)
{
  for (const auto ao_index : std::array<uint32_t, 3> {
         data::pak::core::kNoResourceIndex.get(), 7U, 9U }) {
    // Arrange
    auto item = MakeBaseItem();
    item.material.orm_policy = OrmPolicy::kAuto;
    item.material.textures.metallic = MaterialTextureBinding {
      .index = 7,
      .assigned = true,
      .source_id = "orm",
      .uv_set = 0,
      .uv_transform = {},
    };
    item.material.textures.roughness = item.material.textures.metallic;
    if (ao_index != data::pak::core::kNoResourceIndex) {
      item.material.textures.ambient_occlusion
        = item.material.textures.metallic;
      item.material.textures.ambient_occlusion.index = ao_index;
      item.material.textures.ambient_occlusion.source_id
        = ao_index == 7U ? "orm" : "ao";
    }
    item.material.occlusion_mode = AmbientOcclusionMode::kStrength;
    item.material.inputs.ambient_occlusion = 0.5F;

    MaterialPipeline::WorkResult result;
    co::ThreadPool pool(loop_, 2);

    // Act
    co::Run(loop_, [&] -> co::Co<> {
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
    if (!result.cooked.has_value()) {
      FAIL() << "Expected result.cooked to contain a value";
    }
    const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);

    EXPECT_NE(desc.flags & data::pak::render::kMaterialFlag_GltfOrmPacked, 0U);
    EXPECT_EQ(
      desc.flags & data::pak::render::kMaterialFlag_NoTextureSampling, 0U);
    const auto& references = result.cooked->references;
    for (const auto reference :
      { desc.metallic_texture, desc.roughness_texture }) {
      const auto resolved
        = references.ResolveResource(reference, data::ResourceKind::kTexture);
      ASSERT_TRUE(resolved.has_value());
      EXPECT_EQ(*resolved, std::optional { oxygen::ResourceIndexT { 7U } });
    }
    const auto resolved_ao = references.ResolveResource(
      desc.ambient_occlusion_texture, data::ResourceKind::kTexture);
    ASSERT_TRUE(resolved_ao.has_value());
    if (ao_index == data::pak::core::kNoResourceIndex.get()) {
      EXPECT_FALSE(resolved_ao->has_value());
    } else {
      EXPECT_EQ(
        *resolved_ao, std::optional { oxygen::ResourceIndexT { ao_index } });
    }
    EXPECT_NE(
      desc.flags & data::pak::render::kMaterialFlag_AmbientOcclusionStrength,
      0U);
    EXPECT_EQ(desc.ambient_occlusion.get(), data::Unorm16 { 0.5F }.get());
  }
}

//! Verify force-packed ORM emits an error when inputs are incompatible.
NOLINT_TEST_F(MaterialPipelineOrmTest, CollectForcePackedInvalidEmitsError)
{
  // Arrange
  auto item = MakeBaseItem();
  item.material.orm_policy = OrmPolicy::kForcePacked;
  item.material.textures.metallic = MaterialTextureBinding {
    .index = 4,
    .assigned = true,
    .source_id = "metal",
    .uv_set = 0,
    .uv_transform = {},
  };
  item.material.textures.roughness = MaterialTextureBinding {
    .index = 5,
    .assigned = true,
    .source_id = "rough",
    .uv_set = 0,
    .uv_transform = {},
  };
  item.material.textures.ambient_occlusion = item.material.textures.metallic;

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&] -> co::Co<> {
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
NOLINT_TEST_F(MaterialPipelineUvTest, CollectSharedTransformWritesExtension)
{
  // Arrange
  auto item = MakeBaseItem();
  item.material.textures.base_color = MaterialTextureBinding {
    .index = 2,
    .assigned = true,
    .source_id = "base",
    .uv_set = 2,
    .uv_transform = { { 2.0F, 2.0F }, { 0.25F, 0.5F }, 0.1F },
  };

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&] -> co::Co<> {
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
  if (!result.cooked.has_value()) {
    FAIL() << "Expected result.cooked to contain a value";
  }
  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
  const auto uv = ReadUvTransform(desc);

  EXPECT_EQ(uv.uv_set, 2U);
  EXPECT_FLOAT_EQ(uv.uv_scale[0], 2.0F);
  EXPECT_FLOAT_EQ(uv.uv_scale[1], 2.0F);
  EXPECT_FLOAT_EQ(uv.uv_offset[0], 0.25F);
  EXPECT_FLOAT_EQ(uv.uv_offset[1], 0.5F);
  EXPECT_FLOAT_EQ(uv.uv_rotation_radians, 0.1F);
}

//! Verify mismatched UV transforms use the first assigned transform.
NOLINT_TEST_F(MaterialPipelineUvTest, CollectMismatchedTransformsUsesFirst)
{
  // Arrange
  auto item = MakeBaseItem();
  item.material.textures.base_color = MaterialTextureBinding {
    .index = 2,
    .assigned = true,
    .source_id = "base",
    .uv_set = 0,
    .uv_transform = { { 2.0F, 2.0F }, { 0.0F, 0.0F }, 0.0F },
  };
  item.material.textures.normal = MaterialTextureBinding {
    .index = 3,
    .assigned = true,
    .source_id = "normal",
    .uv_set = 1,
    .uv_transform = {},
  };

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&] -> co::Co<> {
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
  if (!result.cooked.has_value()) {
    FAIL() << "Expected result.cooked to contain a value";
  }
  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
  const auto uv = ReadUvTransform(desc);

  EXPECT_EQ(uv.uv_set, 0U);
  EXPECT_FLOAT_EQ(uv.uv_scale[0], 2.0F);
  EXPECT_FLOAT_EQ(uv.uv_scale[1], 2.0F);
  EXPECT_FLOAT_EQ(uv.uv_offset[0], 0.0F);
  EXPECT_FLOAT_EQ(uv.uv_offset[1], 0.0F);
  EXPECT_FLOAT_EQ(uv.uv_rotation_radians, 0.0F);
}

//=== Shader Reference Tests
//===----------------------------------------------------//

//! Verify shader stages are encoded and ordered by stage bit index.
NOLINT_TEST_F(MaterialPipelineShaderTest, CollectShaderStagesOrderedByBitIndex)
{
  // Arrange
  auto item = MakeBaseItem();
  item.material.shader_requests = {
    MakeShaderRequest(ShaderType::kPixel,
      "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl", "PS"),
    MakeShaderRequest(ShaderType::kVertex,
      "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl", "VS"),
  };
  const auto expected_stages
    = ExpectedShaderStages(item.material.shader_requests);

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&] -> co::Co<> {
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
  if (!result.cooked.has_value()) {
    FAIL() << "Expected result.cooked to contain a value";
  }

  const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
  EXPECT_EQ(desc.shader_stages, expected_stages);

  const auto ref_count = std::popcount(desc.shader_stages);
  const auto refs = ReadShaderRefs(result.cooked->descriptor_bytes, ref_count);
  ASSERT_EQ(refs.size(), ref_count);
  EXPECT_EQ(refs.at(0).shader_type, static_cast<uint8_t>(ShaderType::kVertex));
  EXPECT_EQ(refs.at(1).shader_type, static_cast<uint8_t>(ShaderType::kPixel));
}

//! Verify overlong shader strings emit truncation warnings.
NOLINT_TEST_F(
  MaterialPipelineShaderTest, CollectOverlongShaderStringsEmitWarnings)
{
  // Arrange
  auto item = MakeBaseItem();
  item.material.shader_requests = {
    MakeShaderRequest(ShaderType::kVertex, std::string(200, 's'),
      std::string(80, 'e'), std::string(300, 'd')),
  };

  MaterialPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&] -> co::Co<> {
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
    3U);
}

} // namespace
