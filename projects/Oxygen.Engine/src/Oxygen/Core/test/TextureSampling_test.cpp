//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <set>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen {
namespace {

  NOLINT_TEST(TextureSamplingTest, EveryStateHasItsOwnPositionInTheTable)
  {
    auto offsets = std::set<std::uint32_t> {};
    for (const auto address_u : {
           TextureAddress::kRepeat,
           TextureAddress::kMirror,
           TextureAddress::kClamp,
         }) {
      for (const auto address_v : {
             TextureAddress::kRepeat,
             TextureAddress::kMirror,
             TextureAddress::kClamp,
           }) {
        for (const auto mag_filter :
          { TextureFilter::kNearest, TextureFilter::kLinear }) {
          for (const auto min_filter :
            { TextureFilter::kNearest, TextureFilter::kLinear }) {
            for (const auto mip_filter : {
                   TextureMipFilter::kNone,
                   TextureMipFilter::kNearest,
                   TextureMipFilter::kLinear,
                 }) {
              const auto state = TextureSamplerState {
                .address_u = address_u,
                .address_v = address_v,
                .mag_filter = mag_filter,
                .min_filter = min_filter,
                .mip_filter = mip_filter,
              };
              const auto offset = TextureSamplerOffset(state);
              EXPECT_LT(offset, kTextureSamplerCount);
              EXPECT_TRUE(offsets.insert(offset).second);
              EXPECT_EQ(TextureSamplerStateAt(offset), state);
            }
          }
        }
      }
    }
    EXPECT_EQ(offsets.size(), kTextureSamplerCount);
  }

  NOLINT_TEST(TextureSamplingTest, IndexIsThePositionPastTheTableStart)
  {
    constexpr auto kState = TextureSamplerState {
      .address_u = TextureAddress::kClamp,
      .mag_filter = TextureFilter::kNearest,
    };
    EXPECT_EQ(TextureSamplerIndex(kState).get(),
      bindless::generated::kTextureSamplerTableIndex
        + TextureSamplerOffset(kState));
  }

} // namespace
} // namespace oxygen
