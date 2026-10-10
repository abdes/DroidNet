//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::ViewId;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureReadbackRequest;
using oxygen::vortex::HzbPyramidBuilder;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::ScreenHzbModule;

//! Row-major texels of each mip, mip 0 first.
using Mips = std::vector<std::vector<float>>;

constexpr float kBackground = 0.5F;

//! A source texture and the rect of it the pyramid reduces.
struct SourceCase {
  std::uint32_t texture_width;
  std::uint32_t texture_height;
  std::uint32_t origin_x;
  std::uint32_t origin_y;
  std::uint32_t width;
  std::uint32_t height;
};

//! A 1366 x 768 view at an offset inside a larger texture.
constexpr auto kOffsetViewSource = SourceCase {
  .texture_width = 1400U,
  .texture_height = 800U,
  .origin_x = 17U,
  .origin_y = 9U,
  .width = 1366U,
  .height = 768U,
};

auto MipExtent(const std::uint32_t root, const std::uint32_t mip)
  -> std::uint32_t
{
  return (std::max)(1U, root >> mip);
}

//! Reference reduction: mip-0 texel `x` reduces source pixels `2x` and
//! `2x + 1` clamped to the rect; later mips reduce clamped 2 x 2 blocks.
auto ReferencePyramid(const std::vector<float>& texels,
  const SourceCase& source, const bool closest) -> Mips
{
  const auto root_width = HzbPyramidBuilder::ComputeRootExtent(source.width);
  const auto root_height = HzbPyramidBuilder::ComputeRootExtent(source.height);
  const auto mip_count
    = HzbPyramidBuilder::ComputeMipCount(root_width, root_height);
  const auto reduce = [closest](const float a, const float b) -> float {
    return closest ? (std::max)(a, b) : (std::min)(a, b);
  };

  auto mips = Mips {};
  for (std::uint32_t mip = 0U; mip < mip_count; ++mip) {
    const auto input_width
      = mip == 0U ? source.width : MipExtent(root_width, mip - 1U);
    const auto input_height
      = mip == 0U ? source.height : MipExtent(root_height, mip - 1U);
    const auto load
      = [&](const std::uint32_t x, const std::uint32_t y) -> float {
      const auto cx = (std::min)(x, input_width - 1U);
      const auto cy = (std::min)(y, input_height - 1U);
      if (mip == 0U) {
        return texels.at((static_cast<std::size_t>(source.origin_y + cy)
                           * source.texture_width)
          + source.origin_x + cx);
      }
      return mips.back().at((static_cast<std::size_t>(cy) * input_width) + cx);
    };
    const auto width = MipExtent(root_width, mip);
    const auto height = MipExtent(root_height, mip);
    auto level = std::vector<float>(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        level.at((static_cast<std::size_t>(y) * width) + x)
          = reduce(reduce(load(2U * x, 2U * y), load((2U * x) + 1U, 2U * y)),
            reduce(
              load(2U * x, (2U * y) + 1U), load((2U * x) + 1U, (2U * y) + 1U)));
      }
    }
    mips.push_back(std::move(level));
  }
  return mips;
}

//! Deterministic depths in [0, 1].
auto NoiseTexels(const std::size_t count) -> std::vector<float>
{
  auto texels = std::vector<float>(count);
  auto state = std::uint32_t { 0x9E3779B9U };
  for (auto& texel : texels) {
    state = (state * 1664525U) + 1013904223U;
    texel = static_cast<float>(state >> 8U) / static_cast<float>(1U << 24U);
  }
  return texels;
}

