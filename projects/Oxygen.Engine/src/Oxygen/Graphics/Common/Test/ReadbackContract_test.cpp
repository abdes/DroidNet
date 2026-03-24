//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackValidation.h>

namespace {

using oxygen::SizeBytes;
using oxygen::TextureType;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferRange;
using oxygen::graphics::ClearFlags;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::GpuBufferReadback;
using oxygen::graphics::GpuTextureReadback;
using oxygen::graphics::MappedBufferReadback;
using oxygen::graphics::MappedTextureReadback;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackState;
using oxygen::graphics::ReadbackTicket;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureReadbackLayout;
using oxygen::graphics::TextureReadbackRequest;
using oxygen::graphics::ValidateTextureReadbackRequest;

class ContractBufferReadback final : public GpuBufferReadback {
public:
  explicit ContractBufferReadback(std::vector<std::byte> bytes)
    : bytes_(std::move(bytes))
  {
  }

  auto EnqueueCopy(CommandRecorder& /*recorder*/, const Buffer& /*source*/,
    BufferRange /*range*/ = {})
    -> std::expected<ReadbackTicket, ReadbackError> override
  {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  [[nodiscard]] auto GetState() const noexcept -> ReadbackState override
  {
    return active_map_.expired() ? ReadbackState::kReady
                                 : ReadbackState::kMapped;
  }

  [[nodiscard]] auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto IsReady() const
    -> std::expected<bool, ReadbackError> override
  {
    return true;
  }

  auto TryMap() -> std::expected<MappedBufferReadback, ReadbackError> override
  {
    return CreateMapping();
  }

  auto MapNow() -> std::expected<MappedBufferReadback, ReadbackError> override
  {
    return CreateMapping();
  }

  auto Cancel() -> std::expected<bool, ReadbackError> override { return false; }

  auto Reset() -> void override { active_map_.reset(); }

private:
  auto CreateMapping() -> std::expected<MappedBufferReadback, ReadbackError>
  {
    if (!active_map_.expired()) {
      return std::unexpected(ReadbackError::kAlreadyMapped);
    }

    auto guard = std::make_shared<int>(1);
    active_map_ = std::static_pointer_cast<void>(guard);
    return MappedBufferReadback { std::static_pointer_cast<void>(guard),
      std::span<const std::byte> { bytes_.data(), bytes_.size() } };
  }

  std::weak_ptr<void> active_map_ {};
  std::vector<std::byte> bytes_;
};

class ContractTextureReadback final : public GpuTextureReadback {
public:
  ContractTextureReadback(
    std::vector<std::byte> bytes, TextureReadbackLayout layout)
    : bytes_(std::move(bytes))
    , layout_(std::move(layout))
  {
  }

