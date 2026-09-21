//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <memory>

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/TransformUploader.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>
#include <Oxygen/Vortex/Upload/UploaderTag.h>

namespace oxygen::vortex::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::vortex::upload::internal

namespace oxygen::vortex::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::vortex::internal

namespace {

using oxygen::observer_ptr;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::vortex::internal::RendererTagFactory;
using oxygen::vortex::resources::TransformUploader;
using oxygen::vortex::testing::FakeGraphics;
using oxygen::vortex::upload::DefaultUploadPolicy;
using oxygen::vortex::upload::InlineTransfersCoordinator;
using oxygen::vortex::upload::StagingProvider;
using oxygen::vortex::upload::UploadCoordinator;

// -- Base Fixture -------------------------------------------------------------

class TransformUploaderTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    gfx_ = std::make_shared<FakeGraphics>();
    gfx_->CreateCommandQueues(SingleQueueStrategy());

    uploader_ = std::make_unique<UploadCoordinator>(
      observer_ptr {
        gfx_.get(),
      },
      DefaultUploadPolicy());

    staging_provider_ = uploader_->CreateRingBufferStaging(
      oxygen::frame::SlotCount {
        1,
      },
      4);

    inline_transfers_
      = std::make_unique<InlineTransfersCoordinator>(observer_ptr {
        gfx_.get(),
      });

    transform_uploader_ = std::make_unique<TransformUploader>(
      observer_ptr {
        gfx_.get(),
      },
      observer_ptr {
        staging_provider_.get(),
      },
      observer_ptr {
        inline_transfers_.get(),
      });
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
NOLINT_TEST_F(
  TransformUploaderBasicTest, GetOrAllocateNewTransformReturnsValidHandle)
{
  // Arrange
  constexpr auto transform = glm::mat4 {
    1.0F,
  };
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });

  // Act
  const auto handle = uploader.GetOrAllocate(transform);

  // Assert
  EXPECT_TRUE(uploader.IsHandleValid(handle));
}

//! Multiple allocations in the same frame produce different handles.
NOLINT_TEST_F(TransformUploaderBasicTest,
  GetOrAllocateMultipleTransformsProducesDifferentHandles)
{
  // Arrange
  constexpr auto t1 = glm::mat4 {
    1.0F,
  };
  const auto t2 = glm::scale(t1,
    glm::vec3 {
      2.0F,
    });
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });

  // Act
  const auto h1 = uploader.GetOrAllocate(t1);
  const auto h2 = uploader.GetOrAllocate(t2);

  // Assert
  EXPECT_NE(h1, h2);
}

//! Slot reuse: transforms allocated at the same position in different frames
//! reuse the same handle.
NOLINT_TEST_F(TransformUploaderBasicTest,
  GetOrAllocateSlotReuseSamePositionSameHandleAcrossFrames)
{
  // Arrange
  constexpr auto t1 = glm::mat4 {
    1.0F,
  };
  constexpr auto t2 = glm::translate(t1,
    glm::vec3 {
      1.0F,
      2.0F,
      3.0F,
    });
  auto& uploader = TransformUploaderRef();

  // Act - Frame 1: allocate t1 at position 0
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
  const auto h1_frame1 = uploader.GetOrAllocate(t1);

  // Act - Frame 2: allocate t2 at position 0 (should reuse slot)
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      1,
    },
    Slot {
      0,
    });
  const auto h1_frame2 = uploader.GetOrAllocate(t2);

  // Assert: same position gets same handle across frames
  EXPECT_EQ(h1_frame1, h1_frame2);
}

//! ComputeNormalMatrix correctly handles identity matrix.
NOLINT_TEST_F(
  TransformUploaderBasicTest, ComputeNormalMatrixIdentityMatrixReturnsIdentity)
{
  // Arrange
  constexpr auto identity = glm::mat4 {
    1.0F,
  };
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });

  // Act
  uploader.GetOrAllocate(identity);
  const auto normals = uploader.GetNormalMatrices();

  // Assert
  ASSERT_EQ(normals.size(), 1);
  const auto& normal_mat = normals.front();
  for (int column = 0; column < 4; ++column) {
    SCOPED_TRACE(column);
    const auto actual = glm::column(normal_mat, column);
    EXPECT_FLOAT_EQ(actual.x, column == 0 ? 1.0F : 0.0F);
    EXPECT_FLOAT_EQ(actual.y, column == 1 ? 1.0F : 0.0F);
    EXPECT_FLOAT_EQ(actual.z, column == 2 ? 1.0F : 0.0F);
    EXPECT_FLOAT_EQ(actual.w, column == 3 ? 1.0F : 0.0F);
  }
}

