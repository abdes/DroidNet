//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>

// Reference draws for the mesh draw index contract on the GPU.
//
// Every mesh draw carries its draw index in StartInstanceLocation, and shaders
// read it as SV_StartInstanceLocation. Each test renders eight one-pixel
// columns into an R32_UINT target: draw `i` writes `i + 1` into column `i`.
// Direct draws are the control; each ExecuteIndirect form the indirect lists
// use must produce the same columns.

namespace {

using oxygen::Format;
using oxygen::Scissors;
using oxygen::ViewPort;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferRange;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ShaderRequest;
using oxygen::graphics::Texture;

constexpr auto kColumns = std::uint32_t { 8U };
constexpr auto kQuadVertices = std::uint32_t { 6U };
constexpr auto kCountLimit = std::uint32_t { 5U };
constexpr auto kTexelBytes = sizeof(std::uint32_t);

using Columns = std::array<std::uint32_t, kColumns>;

//! One indirect command: a D3D12_DRAW_ARGUMENTS record.
struct DrawCommand {
  std::uint32_t vertex_count_per_instance { 0U };
  std::uint32_t instance_count { 0U };
  std::uint32_t start_vertex_location { 0U };
  std::uint32_t start_instance_location { 0U };
};

auto VertexShader() -> ShaderRequest
{
  return ShaderRequest {
    .stage = oxygen::ShaderType::kVertex,
    .source_path = "Tests/IndirectDrawReference.hlsl",
    .entry_point = "VS",
  };
}

auto PixelShader() -> ShaderRequest
{
  return ShaderRequest {
    .stage = oxygen::ShaderType::kPixel,
    .source_path = "Tests/IndirectDrawReference.hlsl",
    .entry_point = "PS",
  };
}

auto LoadBytecode(const char* path)
  -> std::shared_ptr<oxygen::graphics::IShaderByteCode>
{
  auto file = std::ifstream(path, std::ios::binary);
  CHECK_F(file.good(), "Missing reference shader `{}`", path);
  file.seekg(0, std::ios::end);
  const auto bytes = static_cast<std::size_t>(file.tellg());
  CHECK_F(bytes > 0U && bytes % sizeof(std::uint32_t) == 0U);
  auto code = std::vector<std::uint32_t>(bytes / sizeof(std::uint32_t));
  file.seekg(0);
  // ifstream reads object bytes through char storage; the destination is
  // allocated uint32 shader bytecode.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  file.read(
    reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(bytes));
  CHECK_F(file.good());
  return std::make_shared<
    oxygen::graphics::ShaderByteCode<std::vector<std::uint32_t>>>(
    std::move(code));
}

//! One single-quad command per column; the draw index is the column.
auto ColumnCommands() -> std::vector<DrawCommand>
{
  auto commands = std::vector<DrawCommand> {};
  for (std::uint32_t index = 0U; index < kColumns; ++index) {
    commands.push_back(DrawCommand {
      .vertex_count_per_instance = kQuadVertices,
      .instance_count = 1U,
      .start_instance_location = index,
    });
  }
  return commands;
}

auto DrawSignature() -> CommandRecorder::IndirectCommandDesc
{
  return CommandRecorder::IndirectCommandDesc {
    .kind = CommandRecorder::IndirectCommandKind::kDraw,
  };
}

auto Ascending(const std::uint32_t drawn) -> Columns
{
  auto columns = Columns {};
  for (std::uint32_t index = 0U; index < drawn; ++index) {
    columns.at(index) = index + 1U;
  }
  return columns;
}

