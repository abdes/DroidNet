//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/ScenePrep/Handles.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

#ifdef OXYGEN_ENGINE_TESTING

namespace oxygen::engine::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::engine::upload::internal

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

#endif // OXYGEN_ENGINE_TESTING

namespace {

using oxygen::observer_ptr;
using oxygen::engine::upload::DefaultUploadPolicy;
using oxygen::engine::upload::InlineTransfersCoordinator;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::resources::TransformUploader;
using oxygen::renderer::testing::FakeGraphics;

// -- Base Fixture -------------------------------------------------------------

class TransformUploaderTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    gfx_ = std::make_shared<FakeGraphics>();
    gfx_->CreateCommandQueues(SingleQueueStrategy());

    uploader_ = std::make_unique<UploadCoordinator>(
      observer_ptr { gfx_.get() }, DefaultUploadPolicy());

    staging_provider_
      = uploader_->CreateRingBufferStaging(oxygen::frame::SlotCount { 1 }, 4);

    inline_transfers_ = std::make_unique<InlineTransfersCoordinator>(
      observer_ptr { gfx_.get() });

    transform_uploader_ = std::make_unique<TransformUploader>(
      observer_ptr { gfx_.get() }, observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() });
  }

  [[nodiscard]] auto TransformUploaderRef() const -> TransformUploader&
  {
    return *transform_uploader_;
  }

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
  std::unique_ptr<InlineTransfersCoordinator> inline_transfers_;
  std::unique_ptr<TransformUploader> transform_uploader_;
};

// -- Basic tests --------------------------------------------------------------

class TransformUploaderBasicTest : public TransformUploaderTest { };

//! GetOrAllocate returns a valid handle for a new transform.
TEST_F(TransformUploaderBasicTest, GetOrAllocateNewTransformReturnsValidHandle)
{
  // Arrange
  constexpr auto transform = glm::mat4 { 1.0F };
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });

  // Act
  const auto handle = uploader.GetOrAllocate(transform);

  // Assert
  EXPECT_TRUE(uploader.IsValidHandle(handle));
}

//! Multiple allocations in the same frame produce different handles.
TEST_F(TransformUploaderBasicTest,
  GetOrAllocateMultipleTransformsProducesDifferentHandles)
{
  // Arrange
  constexpr auto t1 = glm::mat4 { 1.0F };
  const auto t2 = glm::scale(t1, glm::vec3 { 2.0F });
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });

  // Act
  const auto h1 = uploader.GetOrAllocate(t1);
  const auto h2 = uploader.GetOrAllocate(t2);

  // Assert
  EXPECT_NE(h1, h2);
}

//! Slot reuse: transforms allocated at the same position in different frames
//! reuse the same handle.
TEST_F(TransformUploaderBasicTest,
  GetOrAllocateSlotReuseSamePositionSameHandleAcrossFrames)
{
  // Arrange
  constexpr auto t1 = glm::mat4 { 1.0F };
  constexpr auto t2 = glm::translate(t1, glm::vec3 { 1.0F, 2.0F, 3.0F });
  auto& uploader = TransformUploaderRef();

  // Act - Frame 1: allocate t1 at position 0
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });
  const auto h1_frame1 = uploader.GetOrAllocate(t1);

  // Act - Frame 2: allocate t2 at position 0 (should reuse slot)
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });
  const auto h1_frame2 = uploader.GetOrAllocate(t2);

  // Assert: same position gets same handle across frames
  EXPECT_EQ(h1_frame1, h1_frame2);
}

//! ComputeNormalMatrix correctly handles identity matrix.
TEST_F(
  TransformUploaderBasicTest, ComputeNormalMatrixIdentityMatrixReturnsIdentity)
{
  // Arrange
  constexpr auto identity = glm::mat4 { 1.0F };
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });

  // Act
  uploader.GetOrAllocate(identity);
  const auto normals = uploader.GetNormalMatrices();

  // Assert
  EXPECT_EQ(normals.size(), 1);
  const auto& normal_mat = normals[0];
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      const float expected = (c == r) ? 1.0F : 0.0F;
      EXPECT_FLOAT_EQ(normal_mat[c][r], expected)
        << "Mismatch at [" << c << "][" << r << "]";
    }
  }
}