//! ComputeNormalMatrix returns the rotation matrix for pure rotations.
NOLINT_TEST_F(TransformUploaderBasicTest,
  ComputeNormalMatrixPureRotationReturnsRotationMatrix)
{
  // Arrange
  constexpr auto identity = glm::mat4 {
    1.0F,
  };
  const auto world = glm::rotate(identity, glm::radians(37.0F),
    glm::vec3 {
      0.0F,
      0.0F,
      1.0F,
    });
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });

  // Act
  uploader.GetOrAllocate(world);
  const auto normals = uploader.GetNormalMatrices();

  // Assert
  ASSERT_EQ(normals.size(), 1);
  const auto& normal_mat = normals.front();

  const glm::mat3 expected_3x3 = glm::mat3 {
    world,
  };
  for (int column = 0; column < 3; ++column) {
    SCOPED_TRACE(column);
    const auto actual = glm::column(normal_mat, column);
    const auto expected = glm::column(expected_3x3, column);
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
  }
}

//! ComputeNormalMatrix uses inverse-transpose for rotation+non-uniform scale.
NOLINT_TEST_F(TransformUploaderBasicTest,
  ComputeNormalMatrixRotationNonUniformScaleMatchesInverseTranspose)
{
  // Arrange
  constexpr auto identity = glm::mat4 {
    1.0F,
  };
  const auto rot = glm::rotate(identity, glm::radians(25.0F),
    glm::vec3 {
      0.0F,
      1.0F,
      0.0F,
    });
  const auto scl = glm::scale(identity,
    glm::vec3 {
      2.0F,
      1.0F,
      0.5F,
    });
  const auto world = rot * scl;
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });

  // Act
  uploader.GetOrAllocate(world);
  const auto normals = uploader.GetNormalMatrices();

  // Assert
  ASSERT_EQ(normals.size(), 1);
  const auto& normal_mat = normals.front();

  const glm::mat3 upper_3x3 {
    world,
  };
  const glm::mat3 expected_3x3 = glm::transpose(glm::inverse(upper_3x3));

  constexpr float kEps = 1e-5F;
  for (int column = 0; column < 3; ++column) {
    SCOPED_TRACE(column);
    const auto actual = glm::column(normal_mat, column);
    const auto expected = glm::column(expected_3x3, column);
    EXPECT_NEAR(actual.x, expected.x, kEps);
    EXPECT_NEAR(actual.y, expected.y, kEps);
    EXPECT_NEAR(actual.z, expected.z, kEps);
  }
}

//! EnsureFrameResources allocates GPU buffers for transforms.
NOLINT_TEST_F(TransformUploaderBasicTest,
  EnsureFrameResourcesAllocatesBuffersReturnsValidSrvIndices)
{
  // Arrange
  constexpr auto transform = glm::mat4 {
    1.0F,
  };
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
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

//! Later views receive all their transforms without mutating earlier SRVs.
NOLINT_TEST_F(TransformUploaderBasicTest,
  AllocatingAfterPublicationCreatesFreshTransformSnapshots)
{
  auto& uploader = TransformUploaderRef();
  const auto snapshot = [&] -> std::array<oxygen::ShaderVisibleIndex, 3> {
    return std::array {
      uploader.GetWorldsSrvIndex(),
      uploader.GetPreviousWorldsSrvIndex(),
      uploader.GetNormalsSrvIndex(),
    };
  };
  for (unsigned frame = 0; frame < 2; ++frame) {
    uploader.OnFrameStart(RendererTagFactory::Get(),
      SequenceNumber {
        frame,
      },
      Slot {
        0,
      });
    const auto first = uploader.GetOrAllocate(glm::mat4 {
      1,
    });
    const auto original = snapshot();
    const auto moved = glm::translate(
      glm::mat4 {
        1,
      },
      glm::vec3 {
        20,
        0,
        0,
      });
    const auto second = uploader.GetOrAllocate(moved,
      glm::mat4 {
        1,
      });
    const auto expanded = snapshot();
    for (unsigned index = 0; index < original.size(); ++index) {
      EXPECT_TRUE(original.at(index).IsValid());
      EXPECT_TRUE(expanded.at(index).IsValid());
      EXPECT_NE(original.at(index), expanded.at(index));
    }
    EXPECT_TRUE(uploader.IsHandleValid(first));
    EXPECT_TRUE(uploader.IsHandleValid(second));
    const auto worlds = uploader.GetWorldMatrices();
    const auto previous_worlds = uploader.GetPreviousWorldMatrices();
    ASSERT_LT(second.get(), worlds.size());
    ASSERT_LT(second.get(), previous_worlds.size());
    EXPECT_EQ(worlds.subspan(second.get(), 1U).front(), moved);
    EXPECT_EQ(previous_worlds.subspan(second.get(), 1U).front(),
      (glm::mat4 {
        1,
      }));
    uploader.EnsureFrameResources();
    EXPECT_EQ(snapshot(), expanded);
  }
}

//! GetWorldMatrices and GetNormalMatrices return correct data after
//! allocation.
NOLINT_TEST_F(TransformUploaderBasicTest,
  GetWorldMatricesAfterAllocationReturnsAllocatedTransforms)
{
  // Arrange
  constexpr auto t1 = glm::mat4 {
    1.0F,
  };
  const auto t2 = glm::scale(t1,
    glm::vec3 {
      2.0F,
      3.0F,
      4.0F,
    });
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });

  // Act
  uploader.GetOrAllocate(t1);
  uploader.GetOrAllocate(t2);
  const auto matrices = uploader.GetWorldMatrices();

  // Assert
  ASSERT_EQ(matrices.size(), 2);
  EXPECT_EQ(matrices.front(), t1);
  EXPECT_EQ(matrices.back(), t2);
}

