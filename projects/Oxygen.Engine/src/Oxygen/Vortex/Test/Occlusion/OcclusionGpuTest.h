//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::occlusion {

//! GPU fixture with the buffer helpers the occlusion tests share.
class OcclusionGpuTest : public exposure::ExposureGpuTest {
protected:
  //! Starts a new frame, so each build gets fresh transient uploads.
  auto NextFrame() -> void
  {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  }

  //! A device-local structured buffer holding `values`, with an SRV.
  template <typename T>
  auto MakeDeviceBuffer(
    const std::span<const T> values, const std::string& name)
    -> std::pair<std::shared_ptr<graphics::Buffer>, ShaderVisibleIndex>
  {
    const auto size = values.size_bytes();
    auto buffer = CreateRegisteredBuffer({
      .size_bytes = size,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = name,
    });
    auto staging = CreateRegisteredBuffer({
      .size_bytes = size,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = name + ".Staging",
    });
    CHECK_F(buffer != nullptr && staging != nullptr);
    staging->Update(values.data(), size, 0U);
    {
      auto recorder = AcquireRecorder(name + " upload");
      recorder->BeginTrackingResourceState(
        *buffer, graphics::ResourceStates::kCommon, true);
      recorder->BeginTrackingResourceState(
        *staging, graphics::ResourceStates::kGenericRead, false);
      recorder->RequireResourceState(
        *buffer, graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*buffer, 0U, *staging, 0U, size);
      recorder->RequireResourceStateFinal(
        *buffer, graphics::ResourceStates::kShaderResource);
    }
    WaitForQueueIdle();
    return { buffer, RegisterSrv(*buffer, sizeof(T)) };
  }

  //! An upload-heap structured buffer holding `values`, with an SRV.
  template <typename T>
  auto MakeUploadBuffer(
    const std::span<const T> values, const std::string& name)
    -> std::pair<std::shared_ptr<graphics::Buffer>, ShaderVisibleIndex>
  {
    auto buffer = CreateRegisteredBuffer({
      .size_bytes = values.size_bytes(),
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = name,
    });
    CHECK_F(buffer != nullptr);
    buffer->Update(values.data(), values.size_bytes(), 0U);
    return { buffer, RegisterSrv(*buffer, sizeof(T)) };
  }

  auto RegisterSrv(const graphics::Buffer& buffer, const std::size_t stride)
    -> ShaderVisibleIndex
  {
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle
      = allocator.AllocateBindless(bindless::generated::kGlobalSrvDomain,
        graphics::ResourceViewType::kStructuredBuffer_SRV);
    CHECK_F(handle.IsValid());
    const auto index = allocator.GetShaderVisibleIndex(handle);
    Backend().GetResourceRegistry().RegisterView(buffer, std::move(handle),
      graphics::BufferViewDescription {
        .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
        .range = { 0U, buffer.GetSize() },
        .stride = static_cast<std::uint32_t>(stride),
      });
    return index;
  }

  //! Reads `count` words of a buffer the last recorder left in a known state.
  auto ReadUints(const graphics::Buffer& buffer,
    const std::uint64_t offset_bytes, const std::size_t count)
    -> std::vector<std::uint32_t>
  {
    auto readback
      = GetReadbackManager()->CreateBufferReadback("Occlusion test readback");
    CHECK_F(readback != nullptr);
    {
      auto recorder = AcquireRecorder("Occlusion test readback");
      CHECK_F(recorder->AdoptKnownResourceState(buffer));
      CHECK_F(readback
          ->EnqueueCopy(
            *recorder, buffer, { offset_bytes, count * sizeof(std::uint32_t) })
          .has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    auto words = std::vector<std::uint32_t>(count);
    CHECK_F(mapped->Bytes().size() >= count * sizeof(std::uint32_t));
    std::memcpy(
      words.data(), mapped->Bytes().data(), count * sizeof(std::uint32_t));
    return words;
  }

  std::uint64_t sequence_ { 1000U };
};

} // namespace oxygen::vortex::testing::occlusion
