//===----------------------------------------------------------------------===//
// Tests for DomainIndexMapper with generated DomainToken semantics
//===----------------------------------------------------------------------===//

#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Nexus/DomainIndexMapper.h>
#include <Oxygen/Nexus/Test/NexusMocks.h>

namespace {

using oxygen::nexus::DomainIndexMapper;
using oxygen::nexus::DomainKey;
using oxygen::nexus::testing::FakeAllocator;
namespace b = oxygen::bindless;
namespace g = oxygen::bindless::generated;

NOLINT_TEST(DomainIndexMapperTest,
  GetDomainRange_ValidDomain_ReturnsGeneratedRangeAndResolvesBack)
{
  FakeAllocator alloc;
  alloc.SetBase(
    g::kTexturesDomain, b::ShaderVisibleIndex { g::kTexturesShaderIndexBase });
  const DomainKey domain { .domain = g::kTexturesDomain };
  DomainIndexMapper mapper(alloc, { domain });

  const auto range = mapper.GetDomainRange(domain);
  ASSERT_TRUE(range.has_value());
  EXPECT_EQ(
    range->start, b::ShaderVisibleIndex { g::kTexturesShaderIndexBase });
  EXPECT_EQ(range->capacity, b::Capacity { g::kTexturesCapacity });

  const auto resolved = mapper.ResolveDomain(
    b::ShaderVisibleIndex { g::kTexturesShaderIndexBase });
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->domain, domain.domain);
}

NOLINT_TEST(DomainIndexMapperTest, EmptyDomainList_HasNoMappings)
{
  FakeAllocator alloc;
  DomainIndexMapper mapper(alloc, {});

  EXPECT_FALSE(mapper.GetDomainRange(DomainKey { .domain = g::kTexturesDomain })
      .has_value());
  EXPECT_FALSE(
    mapper.ResolveDomain(b::ShaderVisibleIndex { g::kTexturesShaderIndexBase })
      .has_value());
}

NOLINT_TEST(DomainIndexMapperTest, MultipleDomains_ResolveCorrectlyAtBoundaries)
{
  FakeAllocator alloc;
  alloc.SetBase(g::kMaterialsDomain,
    b::ShaderVisibleIndex { g::kMaterialsShaderIndexBase });
  alloc.SetBase(
    g::kTexturesDomain, b::ShaderVisibleIndex { g::kTexturesShaderIndexBase });
  const DomainKey materials { .domain = g::kMaterialsDomain };
  const DomainKey textures { .domain = g::kTexturesDomain };
  DomainIndexMapper mapper(alloc, { materials, textures });

  const auto last_material_index = b::ShaderVisibleIndex {
    g::kMaterialsShaderIndexBase + g::kMaterialsCapacity - 1U,
  };
  const auto first_texture_index = b::ShaderVisibleIndex {
    g::kTexturesShaderIndexBase,
  };

  const auto resolved_material = mapper.ResolveDomain(last_material_index);
  const auto resolved_texture = mapper.ResolveDomain(first_texture_index);

  ASSERT_TRUE(resolved_material.has_value());
  ASSERT_TRUE(resolved_texture.has_value());
  EXPECT_EQ(resolved_material->domain, g::kMaterialsDomain);
  EXPECT_EQ(resolved_texture->domain, g::kTexturesDomain);
}

NOLINT_TEST(DomainIndexMapperTest, UnknownDomain_IsIgnored)
{
  FakeAllocator alloc;
  const DomainKey known { .domain = g::kTexturesDomain };
  const DomainKey unknown { .domain = g::kInvalidDomainToken };
  DomainIndexMapper mapper(alloc, { known, unknown });

  EXPECT_TRUE(mapper.GetDomainRange(known).has_value());
  EXPECT_FALSE(mapper.GetDomainRange(unknown).has_value());
}

NOLINT_TEST(DomainIndexMapperTest, ConcurrentReads_AreDeterministic)
{
  FakeAllocator alloc;
  const DomainKey materials { .domain = g::kMaterialsDomain };
  const DomainKey textures { .domain = g::kTexturesDomain };
  DomainIndexMapper mapper(alloc, { materials, textures });

  std::atomic<bool> ok { true };
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 500; ++j) {
        const auto a = mapper.ResolveDomain(
          b::ShaderVisibleIndex { g::kMaterialsShaderIndexBase });
        const auto bres = mapper.ResolveDomain(
          b::ShaderVisibleIndex { g::kTexturesShaderIndexBase });
        if (!a.has_value() || !bres.has_value()
          || a->domain != g::kMaterialsDomain
          || bres->domain != g::kTexturesDomain) {
          ok.store(false);
          return;
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_TRUE(ok.load());
}

} // namespace