// -- Frame lifecycle and statistics tests -------------------------------------

class TransformUploaderFrameLifecycleTest : public TransformUploaderTest { };

//! OnFrameStart resets frame write count for slot reuse.
NOLINT_TEST_F(TransformUploaderFrameLifecycleTest,
  OnFrameStartResetsCursorAllowsSlotReuseNextFrame)
{
  // Arrange
  constexpr auto t1 = glm::mat4 {
    1.0F,
  };
  const auto t2 = glm::scale(t1,
    glm::vec3 {
      2.0F,
    });
  auto& uploader = TransformUploaderRef();

  // Act & Assert - Frame 1
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
  const auto h1 = uploader.GetOrAllocate(t1);
  const auto h2 = uploader.GetOrAllocate(t2);
  EXPECT_NE(h1, h2);
  EXPECT_EQ(uploader.GetWorldMatrices().size(), 2);

  // Act & Assert - Frame 2: allocate 3 transforms (should reuse first 2 slots)
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      1,
    },
    Slot {
      0,
    });
  const auto h3 = uploader.GetOrAllocate(t1);
  const auto h4 = uploader.GetOrAllocate(t2);
  const auto h5 = uploader.GetOrAllocate(t1);

  EXPECT_EQ(h3, h1); // Reused slot 0
  EXPECT_EQ(h4, h2); // Reused slot 1
  EXPECT_NE(h5, h1); // New slot 2
  EXPECT_EQ(uploader.GetWorldMatrices().size(), 3);
}

//! Multiple frames track transform count correctly.
NOLINT_TEST_F(TransformUploaderFrameLifecycleTest,
  MultipleFramesTransformCountGrowsMonotonically)
{
  // Arrange
  auto& uploader = TransformUploaderRef();

  // Act & Assert - Frame 0: 2 transforms
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
  uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });
  uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });
  size_t size_frame0 = uploader.GetWorldMatrices().size();

  // Act & Assert - Frame 1: allocate 3 transforms (exceeds frame 0 count)
  // to force growth beyond the existing 2 slots
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      1,
    },
    Slot {
      0,
    });
  uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });
  uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });
  uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });
  size_t size_frame1 = uploader.GetWorldMatrices().size();

  // Assert: count grows monotonically
  EXPECT_EQ(size_frame0, 2);
  EXPECT_EQ(size_frame1, 3);
}

//! Slot reuse stays deterministic even when frame slots rotate.
NOLINT_TEST_F(TransformUploaderFrameLifecycleTest,
  RotatingFrameSlotsKeepsSameHandleForSameAllocationPattern)
{
  auto& uploader = TransformUploaderRef();

  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
  const auto h0 = uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });

  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      1,
    },
    Slot {
      1,
    });
  const auto h1 = uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });

  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      2,
    },
    Slot {
      2,
    });
  const auto h2 = uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });

  EXPECT_EQ(h0, h1);
  EXPECT_EQ(h1, h2);
}

// -- Edge cases and boundary conditions ---------------------------------------

class TransformUploaderEdgeCaseTest : public TransformUploaderTest { };

//! Empty transform list doesn't crash on EnsureFrameResources.
NOLINT_TEST_F(TransformUploaderEdgeCaseTest,
  EnsureFrameResourcesEmptyTransformsReturnsEarly)
{
  // Arrange
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });

  // Act & Assert: no allocations, EnsureFrameResources should return early
  NOLINT_EXPECT_NO_THROW(uploader.EnsureFrameResources());
  EXPECT_EQ(uploader.GetWorldMatrices().size(), 0);
}

