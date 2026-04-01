#include <Oxygen/Testing/GTest.h>

#include <cstdint>

#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace {

using oxygen::engine::DepthPrePass;
using oxygen::engine::LightCullingPass;
using oxygen::engine::RenderContext;

TEST(RenderContextTest, ClearRegisteredPassesDropsPerViewPassState)
{
  auto context = RenderContext {};
  auto* depth_pass = reinterpret_cast<DepthPrePass*>(std::uintptr_t { 0x1000 });
  auto* light_culling_pass
    = reinterpret_cast<LightCullingPass*>(std::uintptr_t { 0x2000 });

  context.RegisterPass(depth_pass);
  context.RegisterPass(light_culling_pass);
  ASSERT_EQ(context.GetPass<DepthPrePass>(), depth_pass);
  ASSERT_EQ(context.GetPass<LightCullingPass>(), light_culling_pass);

  context.ClearRegisteredPasses();

  EXPECT_EQ(context.GetPass<DepthPrePass>(), nullptr);
  EXPECT_EQ(context.GetPass<LightCullingPass>(), nullptr);
}

} // namespace
