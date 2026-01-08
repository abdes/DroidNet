//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Shaders.h>

#include <optional>
#include <stdexcept>
#include <string>

#include <Oxygen/Testing/GTest.h>

using namespace oxygen::graphics;
using oxygen::ShaderType;

namespace {

//! Canonicalization rejects invalid or unsafe requests.
NOLINT_TEST(ShaderRequestTest, Canonicalize_RejectsInvalidFields)
{
  {
    auto req = ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "",
      .entry_point = "main",
      .defines = {},
    };

    NOLINT_EXPECT_THROW(
      static_cast<void>(CanonicalizeShaderRequest(req)), std::invalid_argument);
  }

  {
    auto req = ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "C:/abs.hlsl",
      .entry_point = "main",
      .defines = {},
    };

    NOLINT_EXPECT_THROW(
      static_cast<void>(CanonicalizeShaderRequest(req)), std::invalid_argument);
  }

  {
    auto req = ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "shaders:bad.hlsl",
      .entry_point = "main",
      .defines = {},
    };

    NOLINT_EXPECT_THROW(
      static_cast<void>(CanonicalizeShaderRequest(req)), std::invalid_argument);
  }

  {
    auto req = ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "shaders/test.hlsl",
      .entry_point = "1main",
      .defines = {},
    };

    NOLINT_EXPECT_THROW(
      static_cast<void>(CanonicalizeShaderRequest(req)), std::invalid_argument);
  }

  {
    auto req = ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "shaders/test.hlsl",
      .entry_point = "main",
      .defines = {
        ShaderDefine { .name = "Foo", .value = std::nullopt },
      },
    };

    NOLINT_EXPECT_THROW(
      static_cast<void>(CanonicalizeShaderRequest(req)), std::invalid_argument);
  }

  {
    auto req = ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "shaders/test.hlsl",
      .entry_point = "main",
      .defines = {
        ShaderDefine { .name = "FOO", .value = std::string { "bad value" } },
      },
    };

    NOLINT_EXPECT_THROW(
      static_cast<void>(CanonicalizeShaderRequest(req)), std::invalid_argument);
  }
}

//! Canonicalization normalizes paths and sorts/validates defines.
NOLINT_TEST(ShaderRequestTest, Canonicalize_NormalizesAndSorts)
{
  auto req = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "./shaders\\common/../test.hlsl",
    .entry_point = "main",
    .defines = {
      ShaderDefine { .name = "USE_FOG", .value = std::nullopt },
      ShaderDefine { .name = "ALPHA_TEST", .value = std::string { "1" } },
    },
  };

  const auto canonical = CanonicalizeShaderRequest(req);

  EXPECT_EQ(canonical.source_path, "shaders/test.hlsl");

  ASSERT_EQ(canonical.defines.size(), 2U);
  EXPECT_EQ(canonical.defines[0].name, "ALPHA_TEST");
  ASSERT_TRUE(canonical.defines[0].value.has_value());
  EXPECT_EQ(*canonical.defines[0].value, "1");

  EXPECT_EQ(canonical.defines[1].name, "USE_FOG");
  EXPECT_FALSE(canonical.defines[1].value.has_value());
}

//! Canonicalization rejects duplicate define names.
NOLINT_TEST(ShaderRequestTest, Canonicalize_RejectsDuplicateDefineNames)
{
  auto req = ShaderRequest {
    .stage = ShaderType::kCompute,
    .source_path = "shaders/test.hlsl",
    .entry_point = "main",
    .defines = {
      ShaderDefine { .name = "FOO", .value = std::nullopt },
      ShaderDefine { .name = "FOO", .value = std::string { "1" } },
    },
  };

  NOLINT_EXPECT_THROW(
    static_cast<void>(CanonicalizeShaderRequest(req)), std::invalid_argument);
}