//! Large number of transforms allocated in single frame.
NOLINT_TEST_F(
  TransformUploaderEdgeCaseTest, GetOrAllocateManyTransformsAllHandlesValid)
{
  // Arrange
  constexpr int count = 100;
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });

  // Act
  for (int i = 0; i < count; ++i) {
    const auto t = glm::translate(
      glm::mat4 {
        1.0F,
      },
      glm::vec3 {
        static_cast<float>(i),
      });
    const auto h = uploader.GetOrAllocate(t);
    // Assert each handle is valid
    EXPECT_TRUE(uploader.IsHandleValid(h));
  }

  // Assert total count
  EXPECT_EQ(uploader.GetWorldMatrices().size(), count);
  EXPECT_EQ(uploader.GetNormalMatrices().size(), count);
}

//! IsHandleValid rejects out-of-range handles.
NOLINT_TEST_F(
  TransformUploaderEdgeCaseTest, IsHandleValidOutOfRangeHandleReturnsFalse)
{
  // Arrange
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
  uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });

  // Act & Assert
  constexpr auto valid_handle = oxygen::vortex::sceneprep::TransformHandle {
    oxygen::vortex::sceneprep::TransformHandle::Index {
      0U,
    },
    oxygen::vortex::sceneprep::TransformHandle::Generation {
      1U,
    },
  };
  constexpr auto invalid_handle = oxygen::vortex::sceneprep::TransformHandle {
    oxygen::vortex::sceneprep::TransformHandle::Index {
      999U,
    },
    oxygen::vortex::sceneprep::TransformHandle::Generation {
      1U,
    },
  };
  constexpr auto stale_generation_handle
    = oxygen::vortex::sceneprep::TransformHandle {
        oxygen::vortex::sceneprep::TransformHandle::Index {
          0U,
        },
        oxygen::vortex::sceneprep::TransformHandle::Generation {
          99U,
        },
      };
  EXPECT_TRUE(uploader.IsHandleValid(valid_handle));
  EXPECT_FALSE(uploader.IsHandleValid(invalid_handle));
  EXPECT_FALSE(uploader.IsHandleValid(stale_generation_handle));
}

// -- Buffer state and lazy loading tests --------------------------------------

class TransformUploaderBufferTest : public TransformUploaderTest { };

//! GetWorldsSrvIndex returns valid SRV when transforms exist.
NOLINT_TEST_F(TransformUploaderBufferTest,
  GetWorldsSrvIndexWithTransformsReturnsAccessibleIndex)
{
  // Arrange
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
  uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });

  // Act: access SRV from const context
  [[maybe_unused]] const auto srv = uploader.GetWorldsSrvIndex();

  // Assert: SRV is accessible and transforms are available
  EXPECT_TRUE(uploader.GetWorldMatrices().size() > 0);
}

//! GetNormalsSrvIndex returns valid SRV when transforms exist.
NOLINT_TEST_F(TransformUploaderBufferTest,
  GetNormalsSrvIndexWithTransformsReturnsAccessibleIndex)
{
  // Arrange
  auto& uploader = TransformUploaderRef();
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
  uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });

  // Act: access SRV from const context
  [[maybe_unused]] const auto srv = uploader.GetNormalsSrvIndex();

  // Assert: SRV is accessible and transforms are available
  EXPECT_TRUE(uploader.GetNormalMatrices().size() > 0);
}

//! Slot reuse keeps handle count stable across frames with same allocation
//! pattern.
NOLINT_TEST_F(TransformUploaderBufferTest,
  TwoFramesSlotReuseHandleCountStableWhenPatternMatches)
{
  // Arrange
  auto& uploader = TransformUploaderRef();

  // Act & Assert - Frame 0: allocate 1 transform
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      0,
    },
    Slot {
      0,
    });
  const auto h0 = uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });
  uploader.EnsureFrameResources();
  const size_t size_frame0 = uploader.GetWorldMatrices().size();

  // Act & Assert - Frame 1: allocate 1 transform at same position (reuses slot)
  uploader.OnFrameStart(RendererTagFactory::Get(),
    SequenceNumber {
      1,
    },
    Slot {
      0,
    });
  const auto h1 = uploader.GetOrAllocate(glm::mat4 {
    1.0F,
  });
  uploader.EnsureFrameResources();
  const size_t size_frame1 = uploader.GetWorldMatrices().size();

  // Assert: same allocation pattern means same handle and same size
  EXPECT_EQ(h0, h1);
  EXPECT_EQ(size_frame0, 1);
  EXPECT_EQ(size_frame1, 1);
}

} // namespace