class IndirectDrawReferenceGpuTest
  : public oxygen::vortex::testing::exposure::ExposureGpuTest {
protected:
  auto SetUp() -> void override
  {
    ExposureGpuTest::SetUp();
    if (HasFatalFailure()) {
      return;
    }
    FailureBackend().SetShaderOverride(
      VertexShader(), LoadBytecode(OXYGEN_INDIRECT_DRAW_REFERENCE_VS));
    FailureBackend().SetShaderOverride(
      PixelShader(), LoadBytecode(OXYGEN_INDIRECT_DRAW_REFERENCE_PS));
    target_ = CreateRegisteredTexture({
      .width = kColumns,
      .height = 1U,
      .format = Format::kR32UInt,
      .debug_name = "Indirect reference target",
      .is_render_target = true,
      .clear_value = {},
      .use_clear_value = true,
      .initial_state = ResourceStates::kCommon,
    });
    framebuffer_ = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(target_));
    ASSERT_NE(framebuffer_, nullptr);
  }

  //! An upload-heap buffer holding `values`; always readable as arguments.
  template <typename T>
  auto MakeArguments(const std::span<const T> values, const std::string& name)
    -> std::shared_ptr<Buffer>
  {
    auto buffer = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = values.size_bytes(),
      .usage = BufferUsage::kIndirect,
      .memory = BufferMemory::kUpload,
      .debug_name = name,
    });
    buffer->Update(values.data(), values.size_bytes(), 0U);
    return buffer;
  }

  //! A device-local copy of `values` in the indirect-argument state, like the
  //! lists the GPU compacts.
  template <typename T>
  auto MakeDeviceArguments(const std::span<const T> values,
    const std::string& name) -> std::shared_ptr<Buffer>
  {
    const auto staging = MakeArguments(values, name + ".Staging");
    auto buffer = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = values.size_bytes(),
      .usage = BufferUsage::kIndirect | BufferUsage::kStorage,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = name,
    });
    {
      auto recorder = AcquireRecorder(name + " upload");
      recorder->BeginTrackingResourceState(
        *buffer, ResourceStates::kCommon, true);
      recorder->BeginTrackingResourceState(
        *staging, ResourceStates::kGenericRead, false);
      recorder->RequireResourceState(*buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*buffer, 0U, *staging, 0U, values.size_bytes());
      recorder->RequireResourceStateFinal(
        *buffer, ResourceStates::kIndirectArgument);
    }
    WaitForQueueIdle();
    return buffer;
  }

  //! Clears the target, binds the reference pipeline, records `draws` and
  //! returns the eight columns.
  auto Render(const std::function<void(CommandRecorder&)>& draws) -> Columns
  {
    {
      auto recorder = AcquireRecorder("Indirect reference draws");
      recorder->BeginTrackingResourceState(
        *target_, ResourceStates::kCommon, true);
      recorder->RequireResourceState(*target_, ResourceStates::kRenderTarget);
      recorder->FlushBarriers();
      recorder->ClearFramebuffer(*framebuffer_);
      recorder->BindFrameBuffer(*framebuffer_);
      recorder->SetViewport(ViewPort {
        .width = static_cast<float>(kColumns),
        .height = 1.0F,
      });
      recorder->SetScissors(Scissors {
        .right = static_cast<std::int32_t>(kColumns),
        .bottom = 1,
      });
      recorder->SetPipelineState(Pipeline());
      draws(*recorder);
      recorder->RequireResourceStateFinal(
        *target_, ResourceStates::kCopySource);
    }
    WaitForQueueIdle();
    return ReadColumns();
  }