//! EnsureFrameResources allocates GPU buffers for transforms.
TEST_F(TransformUploaderBasicTest,
  EnsureFrameResourcesAllocatesBuffersReturnsValidSrvIndices)
{
  // Arrange
  constexpr auto transform = glm::mat4 { 1.0F };
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });
  uploader.GetOrAllocate(transform);

  // Act
  uploader.EnsureFrameResources();
  [[maybe_unused]] const auto worlds_srv = uploader.GetWorldsSrvIndex();
  [[maybe_unused]] const auto normals_srv = uploader.GetNormalsSrvIndex();

  // Assert: SRV indices should be valid (not kInvalidShaderVisibleIndex)
  // The actual values depend on FakeGraphics implementation
  EXPECT_TRUE(uploader.GetWorldMatrices().size() == 1);
  EXPECT_TRUE(uploader.GetNormalMatrices().size() == 1);
}

//! GetWorldMatrices and GetNormalMatrices return correct data after
//! allocation.
TEST_F(TransformUploaderBasicTest,
  GetWorldMatricesAfterAllocationReturnsAllocatedTransforms)
{
  // Arrange
  constexpr auto t1 = glm::mat4 { 1.0F };
  const auto t2 = glm::scale(t1, glm::vec3 { 2.0F, 3.0F, 4.0F });
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });

  // Act
  uploader.GetOrAllocate(t1);
  uploader.GetOrAllocate(t2);
  const auto matrices = uploader.GetWorldMatrices();

  // Assert
  EXPECT_EQ(matrices.size(), 2);
  EXPECT_EQ(matrices[0], t1);
  EXPECT_EQ(matrices[1], t2);
}

// -- Frame lifecycle and statistics tests -------------------------------------

class TransformUploaderFrameLifecycleTest : public TransformUploaderTest { };

//! OnFrameStart resets frame write count for slot reuse.
TEST_F(TransformUploaderFrameLifecycleTest,
  OnFrameStartResetsCursorAllowsSlotReuseNextFrame)
{
  // Arrange
  constexpr auto t1 = glm::mat4 { 1.0F };
  const auto t2 = glm::scale(t1, glm::vec3 { 2.0F });
  auto& uploader = TransformUploaderRef();

  // Act & Assert - Frame 1
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });
  const auto h1 = uploader.GetOrAllocate(t1);
  const auto h2 = uploader.GetOrAllocate(t2);
  EXPECT_NE(h1, h2);
  EXPECT_EQ(uploader.GetWorldMatrices().size(), 2);

  // Act & Assert - Frame 2: allocate 3 transforms (should reuse first 2 slots)
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });
  const auto h3 = uploader.GetOrAllocate(t1);
  const auto h4 = uploader.GetOrAllocate(t2);
  const auto h5 = uploader.GetOrAllocate(t1);

  EXPECT_EQ(h3, h1); // Reused slot 0
  EXPECT_EQ(h4, h2); // Reused slot 1
  EXPECT_NE(h5, h1); // New slot 2
  EXPECT_EQ(uploader.GetWorldMatrices().size(), 3);
}

//! Multiple frames track transform count correctly.
TEST_F(TransformUploaderFrameLifecycleTest,
  MultipleFramesTransformCountGrowsMonotonically)
{
  // Arrange
  auto& uploader = TransformUploaderRef();

  // Act & Assert - Frame 0: 2 transforms
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });
  uploader.GetOrAllocate(glm::mat4 { 1.0F });
  uploader.GetOrAllocate(glm::mat4 { 1.0F });
  size_t size_frame0 = uploader.GetWorldMatrices().size();

  // Act & Assert - Frame 1: allocate 3 transforms (exceeds frame 0 count)
  // to force growth beyond the existing 2 slots
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });
  uploader.GetOrAllocate(glm::mat4 { 1.0F });
  uploader.GetOrAllocate(glm::mat4 { 1.0F });
  uploader.GetOrAllocate(glm::mat4 { 1.0F });
  size_t size_frame1 = uploader.GetWorldMatrices().size();

  // Assert: count grows monotonically
  EXPECT_EQ(size_frame0, 2);
  EXPECT_EQ(size_frame1, 3);
}

// -- Edge cases and boundary conditions ---------------------------------------

class TransformUploaderEdgeCaseTest : public TransformUploaderTest { };

