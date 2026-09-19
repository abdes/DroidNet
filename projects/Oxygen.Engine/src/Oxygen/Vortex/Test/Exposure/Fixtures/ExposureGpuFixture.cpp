//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestEngine.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

ExposureGpuTest::ExposureGpuTest() = default;
ExposureGpuTest::~ExposureGpuTest() = default;

auto ExposureProbeRootBindings() -> std::vector<RootBindingItem>
{
  namespace root = oxygen::bindless::generated::d3d12;
  std::vector<RootBindingItem> bindings;
  for (const auto& parameter : root::kRootParamTable) {
    RootBindingDesc binding {};
    binding.binding_slot_desc.register_index = parameter.shader_register;
    binding.binding_slot_desc.register_space = parameter.register_space;
    binding.visibility = ShaderStageFlags::kAll;
    if (parameter.kind == root::RootParamKind::DescriptorTable) {
      const auto& range = parameter.ranges.front();
      CHECK_F(range.range_type == root::RangeType::SRV
        || range.range_type == root::RangeType::Sampler);
      binding.data = DescriptorTableBinding { .view_type
        = range.range_type == root::RangeType::Sampler
          ? ResourceViewType::kSampler
          : ResourceViewType::kRawBuffer_SRV,
        .base_index = range.base_register,
        .count = range.num_descriptors };
    } else if (parameter.kind == root::RootParamKind::CBV) {
      binding.data = DirectBufferBinding {};
    } else {
      binding.data = PushConstantsBinding { .size = parameter.constants_count };
    }
    bindings.emplace_back(binding);
  }
  return bindings;
}

auto ExposureGpuTest::CreateBackend(const SerializedBackendConfig& config,
  const SerializedPathFinderConfig& paths)
  -> std::shared_ptr<graphics::d3d12::Graphics>
{
  return std::make_shared<ExposureFailureGraphics>(config, paths);
}

auto ExposureGpuTest::BackendConfigJson() const -> std::string
{
  if (!CapturePath().empty()) {
    return R"({"enable_debug_layer":true,"frame_capture":{"provider":"renderdoc","init_mode":"search"}})";
  }
  return R"({"enable_debug_layer":true})";
}

auto ExposureGpuTest::CapturePath() -> std::string
{
  char* value = nullptr;
  std::size_t size = 0U;
  if (_dupenv_s(&value, &size, "OXYGEN_EXPOSURE_CAPTURE") != 0 || !value) {
    return {};
  }
  const auto owned
    = std::unique_ptr<char, decltype(&std::free)>(value, &std::free);
  return owned.get();
}

auto ExposureGpuTest::BeginOptionalCapture()
  -> observer_ptr<FrameCaptureController>
{
  const auto path = CapturePath();
  if (path.empty()) {
    return {};
  }
  WaitForQueueIdle();
  const auto capture = Backend().GetFrameCaptureController();
  CHECK_F(capture && capture->IsAvailable());
  CHECK_F(capture->SetCaptureFileTemplate(path));
  CHECK_F(capture->StartCapture());
  return capture;
}

auto ExposureGpuTest::PathFinderConfigJson() const -> std::string
{
  return R"({"workspace_root_path":")" OXYGEN_EXPOSURE_WORKSPACE R"("})";
}

auto ExposureGpuTest::SetUp() -> void
{
  ReadbackTestFixture::SetUp();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  ctx_.current_view.view_id = ViewId { 1U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 1U };
  ctx_.frame_slot = frame::Slot { 0U };
}

auto ExposureGpuTest::TearDown() -> void
{
  FlushBackend();
  registered_targets_.clear();
  last_state_.reset();
  pass_.reset();
  if (renderer_) {
    renderer_->OnShutdown();
    renderer_.reset();
  }
  ReadbackTestFixture::TearDown();
}

