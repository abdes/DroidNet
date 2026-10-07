//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <limits>

#include <Oxygen/Core/Types/CameraAspectMode.h>
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
  for (const auto value : {
         0.0F,
         -1.0F,
         std::numeric_limits<float>::infinity(),
         std::numeric_limits<float>::quiet_NaN(),
       }) {
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

NOLINT_TEST(
  CameraExposureSerializationTest, AspectPolicyRetainsRatioInBothModes)
{
  using oxygen::CameraAspectMode;
  using oxygen::data::pak::world::PerspectiveCameraRecord;
  constexpr auto kRetainedRatio = 4.0F / 3.0F;
  for (const auto mode :
    { CameraAspectMode::kAuto, CameraAspectMode::kFixed }) {
    PerspectiveCameraRecord source;
    source.aspect_mode = mode;
    source.aspect_ratio = kRetainedRatio;
    oxygen::serio::MemoryStream stream;
    oxygen::serio::Writer writer(stream);
    ASSERT_TRUE(oxygen::serio::Store(writer, source));
    ASSERT_TRUE(stream.Seek(0));
    oxygen::serio::Reader reader(stream);
    PerspectiveCameraRecord decoded;
    ASSERT_TRUE(oxygen::serio::Load(reader, decoded));
    EXPECT_EQ(decoded.aspect_mode, mode);
    EXPECT_FLOAT_EQ(decoded.aspect_ratio, kRetainedRatio);
  }
}

NOLINT_TEST(CameraExposureSerializationTest, InvalidProjectionCannotBeWritten)
{
  using oxygen::data::pak::world::PerspectiveCameraRecord;
  std::array<PerspectiveCameraRecord, 4> records {};
  records.at(0).aspect_mode = static_cast<oxygen::CameraAspectMode>(UINT8_MAX);
  records.at(1).aspect_ratio = 0.0F;
  records.at(2).fov_y = std::numeric_limits<float>::infinity();
  records.at(3).far_plane = records.at(3).near_plane;
  for (const auto& record : records) {
    oxygen::serio::MemoryStream stream;
    oxygen::serio::Writer writer(stream);
    EXPECT_FALSE(oxygen::serio::Store(writer, record));
    EXPECT_TRUE(stream.Data().empty());
  }
}

NOLINT_TEST(CameraExposureSerializationTest, OrthographicAspectModeRoundTrips)
{
  using oxygen::CameraAspectMode;
  using oxygen::data::pak::world::OrthographicCameraRecord;
  for (const auto mode :
    { CameraAspectMode::kAuto, CameraAspectMode::kFixed }) {
    OrthographicCameraRecord source;
    source.aspect_mode = mode;
    oxygen::serio::MemoryStream stream;
    oxygen::serio::Writer writer(stream);
    ASSERT_TRUE(oxygen::serio::Store(writer, source));
    ASSERT_TRUE(stream.Seek(0));
    oxygen::serio::Reader reader(stream);
    OrthographicCameraRecord decoded;
    ASSERT_TRUE(oxygen::serio::Load(reader, decoded));
    EXPECT_EQ(decoded.aspect_mode, mode);
  }
}

NOLINT_TEST(
  CameraExposureSerializationTest, InvalidOrthographicVolumeIsRejected)
{
  using oxygen::data::pak::world::OrthographicCameraRecord;
  std::array<OrthographicCameraRecord, 4> records {};
  records.at(0).aspect_mode = static_cast<oxygen::CameraAspectMode>(UINT8_MAX);
  records.at(1).right = records.at(1).left;
  records.at(2).top = std::numeric_limits<float>::quiet_NaN();
  records.at(3).far_plane = records.at(3).near_plane;
  for (const auto& record : records) {
    oxygen::serio::MemoryStream stream;
    oxygen::serio::Writer writer(stream);
    EXPECT_FALSE(oxygen::serio::Store(writer, record));
    EXPECT_TRUE(stream.Data().empty());
  }
}

} // namespace