  auto EnqueueCopy(CommandRecorder& /*recorder*/, const Texture& /*source*/,
    TextureReadbackRequest /*request*/ = {})
    -> std::expected<ReadbackTicket, ReadbackError> override
  {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  [[nodiscard]] auto GetState() const noexcept -> ReadbackState override
  {
    return active_map_.expired() ? ReadbackState::kReady
                                 : ReadbackState::kMapped;
  }

  [[nodiscard]] auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto IsReady() const
    -> std::expected<bool, ReadbackError> override
  {
    return true;
  }

  auto TryMap() -> std::expected<MappedTextureReadback, ReadbackError> override
  {
    return CreateMapping();
  }

  auto MapNow() -> std::expected<MappedTextureReadback, ReadbackError> override
  {
    return CreateMapping();
  }

  auto Cancel() -> std::expected<bool, ReadbackError> override { return false; }

  auto Reset() -> void override { active_map_.reset(); }

private:
  auto CreateMapping() -> std::expected<MappedTextureReadback, ReadbackError>
  {
    if (!active_map_.expired()) {
      return std::unexpected(ReadbackError::kAlreadyMapped);
    }

    auto guard = std::make_shared<int>(1);
    active_map_ = std::static_pointer_cast<void>(guard);
    return MappedTextureReadback { std::static_pointer_cast<void>(guard),
      bytes_.data(), layout_ };
  }

  std::weak_ptr<void> active_map_ {};
  std::vector<std::byte> bytes_;
  TextureReadbackLayout layout_ {};
};

NOLINT_TEST(ReadbackContractTest, TextureReadbackRequestDefaultsMatchContract)
{
  const TextureReadbackRequest request;

  EXPECT_EQ(request.aspects, ClearFlags::kColor);
  EXPECT_EQ(
    request.msaa_mode, oxygen::graphics::MsaaReadbackMode::kResolveIfNeeded);
}

NOLINT_TEST(ReadbackContractTest,
  ValidateTextureReadbackRequestAcceptsColorReadbackForColorTexture)
{
  const TextureDesc desc {
    .width = 4,
    .height = 4,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = TextureType::kTexture2D,
  };

  const auto result = ValidateTextureReadbackRequest(desc, {});

  EXPECT_TRUE(result.has_value());
}

NOLINT_TEST(ReadbackContractTest,
  ValidateTextureReadbackRequestRejectsUnknownAndTypelessFormats)
{
  TextureDesc unknown_desc {
    .width = 4,
    .height = 4,
    .format = oxygen::Format::kUnknown,
    .texture_type = TextureType::kTexture2D,
  };
  TextureDesc typeless_desc {
    .width = 4,
    .height = 4,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = TextureType::kTexture2D,
    .is_typeless = true,
  };

  const auto unknown = ValidateTextureReadbackRequest(unknown_desc, {});
  const auto typeless = ValidateTextureReadbackRequest(typeless_desc, {});

  ASSERT_FALSE(unknown.has_value());
  ASSERT_FALSE(typeless.has_value());
  EXPECT_EQ(unknown.error(), ReadbackError::kUnsupportedFormat);
  EXPECT_EQ(typeless.error(), ReadbackError::kUnsupportedFormat);
}

NOLINT_TEST(
  ReadbackContractTest, ValidateTextureReadbackRequestRejectsMixedAspectMasks)
{
  const TextureDesc desc {
    .width = 4,
    .height = 4,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = TextureType::kTexture2D,
  };

  const auto result = ValidateTextureReadbackRequest(desc,
    TextureReadbackRequest {
      .aspects = ClearFlags::kColor | ClearFlags::kDepth,
    });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ReadbackError::kInvalidArgument);
}

NOLINT_TEST(ReadbackContractTest,
  ValidateTextureReadbackRequestRejectsDepthAspectOnColorTextures)
{
  const TextureDesc desc {
    .width = 4,
    .height = 4,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = TextureType::kTexture2D,
  };

  const auto depth = ValidateTextureReadbackRequest(desc,
    TextureReadbackRequest {
      .aspects = ClearFlags::kDepth,
    });
  const auto stencil = ValidateTextureReadbackRequest(desc,
    TextureReadbackRequest {
      .aspects = ClearFlags::kStencil,
    });

  ASSERT_FALSE(depth.has_value());
  ASSERT_FALSE(stencil.has_value());
  EXPECT_EQ(depth.error(), ReadbackError::kInvalidArgument);
  EXPECT_EQ(stencil.error(), ReadbackError::kInvalidArgument);
}

NOLINT_TEST(ReadbackContractTest,
  ValidateTextureReadbackRequestRejectsDepthStencilReadbackUntilBackendSupportsIt)
{
  const TextureDesc desc {
    .width = 4,
    .height = 4,
    .format = oxygen::Format::kDepth24Stencil8,
    .texture_type = TextureType::kTexture2D,
  };

  const auto depth = ValidateTextureReadbackRequest(desc,
    TextureReadbackRequest {
      .aspects = ClearFlags::kDepth,
    });
  const auto stencil = ValidateTextureReadbackRequest(desc,
    TextureReadbackRequest {
      .aspects = ClearFlags::kStencil,
    });
  const auto color = ValidateTextureReadbackRequest(desc,
    TextureReadbackRequest {
      .aspects = ClearFlags::kColor,
    });

  ASSERT_FALSE(depth.has_value());
  ASSERT_FALSE(stencil.has_value());
  ASSERT_FALSE(color.has_value());
  EXPECT_EQ(depth.error(), ReadbackError::kUnsupportedResource);
  EXPECT_EQ(stencil.error(), ReadbackError::kUnsupportedResource);
  EXPECT_EQ(color.error(), ReadbackError::kInvalidArgument);
}

NOLINT_TEST(ReadbackContractTest,
  ValidateTextureReadbackRequestRejectsMsaaReadbackWhenResolveIsDisallowed)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.sample_count = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2DMultiSample;

  const auto result = ValidateTextureReadbackRequest(desc,
    TextureReadbackRequest {
      .msaa_mode = oxygen::graphics::MsaaReadbackMode::kDisallow,
    });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ReadbackError::kUnsupportedResource);
}

NOLINT_TEST(
  ReadbackContractTest, BufferMappedViewAllowsOnlyOneActiveMapOwnerAtATime)
{
  ContractBufferReadback readback(
    { std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x03 } });

  auto first = readback.TryMap();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(readback.GetState(), ReadbackState::kMapped);

  const auto second = readback.TryMap();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyMapped);

  auto moved = std::move(*first);
  EXPECT_EQ(readback.GetState(), ReadbackState::kMapped);
  EXPECT_EQ(moved.Bytes().size(), 3U);

  const auto third = readback.MapNow();
  ASSERT_FALSE(third.has_value());
  EXPECT_EQ(third.error(), ReadbackError::kAlreadyMapped);

  moved = MappedBufferReadback {};
  EXPECT_EQ(readback.GetState(), ReadbackState::kReady);

  const auto fourth = readback.TryMap();
  ASSERT_TRUE(fourth.has_value());
  EXPECT_EQ(fourth->Bytes().size(), 3U);
}

NOLINT_TEST(
  ReadbackContractTest, TextureMappedViewAllowsOnlyOneActiveMapOwnerAtATime)
{
  ContractTextureReadback readback({ std::byte { 0x10 }, std::byte { 0x20 },
                                     std::byte { 0x30 }, std::byte { 0x40 } },
    TextureReadbackLayout {
      .format = oxygen::Format::kRGBA8UNorm,
      .texture_type = TextureType::kTexture2D,
      .width = 1,
      .height = 1,
      .depth = 1,
      .row_pitch = SizeBytes { 4 },
      .slice_pitch = SizeBytes { 4 },
      .aspects = ClearFlags::kColor,
    });

  auto first = readback.TryMap();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(readback.GetState(), ReadbackState::kMapped);
  EXPECT_EQ(first->Layout().row_pitch, SizeBytes { 4 });

  const auto second = readback.TryMap();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyMapped);

  auto moved = std::move(*first);
  EXPECT_EQ(readback.GetState(), ReadbackState::kMapped);
  ASSERT_NE(moved.Data(), nullptr);
  EXPECT_EQ(moved.Layout().slice_pitch, SizeBytes { 4 });

  moved = MappedTextureReadback {};
  EXPECT_EQ(readback.GetState(), ReadbackState::kReady);

  const auto third = readback.MapNow();
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(third->Layout().aspects, ClearFlags::kColor);
}

} // namespace