auto ExposureGpuTest::MakeSignal(std::uint32_t width, std::uint32_t height,
  std::span<const Pixel> pixels, std::uint32_t depth, Format format,
  bool bindless_texture) -> Signal
{
  CHECK_F(pixels.size() == 1U || pixels.size() == width * height * depth);
  CHECK_F(format == Format::kRGBA32Float || format == Format::kRGBA16Float);
  const auto type
    = depth == 1U ? TextureType::kTexture2D : TextureType::kTexture3D;
  const auto stride = format == Format::kRGBA32Float ? 16U : 8U;
  auto texture = CreateRegisteredTexture({
    .width = width,
    .height = height,
    .depth = depth,
    .format = format,
    .texture_type = type,
    .debug_name = "ExposureFloatFixture",
    .is_shader_resource = true,
    .initial_state = ResourceStates::kCommon,
  });
  const auto pitch = ((width * stride + 255U) / 256U) * 256U;
  auto upload
    = CreateUploadBuffer(SizeBytes { std::uint64_t(pitch) * height * depth });
  std::vector<std::byte> bytes(std::size_t(pitch) * height * depth);
  for (std::uint32_t z = 0; z < depth; ++z) {
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        const auto& pixel
          = pixels[pixels.size() == 1U ? 0U : (z * height + y) * width + x];
        auto* destination
          = bytes.data() + (std::size_t(z) * height + y) * pitch + x * stride;
        if (format == Format::kRGBA32Float) {
          std::memcpy(destination, pixel.data(), 16U);
        } else {
          const std::array packed { data::HalfFloat { pixel[0] }.get(),
            data::HalfFloat { pixel[1] }.get(),
            data::HalfFloat { pixel[2] }.get(),
            data::HalfFloat { pixel[3] }.get() };
          std::memcpy(destination, packed.data(), 8U);
        }
      }
    }
  }
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Exposure fixture upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      {
        .buffer_offset = 0U,
        .buffer_row_pitch = pitch,
        .buffer_slice_pitch = std::uint64_t(pitch) * height,
        .dst_slice = { .width = width, .height = height, .depth = depth },
      },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  WaitForQueueIdle();
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = bindless_texture
    ? allocator.AllocateBindless(oxygen::bindless::generated::kTexturesDomain,
        ResourceViewType::kTexture_SRV)
    : allocator.AllocateRaw(
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  auto srv = allocator.GetShaderVisibleIndex(handle);
  const auto view
    = Backend().GetResourceRegistry().RegisterView(*texture, std::move(handle),
      TextureViewDescription {
        .format = format,
        .dimension = type,
      });
  CHECK_F(view->IsValid());
  return { std::move(texture), srv };
}

auto ExposureGpuTest::Uniform(
  float value, std::uint32_t width, std::uint32_t height) -> Signal
{
  const auto pixels
    = std::array<Pixel, 1> { Pixel { value, value, value, 1.0F } };
  return MakeSignal(width, height, pixels);
}

