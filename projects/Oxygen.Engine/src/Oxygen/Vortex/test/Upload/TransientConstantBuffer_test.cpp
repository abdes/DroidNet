//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/RingBufferStaging.h>
#include <Oxygen/Vortex/Upload/TransientConstantBuffer.h>
#include <Oxygen/Vortex/test/Fixtures/RingBufferStagingFixture.h>

namespace oxygen::vortex::upload {

namespace {

  //! A record smaller than the placement alignment, so packing would put two
  //! records inside one 256-byte block if alignment were ignored.
  struct SmallRecord {
    std::array<std::uint32_t, 4> words {};
  };

  class TransientConstantBufferTest : public testing::RingBufferStagingFixture {
  protected:
    void SetUp() override
    {
      testing::RingBufferStagingFixture::SetUp();
      // The provider's own alignment is the structured-buffer one, as in the
      // renderer; constant records must still land on 256-byte boundaries.
      ASSERT_NE(MakeRingBuffer(
                  frame::SlotCount { 1U }, packing::kStructuredBufferAlignment),
        nullptr);
    }
  };

  NOLINT_TEST_F(TransientConstantBufferTest, EachWriteIsANewAlignedRecord)
  {
    auto constants = TransientConstantBuffer(Staging(), nullptr, "Test");

    const auto first = constants.Write(SmallRecord { .words = { 1U } });
    const auto second = constants.Write(SmallRecord { .words = { 2U } });

    ASSERT_HAS_VALUE(first);
    ASSERT_HAS_VALUE(second);
    EXPECT_TRUE(first->IsValid());
    EXPECT_NE(first->GpuAddress(), second->GpuAddress());
    EXPECT_EQ(first->offset_bytes % packing::kConstantBufferAlignment, 0U);
    EXPECT_EQ(second->offset_bytes % packing::kConstantBufferAlignment, 0U);
  }

  NOLINT_TEST_F(
    TransientConstantBufferTest, EarlierRecordOutlivesProviderGrowth)
  {
    auto constants = TransientConstantBuffer(Staging(), nullptr, "Test");
    const auto first = constants.Write(SmallRecord {});
    ASSERT_HAS_VALUE(first);

    // Outgrow the initial partition so the provider recreates its buffer.
    const auto large = std::vector<std::byte>(
      RingBufferStaging::kDefaultInitialPartitionBytes.get() + 1U);
    const auto grown = constants.WriteBytes(std::span { large });
    ASSERT_HAS_VALUE(grown);

    EXPECT_NE(first->buffer, grown->buffer);
    EXPECT_TRUE(first->IsValid());
  }

  NOLINT_TEST_F(TransientConstantBufferTest, EmptyWriteIsRejected)
  {
    auto constants = TransientConstantBuffer(Staging(), nullptr, "Test");

    const auto record = constants.WriteBytes({});

    ASSERT_FALSE(record.has_value());
    EXPECT_EQ(record.error(), make_error_code(UploadError::kInvalidRequest));
  }

} // namespace

} // namespace oxygen::vortex::upload