private:
  static auto Pipeline() -> GraphicsPipelineDesc
  {
    auto root_bindings = oxygen::vortex::internal::BuildVortexRootBindings();
    return GraphicsPipelineDesc::Builder {}
      .SetVertexShader(VertexShader())
      .SetPixelShader(PixelShader())
      .SetPrimitiveTopology(oxygen::graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(oxygen::graphics::RasterizerStateDesc {
        .cull_mode = oxygen::graphics::CullMode::kNone,
      })
      .SetDepthStencilState(oxygen::graphics::DepthStencilStateDesc {})
      .SetBlendState({ oxygen::graphics::BlendTargetDesc {} })
      .SetFramebufferLayout(oxygen::graphics::FramebufferLayoutDesc {
        .color_target_formats = { Format::kR32UInt },
      })
      .SetRootBindings(std::span<const oxygen::graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Indirect reference draws")
      .Build();
  }

  auto ReadColumns() -> Columns
  {
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Indirect reference");
    {
      auto recorder = AcquireRecorder("Indirect reference readback");
      CHECK_F(recorder->AdoptKnownResourceState(*target_));
      CHECK_F(readback->EnqueueCopy(*recorder, *target_, {}).has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    const auto bytes = MappedTextureBytes(*mapped, kTexelBytes);
    auto columns = Columns {};
    CHECK_F(bytes.size() >= sizeof(columns));
    std::memcpy(columns.data(), bytes.data(), sizeof(columns));
    return columns;
  }

  std::shared_ptr<Texture> target_;
  std::shared_ptr<Framebuffer> framebuffer_;
};

//! The control: one direct draw per column, the draw index as start instance.
NOLINT_TEST_F(IndirectDrawReferenceGpuTest, DirectDrawsCarryTheDrawIndex)
{
  const auto columns = Render([](CommandRecorder& recorder) -> void {
    for (std::uint32_t index = 0U; index < kColumns; ++index) {
      recorder.Draw(kQuadVertices, 1U, 0U, index);
    }
  });

  EXPECT_EQ(columns, Ascending(kColumns));
}

//! Indirect draws deliver each command's start instance as its draw index.
NOLINT_TEST_F(IndirectDrawReferenceGpuTest, IndirectDrawsCarryTheDrawIndex)
{
  const auto commands = ColumnCommands();
  const auto arguments = MakeArguments(
    std::span<const DrawCommand>(commands), "Indirect reference draws");

  const auto columns = Render([&](CommandRecorder& recorder) -> void {
    recorder.ExecuteIndirect(*arguments, DrawSignature(),
      CommandRecorder::IndirectExecutionDesc {
        .command_count = CommandRecorder::IndirectCommandCount { kColumns },
      });
  });

  EXPECT_EQ(columns, Ascending(kColumns));
}

//! A count buffer below the maximum command count limits the draws.
NOLINT_TEST_F(IndirectDrawReferenceGpuTest, CountBufferLimitsTheDraws)
{
  const auto commands = ColumnCommands();
  const auto arguments = MakeArguments(
    std::span<const DrawCommand>(commands), "Indirect reference draws");
  const auto counts = std::array { kCountLimit };
  const auto count_buffer = MakeArguments(
    std::span<const std::uint32_t>(counts), "Indirect reference count");

  const auto columns = Render([&](CommandRecorder& recorder) -> void {
    recorder.ExecuteIndirect(*arguments, DrawSignature(),
      CommandRecorder::IndirectExecutionDesc {
        .command_count = CommandRecorder::IndirectCommandCount { kColumns },
        .count_buffer
        = oxygen::observer_ptr<const Buffer> { count_buffer.get() },
        .count_buffer_range = BufferRange { 0U, sizeof(std::uint32_t) },
      });
  });

  EXPECT_EQ(columns, Ascending(kCountLimit));
}

//! Device-local arguments and counts in the indirect-argument state, issued
//! one segment at a time at a byte offset, as the compacted lists are.
NOLINT_TEST_F(IndirectDrawReferenceGpuTest, DeviceLocalSegmentsAtOffsets)
{
  constexpr auto kSegmentSize = kColumns / 2U;
  const auto commands = ColumnCommands();
  const auto arguments = MakeDeviceArguments(
    std::span<const DrawCommand>(commands), "Indirect reference device draws");
  const auto counts = std::array { kSegmentSize, kSegmentSize - 1U };
  const auto count_buffer = MakeDeviceArguments(
    std::span<const std::uint32_t>(counts), "Indirect reference device counts");

  const auto columns = Render([&](CommandRecorder& recorder) -> void {
    CHECK_F(recorder.AdoptKnownResourceState(*arguments));
    CHECK_F(recorder.AdoptKnownResourceState(*count_buffer));
    for (std::uint32_t segment = 0U; segment < counts.size(); ++segment) {
      recorder.ExecuteIndirect(*arguments, DrawSignature(),
        CommandRecorder::IndirectExecutionDesc {
          .argument_buffer_range = BufferRange {
            static_cast<std::uint64_t>(segment) * kSegmentSize
              * sizeof(DrawCommand),
            static_cast<std::uint64_t>(kSegmentSize) * sizeof(DrawCommand),
          },
          .command_count
          = CommandRecorder::IndirectCommandCount { kSegmentSize },
          .count_buffer = oxygen::observer_ptr<const Buffer> {
            count_buffer.get() },
          .count_buffer_range = BufferRange {
            static_cast<std::uint64_t>(segment) * sizeof(std::uint32_t),
            sizeof(std::uint32_t),
          },
        });
    }
  });

  EXPECT_EQ(columns, (Columns { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 0U }));
}

} // namespace