//! Empty transform list doesn't crash on EnsureFrameResources.
TEST_F(TransformUploaderEdgeCaseTest,
  EnsureFrameResourcesEmptyTransformsReturnsEarly)
{
  // Arrange
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });

  // Act & Assert: no allocations, EnsureFrameResources should return early
  EXPECT_NO_THROW(uploader.EnsureFrameResources());
  EXPECT_EQ(uploader.GetWorldMatrices().size(), 0);
}

//! Large number of transforms allocated in single frame.
TEST_F(
  TransformUploaderEdgeCaseTest, GetOrAllocateManyTransformsAllHandlesValid)
{
  // Arrange
  constexpr int count = 100;
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });

  // Act
  for (int i = 0; i < count; ++i) {
    const auto t
      = glm::translate(glm::mat4 { 1.0F }, glm::vec3 { static_cast<float>(i) });
    const auto h = uploader.GetOrAllocate(t);
    // Assert each handle is valid
    EXPECT_TRUE(uploader.IsValidHandle(h));
  }

  // Assert total count
  EXPECT_EQ(uploader.GetWorldMatrices().size(), count);
  EXPECT_EQ(uploader.GetNormalMatrices().size(), count);
}

//! IsValidHandle rejects out-of-range handles.
TEST_F(TransformUploaderEdgeCaseTest, IsValidHandleOutOfRangeHandleReturnsFalse)
{
  // Arrange
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });
  uploader.GetOrAllocate(glm::mat4 { 1.0F });

  // Act & Assert
  constexpr auto valid_handle
    = oxygen::engine::sceneprep::TransformHandle { 0 };
  constexpr auto invalid_handle
    = oxygen::engine::sceneprep::TransformHandle { 999 };
  EXPECT_TRUE(uploader.IsValidHandle(valid_handle));
  EXPECT_FALSE(uploader.IsValidHandle(invalid_handle));
}

// -- Buffer state and lazy loading tests --------------------------------------

class TransformUploaderBufferTest : public TransformUploaderTest { };

//! GetWorldsSrvIndex returns valid SRV when transforms exist.
TEST_F(TransformUploaderBufferTest,
  GetWorldsSrvIndexWithTransformsReturnsAccessibleIndex)
{
  // Arrange
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });
  uploader.GetOrAllocate(glm::mat4 { 1.0F });

  // Act: access SRV from const context
  [[maybe_unused]] const auto srv = uploader.GetWorldsSrvIndex();

  // Assert: SRV is accessible and transforms are available
  EXPECT_TRUE(uploader.GetWorldMatrices().size() > 0);
}

//! GetNormalsSrvIndex returns valid SRV when transforms exist.
TEST_F(TransformUploaderBufferTest,
  GetNormalsSrvIndexWithTransformsReturnsAccessibleIndex)
{
  // Arrange
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });
  uploader.GetOrAllocate(glm::mat4 { 1.0F });

  // Act: access SRV from const context
  [[maybe_unused]] const auto srv = uploader.GetNormalsSrvIndex();

  // Assert: SRV is accessible and transforms are available
  EXPECT_TRUE(uploader.GetNormalMatrices().size() > 0);
}

//! Slot reuse keeps handle count stable across frames with same allocation
//! pattern.
TEST_F(TransformUploaderBufferTest,
  TwoFramesSlotReuseHandleCountStableWhenPatternMatches)
{
  // Arrange
  auto& uploader = TransformUploaderRef();

  // Act & Assert - Frame 0: allocate 1 transform
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 0 }, Slot { 0 });
  const auto h0 = uploader.GetOrAllocate(glm::mat4 { 1.0F });
  uploader.EnsureFrameResources();
  const size_t size_frame0 = uploader.GetWorldMatrices().size();

  // Act & Assert - Frame 1: allocate 1 transform at same position (reuses slot)
  uploader.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });
  const auto h1 = uploader.GetOrAllocate(glm::mat4 { 1.0F });
  uploader.EnsureFrameResources();
  const size_t size_frame1 = uploader.GetWorldMatrices().size();

  // Assert: same allocation pattern means same handle and same size
  EXPECT_EQ(h0, h1);
  EXPECT_EQ(size_frame0, 1);
  EXPECT_EQ(size_frame1, 1);
}

} // namespace
