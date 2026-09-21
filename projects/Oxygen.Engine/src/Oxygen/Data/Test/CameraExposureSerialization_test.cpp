//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace {

template <typename Record> auto VerifyPhysicalCameraRoundTrip() -> void
{
  constexpr float kApertureF = 2.8F;
  constexpr float kShutterRate = 250.0F;
  constexpr float kIso = 400.0F;
  auto source = Record {};
  source.aperture_f = kApertureF;
  source.shutter_rate = kShutterRate;
  source.iso = kIso;
  oxygen::serio::MemoryStream stream;
  oxygen::serio::Writer writer(stream);
  ASSERT_TRUE(oxygen::serio::Store(writer, source));
  ASSERT_EQ(stream.Data().size(), sizeof(Record));
  ASSERT_TRUE(stream.Seek(0));
  oxygen::serio::Reader reader(stream);
  auto decoded = Record {};
  ASSERT_TRUE(oxygen::serio::Load(reader, decoded));
  EXPECT_FLOAT_EQ(decoded.aperture_f, 2.8F);
  EXPECT_FLOAT_EQ(decoded.shutter_rate, 250.0F);
  EXPECT_FLOAT_EQ(decoded.iso, 400.0F);
}

NOLINT_TEST(CameraExposureSerializationTest, BothProjectionRecordsRoundTrip)
{
  VerifyPhysicalCameraRoundTrip<
    oxygen::data::pak::world::PerspectiveCameraRecord>();
  VerifyPhysicalCameraRoundTrip<
    oxygen::data::pak::world::OrthographicCameraRecord>();
}

template <typename Record> auto VerifyInvalidPhysicalCameraWrites() -> void
{
  for (const auto value : { 0.0F, -1.0F, std::numeric_limits<float>::infinity(),
         std::numeric_limits<float>::quiet_NaN() }) {
    for (const auto member :
      { &Record::aperture_f, &Record::shutter_rate, &Record::iso }) {
      auto source = Record {};
      source.*member = value;
      oxygen::serio::MemoryStream stream;
      oxygen::serio::Writer writer(stream);
      EXPECT_FALSE(oxygen::serio::Store(writer, source));
      EXPECT_TRUE(stream.Data().empty());
    }
  }
}

NOLINT_TEST(CameraExposureSerializationTest,
  InvalidPhysicalValuesAreRejectedBeforeWriting)
{
  VerifyInvalidPhysicalCameraWrites<
    oxygen::data::pak::world::PerspectiveCameraRecord>();
  VerifyInvalidPhysicalCameraWrites<
    oxygen::data::pak::world::OrthographicCameraRecord>();
}

} // namespace