//! Cache keys are stable and depend only on the canonical request.
NOLINT_TEST(ShaderRequestTest, ComputeShaderRequestKey_UsesCanonicalForm)
{
  auto canonical = ShaderRequest {
    .stage = ShaderType::kVertex,
    .source_path = "shaders/test.hlsl",
    .entry_point = "main",
    .defines = {
      ShaderDefine { .name = "ALPHA_TEST", .value = std::string { "1" } },
      ShaderDefine { .name = "USE_FOG", .value = std::nullopt },
    },
  };

  auto non_canonical = ShaderRequest {
    .stage = ShaderType::kVertex,
    .source_path = "./shaders\\common/../test.hlsl",
    .entry_point = "main",
    .defines = {
      ShaderDefine { .name = "USE_FOG", .value = std::nullopt },
      ShaderDefine { .name = "ALPHA_TEST", .value = std::string { "1" } },
    },
  };

  const auto key_a = ComputeShaderRequestKey(canonical);
  const auto key_b = ComputeShaderRequestKey(non_canonical);

  EXPECT_EQ(key_a, key_b);

  auto different = canonical;
  different.entry_point = "main2";
  EXPECT_NE(key_a, ComputeShaderRequestKey(different));
}

//! Cache key computation rejects invalid requests.
NOLINT_TEST(ShaderRequestTest, ComputeShaderRequestKey_RejectsInvalidRequest)
{
  auto req = ShaderRequest {
    .stage = ShaderType::kVertex,
    .source_path = "C:/abs.hlsl",
    .entry_point = "main",
    .defines = {},
  };

  NOLINT_EXPECT_THROW(
    static_cast<void>(ComputeShaderRequestKey(req)), std::invalid_argument);
}

//! Same shader with different defines produces different cache keys.
//! This validates that material permutations (e.g., ALPHA_TEST) result in
//! distinct PSO variants.
NOLINT_TEST(ShaderRequestTest, DifferentDefines_ProduceDifferentKeys)
{
  // Opaque path: no defines
  auto opaque_request = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
    .entry_point = "PS",
    .defines = {},
  };

  // Masked path: ALPHA_TEST=1
  auto masked_request = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
    .entry_point = "PS",
    .defines = {
      ShaderDefine { .name = "ALPHA_TEST", .value = std::string { "1" } },
    },
  };

  const auto opaque_key = ComputeShaderRequestKey(opaque_request);
  const auto masked_key = ComputeShaderRequestKey(masked_request);

  // Different defines must produce different keys
  EXPECT_NE(opaque_key, masked_key);
}

//! Identical defines produce identical cache keys (PSO reuse).
NOLINT_TEST(ShaderRequestTest, IdenticalDefines_ProduceSameKey)
{
  auto request_a = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
    .entry_point = "PS",
    .defines = {
      ShaderDefine { .name = "ALPHA_TEST", .value = std::string { "1" } },
    },
  };

  auto request_b = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
    .entry_point = "PS",
    .defines = {
      ShaderDefine { .name = "ALPHA_TEST", .value = std::string { "1" } },
    },
  };

  const auto key_a = ComputeShaderRequestKey(request_a);
  const auto key_b = ComputeShaderRequestKey(request_b);

  // Identical requests must produce identical keys
  EXPECT_EQ(key_a, key_b);
}

//! Multiple defines produce a key different from single define.
NOLINT_TEST(ShaderRequestTest, MultipleDefines_ProduceDifferentKeyFromSingle)
{
  auto single_define = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
    .entry_point = "PS",
    .defines = {
      ShaderDefine { .name = "ALPHA_TEST", .value = std::string { "1" } },
    },
  };

  auto multiple_defines = ShaderRequest {
    .stage = ShaderType::kPixel,
    .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
    .entry_point = "PS",
    .defines = {
      ShaderDefine { .name = "ALPHA_TEST", .value = std::string { "1" } },
      ShaderDefine { .name = "HAS_EMISSIVE", .value = std::string { "1" } },
    },
  };

  const auto single_key = ComputeShaderRequestKey(single_define);
  const auto multi_key = ComputeShaderRequestKey(multiple_defines);

  EXPECT_NE(single_key, multi_key);
}

} // namespace