auto ExposureGpuTest::RunToneProbe(std::span<const std::byte> inputs_data,
  std::uint32_t record_count, std::uint32_t mode, bool capture_enabled)
  -> std::vector<std::array<float, 8>>
{
  if (mode == 16384U) {
    CHECK_EQ_F(record_count, 64U);
  }
  std::ifstream shader(
    OXYGEN_EXPOSURE_TONE_PROBE, std::ios::binary | std::ios::ate);
  CHECK_F(shader.good());
  const auto bytes = static_cast<std::size_t>(shader.tellg());
  CHECK_GT_F(bytes, 0U);
  CHECK_EQ_F(bytes % sizeof(std::uint32_t), 0U);
  std::vector<std::uint32_t> code(bytes / sizeof(std::uint32_t));
  shader.seekg(0);
  shader.read(
    reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(bytes));
  CHECK_F(shader.good());
  static_cast<ExposureFailureGraphics&>(Backend()).tone_probe
    = std::make_shared<ShaderByteCode<std::vector<std::uint32_t>>>(
      std::move(code));

  const auto input_size = inputs_data.size_bytes();
  auto inputs = CreateRegisteredBuffer({ .size_bytes = input_size,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "Tone bound arithmetic inputs" });
  inputs->Update(inputs_data.data(), input_size, 0U);
  auto output = CreateRegisteredBuffer({ .size_bytes = record_count * 32U,
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = "Tone bound arithmetic output" });
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto input_handle
    = allocator.AllocateBindless(oxygen::bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
  const auto input_slot = allocator.GetShaderVisibleIndex(input_handle);
  Backend().GetResourceRegistry().RegisterView(*inputs, std::move(input_handle),
    BufferViewDescription {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .range = { 0U, input_size },
      .stride = 16U });
  auto output_handle = allocator.AllocateRaw(
    ResourceViewType::kRawBuffer_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(output_handle);
  Backend().GetResourceRegistry().RegisterView(*output,
    std::move(output_handle),
    BufferViewDescription { .view_type = ResourceViewType::kRawBuffer_UAV,
      .range = { 0U, record_count * 32U },
      .stride = 0U });
  const auto constants = PublishFixtureData(std::array<std::uint32_t, 4> {
    input_slot.get(), output_slot.get(), record_count, mode });
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest { .stage = ShaderType::kCompute,
          .source_path = "Tests/ToneBoundsProbe.hlsl",
          .entry_point = "CS" })
        .SetRootBindings(ExposureProbeRootBindings())
        .SetDebugName("Tone bound arithmetic probe")
        .Build();
  auto readback = GetReadbackManager()->CreateBufferReadback(
    "Tone bound arithmetic results");
  const auto capture = capture_enabled
    ? BeginOptionalCapture()
    : observer_ptr<FrameCaptureController> {};
  const auto probe_view = ViewConstants::GpuData {};
  auto probe_view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  probe_view_buffer->Update(&probe_view, sizeof(probe_view), 0U);
  {
    auto recorder = AcquireRecorder("Tone bound arithmetic");
    EnsureTracked(*recorder, inputs, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, output, ResourceStates::kCommon);
    recorder->RequireResourceState(*output, ResourceStates::kUnorderedAccess);
    recorder->FlushBarriers();
    recorder->SetPipelineState(pipeline);
    recorder->SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(
        oxygen::bindless::generated::d3d12::RootParam::kViewConstants),
      probe_view_buffer->GetGPUVirtualAddress());
    const auto root = static_cast<std::uint32_t>(
      oxygen::bindless::generated::d3d12::RootParam::kRootConstants);
    recorder->SetComputeRoot32BitConstant(root, 0U, 0U);
    recorder->SetComputeRoot32BitConstant(root, constants.get(), 1U);
    recorder->Dispatch(
      static_cast<std::uint32_t>((record_count + 63U) / 64U), 1U, 1U);
    CHECK_F(
      readback->EnqueueCopy(*recorder, *output, { 0U, record_count * 32U })
        .has_value());
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  const auto mapped = readback->MapNow();
  CHECK_F(mapped.has_value());
  std::vector<std::array<float, 8>> result(record_count);
  std::memcpy(
    result.data(), mapped->Bytes().data(), result.size() * sizeof(result[0]));
  return result;
}

auto ExposureGpuTest::ReadFloatTexture(const Texture& texture, bool allow_half)
  -> std::vector<Pixel>
{
  const bool half = texture.GetDescriptor().format == Format::kRGBA16Float;
  CHECK_F(texture.GetDescriptor().format == Format::kRGBA32Float
    || (allow_half && half));
  auto readback
    = GetReadbackManager()->CreateTextureReadback("Fog edge output");
  {
    auto recorder = AcquireRecorder("Fog edge readback");
    CHECK_F(recorder->AdoptKnownResourceState(texture));
    CHECK_F(readback->EnqueueCopy(*recorder, texture, {}).has_value());
  }
  const auto mapped = readback->MapNow();
  CHECK_F(mapped.has_value());
  const auto& desc = texture.GetDescriptor();
  std::vector<Pixel> pixels(desc.width * desc.height * desc.depth);
  for (unsigned z = 0; z < desc.depth; ++z) {
    for (unsigned y = 0; y < desc.height; ++y) {
      for (unsigned x = 0; x < desc.width; ++x) {
        auto& pixel = pixels[(z * desc.height + y) * desc.width + x];
        const auto* bytes = mapped->Data()
          + z * mapped->Layout().slice_pitch.get()
          + y * mapped->Layout().row_pitch.get()
          + x * (half ? 8U : sizeof(Pixel));
        if (half) {
          std::array<std::uint16_t, 4> packed;
          std::memcpy(packed.data(), bytes, sizeof(packed));
          for (unsigned c = 0; c < 4; ++c) {
            pixel[c] = data::HalfFloat { packed[c] }.ToFloat();
          }
        } else {
          std::memcpy(pixel.data(), bytes, sizeof(pixel));
        }
      }
    }
  }
  return pixels;
}

} // namespace oxygen::vortex::testing::exposure
