//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/PhysicsResourceLoader.h>
#include <Oxygen/Data/PhysicsResource.h>

#include "Fixtures/PhysicsLoaderTestFixtures.h"

using testing::NotNull;

using oxygen::content::loaders::LoadPhysicsResource;

namespace {

class PhysicsResourceLoaderHappyPathTest
  : public oxygen::content::testing::PhysicsResourceFixtureBase { };

class PhysicsResourceLoaderValidationTest
  : public oxygen::content::testing::PhysicsResourceFixtureBase { };

NOLINT_TEST_F(
  PhysicsResourceLoaderHappyPathTest, LoadPhysicsResourceValidInputSucceeds)
{
  oxygen::data::pak::physics::PhysicsResourceDesc desc {};
  desc.data_offset = 256;
  desc.size_bytes = 96;
  desc.format
    = oxygen::data::pak::physics::PhysicsResourceFormat::kJoltShapeBinary;
  {
    constexpr auto kHashPrefix = 0x1122334455667788ULL;
    std::memcpy(desc.content_hash, &kHashPrefix, sizeof(kHashPrefix));
  }
  WriteDescriptorAndData(desc, 0x7A);

  const auto resource = LoadPhysicsResource(MakeContext());

  ASSERT_THAT(resource, NotNull());
  EXPECT_EQ(resource->GetDataOffset(), 256U);
  EXPECT_EQ(resource->GetDataSize(), 96U);
  EXPECT_EQ(resource->GetFormat(),
    oxygen::data::pak::physics::PhysicsResourceFormat::kJoltShapeBinary);
  EXPECT_EQ(resource->GetContentHash(), 0x1122334455667788ULL);
  EXPECT_THAT(resource->GetData(), ::testing::Each(static_cast<uint8_t>(0x7A)));
}

NOLINT_TEST_F(
  PhysicsResourceLoaderValidationTest, LoadPhysicsResourceTruncatedDescThrows)
{
  std::vector<std::byte> short_bytes(
    sizeof(oxygen::data::pak::physics::PhysicsResourceDesc) / 2,
    std::byte { 0 });
  auto packed_desc = desc_writer_.ScopedAlignment(1);
  const auto write_result = desc_writer_.WriteBlob(
    std::span<const std::byte>(short_bytes.data(), short_bytes.size()));
  ASSERT_TRUE(write_result);

  EXPECT_THROW(
    { (void)LoadPhysicsResource(MakeContext()); }, std::runtime_error);
}

NOLINT_TEST_F(
  PhysicsResourceLoaderValidationTest, LoadPhysicsResourceDataReadFailureThrows)
{
  oxygen::data::pak::physics::PhysicsResourceDesc desc {};
  desc.data_offset = 64;
  desc.size_bytes = 8;
  desc.format
    = oxygen::data::pak::physics::PhysicsResourceFormat::kJoltShapeBinary;

  auto packed_desc = desc_writer_.ScopedAlignment(1);
  const auto desc_bytes = std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&desc), sizeof(desc));
  const auto desc_result = desc_writer_.WriteBlob(desc_bytes);
  ASSERT_TRUE(desc_result);

  EXPECT_THROW(
    { (void)LoadPhysicsResource(MakeContext()); }, std::runtime_error);
}

} // namespace
