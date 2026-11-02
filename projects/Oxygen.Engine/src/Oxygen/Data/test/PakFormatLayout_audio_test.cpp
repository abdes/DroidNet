//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "PakFormatWireBytes.h"

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace audio = oxygen::data::pak::audio;

using oxygen::data::pak::testing::Encoded;
using oxygen::data::pak::testing::WireBytes;

//! An audio row is its payload location, sample rate, channels, format, bit
//! depth, alignment and zeroed reserve.
NOLINT_TEST(PakFormatLayoutAudioTest, AudioResourceDescWireBytes)
{
  const auto row = audio::AudioResourceDesc {
    .data_offset = 0x0102030405060708U,
    .size_bytes = 0x11121314U,
    .sample_rate = 0x21222324U,
    .channels = 0x31323334U,
    .audio_format = 0x41424344U,
    .bits_per_sample = 0x5152U,
    .alignment = 0x6162U,
  };

  const auto expected = WireBytes {}
                          .U64(0x0102030405060708U)
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U16(0x5152U)
                          .U16(0x6162U)
                          .Zeros(4);

  EXPECT_EQ(Encoded(row), expected.Bytes());
}

} // namespace