class HzbPyramidGpuTest
  : public oxygen::vortex::testing::exposure::ExposureGpuTest {
protected:
  auto MakeSource(const SourceCase& source, const std::span<const float> texels)
    -> std::shared_ptr<Texture>
  {
    auto desc = oxygen::graphics::TextureDesc {};
    desc.width = source.texture_width;
    desc.height = source.texture_height;
    desc.format = Format::kR32Float;
    desc.texture_type = TextureType::kTexture2D;
    desc.debug_name = "HZB test source";
    desc.is_shader_resource = true;
    desc.initial_state = ResourceStates::kCommon;
    auto texture = CreateRegisteredTexture(desc);
    WriteTexels(*texture, std::as_bytes(texels), sizeof(float),
      ResourceStates::kShaderResource);
    return texture;
  }

  auto MakePyramid(const SourceCase& source) -> std::shared_ptr<Texture>
  {
    return CreateRegisteredTexture(HzbPyramidBuilder::MakeTextureDesc(
      source.width, source.height, "HZB test pyramid"));
  }

  static auto Rect(const Texture& texture, const SourceCase& source)
    -> HzbPyramidBuilder::Source
  {
    return HzbPyramidBuilder::Source {
      .depth = oxygen::observer_ptr { &texture },
      .origin_x = source.origin_x,
      .origin_y = source.origin_y,
      .width = source.width,
      .height = source.height,
    };
  }

  //! Starts a new frame, so each build gets fresh transient constants.
  auto NextFrame() -> void
  {
    ctx_.frame_sequence = oxygen::frame::SequenceNumber { ++sequence_ };
  }

  auto Build(HzbPyramidBuilder& builder, const HzbPyramidBuilder::Source& rect,
    const HzbPyramidBuilder::Targets& targets) -> bool
  {
    NextFrame();
    return SubmitCommands(
      "HZB test build", [&](CommandRecorder& recorder) -> bool {
        return builder.Build(ctx_, recorder, rect, targets);
      });
  }

  auto ReadMips(const Texture& texture) -> Mips
  {
    const auto& desc = texture.GetDescriptor();
    auto mips = Mips {};
    for (std::uint32_t mip = 0U; mip < desc.mip_levels; ++mip) {
      auto readback
        = GetReadbackManager()->CreateTextureReadback("HZB test readback");
      {
        auto recorder = AcquireRecorder("HZB test readback");
        CHECK_F(recorder->AdoptKnownResourceState(texture));
        CHECK_F(readback
            ->EnqueueCopy(*recorder, texture,
              TextureReadbackRequest {
                .src_slice = { .mip_level = mip },
              })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      CHECK_F(mapped.has_value());
      const auto bytes = MappedTextureBytes(*mapped, sizeof(float));
      const auto row_pitch = mapped->Layout().row_pitch.get();
      const auto width = MipExtent(desc.width, mip);
      const auto height = MipExtent(desc.height, mip);
      auto level = std::vector<float>(static_cast<std::size_t>(width) * height);
      const auto rows = std::span(level);
      for (std::uint32_t y = 0U; y < height; ++y) {
        const auto row
          = rows.subspan(static_cast<std::size_t>(y) * width, width);
        std::memcpy(
          row.data(), bytes.subspan(y * row_pitch).data(), row.size_bytes());
      }
      mips.push_back(std::move(level));
    }
    return mips;
  }
};

//! A single foreground pixel anywhere in a non-power-of-two view, including
//! its last row and column, reaches every mip of both pyramids.
NOLINT_TEST_F(HzbPyramidGpuTest, SingleForegroundPixelReachesEveryMip)
{
  auto builder = HzbPyramidBuilder(*renderer_, "HZB test builder");
  for (const auto& [width, height] : std::array {
         std::array { 1920U, 1080U },
         std::array { 1366U, 768U },
       }) {
    const auto source = SourceCase {
      .texture_width = width,
      .texture_height = height,
      .origin_x = 0U,
      .origin_y = 0U,
      .width = width,
      .height = height,
    };
    for (const auto& [px, py] : std::array {
           std::array { 0U, 0U },
           std::array { width - 1U, 0U },
           std::array { 0U, height - 1U },
           std::array { width - 1U, height - 1U },
           std::array { (width / 2U) + 1U, (height / 2U) + 1U },
         }) {
      // Closest = max sees a near (1) pixel; furthest = min a far (0) one.
      for (const bool closest : { true, false }) {
        TRACE_GCHECK_F(([&] -> void {
          auto texels = std::vector<float>(
            static_cast<std::size_t>(width) * height, kBackground);
          const auto foreground = closest ? 1.0F : 0.0F;
          texels.at((static_cast<std::size_t>(py) * width) + px) = foreground;
          const auto depth = MakeSource(source, texels);
          const auto pyramid = MakePyramid(source);

          ASSERT_TRUE(Build(builder, Rect(*depth, source),
            closest ? HzbPyramidBuilder::Targets { .closest
                        = oxygen::observer_ptr { pyramid.get() } }
                    : HzbPyramidBuilder::Targets {
                        .furthest = oxygen::observer_ptr { pyramid.get() } }));
          const auto mips = ReadMips(*pyramid);

          for (std::uint32_t mip = 0U; mip < mips.size(); ++mip) {
            const auto mip_width
              = MipExtent(pyramid->GetDescriptor().width, mip);
            const auto x = px >> (mip + 1U);
            const auto y = py >> (mip + 1U);
            EXPECT_EQ(
              mips.at(mip).at((static_cast<std::size_t>(y) * mip_width) + x),
              foreground)
              << "mip " << mip;
          }
        }()),
          fmt::format("{}x{} pixel ({}, {}) {}", width, height, px, py,
            closest ? "closest" : "furthest"));
      }
    }
  }
}

//! Both pyramids equal the reference reduction, for sub-rects with an origin,
//! views that need the tail dispatch, and roots with a 1-texel axis.
NOLINT_TEST_F(HzbPyramidGpuTest, PyramidsMatchCpuReference)
{
  auto builder = HzbPyramidBuilder(*renderer_, "HZB test builder");
  for (const auto& source : std::array {
         kOffsetViewSource,
         SourceCase {
           .texture_width = 1920U,
           .texture_height = 1080U,
           .origin_x = 0U,
           .origin_y = 0U,
           .width = 1920U,
           .height = 1080U,
         },
         SourceCase {
           .texture_width = 64U,
           .texture_height = 16U,
           .origin_x = 3U,
           .origin_y = 2U,
           .width = 37U,
           .height = 5U,
         },
       }) {
    TRACE_GCHECK_F(([&] -> void {
      const auto texels = NoiseTexels(
        static_cast<std::size_t>(source.texture_width) * source.texture_height);
      const auto depth = MakeSource(source, texels);
      const auto closest = MakePyramid(source);
      const auto furthest = MakePyramid(source);

      ASSERT_TRUE(Build(builder, Rect(*depth, source),
        HzbPyramidBuilder::Targets {
          .closest = oxygen::observer_ptr { closest.get() },
          .furthest = oxygen::observer_ptr { furthest.get() },
        }));

      EXPECT_EQ(ReadMips(*closest), ReferencePyramid(texels, source, true));
      EXPECT_EQ(ReadMips(*furthest), ReferencePyramid(texels, source, false));
    }()),
      fmt::format("{}x{} at ({}, {})", source.width, source.height,
        source.origin_x, source.origin_y));
  }
}

//! The occlusion pyramid is the furthest reduction of its source, persists
//! while its extent holds, and never becomes the published Screen HZB.
NOLINT_TEST_F(HzbPyramidGpuTest, OcclusionPyramidIsFurthestAndUnpublished)
{
  auto module = ScreenHzbModule(*renderer_, SceneTexturesConfig {});
  ctx_.current_view.view_id = ViewId { 7U };
  const auto& source = kOffsetViewSource;
  const auto texels = NoiseTexels(
    static_cast<std::size_t>(source.texture_width) * source.texture_height);
  const auto depth = MakeSource(source, texels);
  const auto build = [&] -> std::optional<ScreenHzbModule::OcclusionPyramid> {
    NextFrame();
    return SubmitCommands("HZB test occlusion pyramid",
      [&](CommandRecorder& recorder)
        -> std::optional<ScreenHzbModule::OcclusionPyramid> {
        return module.BuildOcclusionPyramid(
          ctx_, recorder, Rect(*depth, source));
      });
  };

  const auto first = build();
  const auto second = build();

  if (!first.has_value() || !second.has_value()) {
    FAIL() << "Expected both occlusion pyramid builds to succeed";
  }
  const auto& pyramid = second.value();
  EXPECT_EQ(pyramid.texture, first.value().texture);
  EXPECT_TRUE(pyramid.srv.IsValid());
  EXPECT_EQ(pyramid.width, 1024U);
  EXPECT_EQ(pyramid.height, 512U);
  EXPECT_EQ(pyramid.mip_count, 10U);
  EXPECT_EQ(
    ReadMips(*pyramid.texture), ReferencePyramid(texels, source, false));
  EXPECT_FALSE(module.GetCurrentOutput().available);
  EXPECT_FALSE(module.GetPreviousOutput().available);
}

} // namespace
