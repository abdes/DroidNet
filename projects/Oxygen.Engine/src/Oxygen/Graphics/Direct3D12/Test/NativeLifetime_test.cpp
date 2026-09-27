//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DebugLayer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/NativeLifetime.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics::d3d12::testing {
namespace {
  class NativeLifetimeTest : public OffscreenTestFixture {
  protected:
    auto BackendConfigJson() const -> std::string override
    {
      return R"({"enable_debug_layer":true})";
    }

    auto TearDown() -> void override
    {
      const bool retained = retained_texture_ != nullptr;
      OffscreenTestFixture::TearDown();
      EXPECT_TRUE(facade_.expired());
      EXPECT_EQ(debug_layer_.expired(), !retained);
      retained_texture_.reset();
      EXPECT_TRUE(texture_observer_.expired());
      EXPECT_TRUE(debug_layer_.expired());
    }

    auto ObserveLifetime() -> void
    {
      facade_ = GetGraphicsShared();
      const auto& native = Backend().GetNativeLifetime();
      ASSERT_NE(native->debug_layer, nullptr);
      debug_layer_ = native->debug_layer;
    }

    auto RetainTexture() -> void
    {
      auto description = graphics::TextureDesc {};
      description.width = 4U;
      description.height = 4U;
      description.format = Format::kRGBA8UNorm;
      description.is_shader_resource = true;
      description.debug_name = "Retained native lifetime";
      retained_texture_ = Backend().CreateTexture(description);
      ASSERT_NE(retained_texture_, nullptr);
      texture_observer_ = retained_texture_;
      const auto self = retained_texture_->shared_from_this();
      EXPECT_EQ(self.get(), retained_texture_.get());
      EXPECT_FALSE(self.owner_before(retained_texture_));
      EXPECT_FALSE(retained_texture_.owner_before(self));
    }

  private:
    std::shared_ptr<graphics::Texture> retained_texture_;
    std::weak_ptr<graphics::Texture> texture_observer_;
    std::weak_ptr<Graphics> facade_;
    std::weak_ptr<DebugLayer> debug_layer_;
  };
} // namespace

NOLINT_TEST_F(NativeLifetimeTest, DiagnosticsRetireWithFacadeWithoutReaders)
{
  ObserveLifetime();
}

NOLINT_TEST_F(NativeLifetimeTest, DiagnosticsOutliveFacadeUntilTextureRetires)
{
  ObserveLifetime();
  RetainTexture();
}
} // namespace oxygen::graphics::d3d12::testing
