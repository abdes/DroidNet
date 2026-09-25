//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>

namespace oxygen::vortex::environment::internal {
namespace {

  using graphics::ResourceStates;
  using graphics::ResourceViewType;
  namespace root = bindless::generated::d3d12;

  struct Work {
    std::uint32_t source_srv {};
    std::uint32_t output_uav {};
    std::uint32_t metadata_uav {};
    std::uint32_t partials_uav {};
    std::uint32_t input_uav {};
    std::uint32_t source_size {};
    std::uint32_t output_size {};
    std::uint32_t output_mip {};
    std::uint32_t maximum_mip {};
    std::uint32_t partial_count {};
    std::uint32_t revision {};
    std::uint32_t hemisphere_enabled {};
    std::array<float, 4> lower_hemisphere {};
    float source_rotation {};
    std::array<float, 3> padding {};
  };
  static_assert(sizeof(Work) == 80U);

  struct TextureAllocation {
    std::shared_ptr<graphics::Texture> resource;
    graphics::RegistrationOwner registration;
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    std::vector<ShaderVisibleIndex> uavs;
  };
  struct BufferAllocation {
    std::shared_ptr<graphics::Buffer> resource;
    graphics::RegistrationOwner registration;
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav { kInvalidShaderVisibleIndex };
  };

  auto MakeTexture(Graphics& graphics, std::uint32_t size, std::uint32_t mips,
    Format format, const std::string& name) -> TextureAllocation
  {
    auto result = TextureAllocation {};
    result.resource = graphics.CreateTexture({ .width = size,
      .height = size,
      .array_size = 6U,
      .mip_levels = mips,
      .format = format,
      .texture_type = TextureType::kTextureCube,
      .debug_name = name,
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon });
    auto& registry = graphics.GetResourceRegistry();
    auto registration = registry.RegisterManaged(result.resource);
    if (!registration)
      throw std::bad_alloc {};
    result.registration = registration->AllocationOwner();
    const auto srv
      = registry.AcquireManagedView<graphics::Texture>(*registration,
        { .view_type = ResourceViewType::kTexture_SRV,
          .format = format,
          .dimension = TextureType::kTextureCube,
          .sub_resources = graphics::TextureSubResourceSet::EntireTexture() },
        bindless::generated::kTexturesDomain);
    if (!srv)
      throw std::bad_alloc {};
    result.srv = srv->shader_visible_index;
    for (auto mip = 0U; mip < mips; ++mip) {
      const auto uav
        = registry.AcquireManagedView<graphics::Texture>(*registration,
          { .view_type = ResourceViewType::kTexture_UAV,
            .format = format,
            .dimension = TextureType::kTexture2DArray,
            .sub_resources = { .base_mip_level = mip,
              .num_mip_levels = 1U,
              .base_array_slice = 0U,
              .num_array_slices = 6U } });
      if (!uav)
        throw std::bad_alloc {};
      result.uavs.push_back(uav->shader_visible_index);
    }
    return result;
  }

  auto MakeBuffer(Graphics& graphics, std::uint32_t count, std::uint32_t stride,
    const std::string& name,
    graphics::BufferMemory memory = graphics::BufferMemory::kDeviceLocal)
    -> BufferAllocation
  {
    auto result = BufferAllocation {};
    const auto upload = memory == graphics::BufferMemory::kUpload;
    result.resource = graphics.CreateBuffer(
      { .size_bytes = static_cast<std::uint64_t>(count) * stride,
        .usage = upload ? graphics::BufferUsage::kNone
                        : graphics::BufferUsage::kStorage,
        .memory = memory,
        .debug_name = name });
    auto& registry = graphics.GetResourceRegistry();
    auto registration = registry.RegisterManaged(result.resource);
    if (!registration)
      throw std::bad_alloc {};
    result.registration = registration->AllocationOwner();
    const auto srv
      = registry.AcquireManagedView<graphics::Buffer>(*registration,
        { .view_type = ResourceViewType::kStructuredBuffer_SRV,
          .stride = stride },
        bindless::generated::kGlobalSrvDomain);
    if (!srv)
      throw std::bad_alloc {};
    result.srv = srv->shader_visible_index;
    if (!upload) {
      const auto uav
        = registry.AcquireManagedView<graphics::Buffer>(*registration,
          { .view_type = ResourceViewType::kStructuredBuffer_UAV,
            .stride = stride });
      if (!uav)
        throw std::bad_alloc {};
      result.uav = uav->shader_visible_index;
    }
    return result;
  }

  template <typename Resource>
  auto Track(graphics::CommandRecorder& recorder, const Resource& resource,
    ResourceStates initial = ResourceStates::kCommon) -> void
  {
    if (!recorder.IsResourceTracked(resource)
      && !recorder.AdoptKnownResourceState(resource))
      recorder.BeginTrackingResourceState(resource, initial, true);
  }

  struct ProductStorage {
    std::uint32_t size {};
    TextureAllocation scratch;
    TextureAllocation processed;
    TextureAllocation specular;
    BufferAllocation sh;
    BufferAllocation metadata;
    BufferAllocation partials;
    BufferAllocation constants;
  };

  struct ProductPool {
    std::mutex mutex;
    nexus::IndexReuse<std::uint32_t> reuse;
    std::array<std::shared_ptr<ProductStorage>, IblGpuProcessor::kMaximumSlots>
      slots;
    std::array<std::uint32_t, IblGpuProcessor::kMaximumSlots> free {};
    std::uint32_t capacity {};
    std::uint32_t available {};
    std::uint64_t storage_creations {};
    bool closed { false };
    bool failed { false };
  };

  struct ProductVersion {
    std::shared_ptr<ProductPool> pool;
    std::shared_ptr<ProductStorage> storage;
    nexus::VersionedIndex<std::uint32_t> handle;
    std::optional<nexus::RetirementTicket<std::uint32_t>> retirement;
    std::size_t uses {};
    bool activated { false };
    bool owned { true };
    bool finalized { false };

    // Caller holds pool.mutex. Nexus resolves retirement before the free index
    // is published; completion callbacks neither allocate nor add frame delays.
    auto Finalize() noexcept -> void
    {
      if (owned || uses != 0U || finalized || !retirement)
        return;
      const auto result = retirement->Finalize();
      retirement.reset();
      finalized = true;
      if (result.index && !pool->closed) {
        assert(pool->available < pool->capacity);
        pool->free[pool->available++] = *result.index;
      }
    }
  };

  auto AttachVersion(graphics::CommandRecorder& recorder,
    const std::shared_ptr<ProductVersion>& version) -> void
  {
    recorder.RetainOpaqueUse(version, 0x49424c50524f4400ULL, version.get(),
      { .prepare =
          [](void* pointer, graphics::QueueIdentity) {
            auto& value = *static_cast<ProductVersion*>(pointer);
            std::lock_guard lock(value.pool->mutex);
            if (!value.activated || !value.owned || value.finalized
              || value.pool->failed)
              throw std::logic_error("IBL generation is closed");
            ++value.uses;
          },
        .valid =
          [](const void* pointer) noexcept {
            const auto& value = *static_cast<const ProductVersion*>(pointer);
            std::lock_guard lock(value.pool->mutex);
            return !value.finalized && !value.pool->failed;
          },
        .submitted =
          [](void* pointer, graphics::QueueIdentity,
            const graphics::SubmissionResult& result) noexcept {
            if (result.outcome
              != graphics::SubmissionOutcome::kExecutionUncertain)
              return;
            auto& value = *static_cast<ProductVersion*>(pointer);
            std::lock_guard lock(value.pool->mutex);
            value.pool->failed = true;
            value.pool->closed = true;
            value.pool->reuse.Close();
          },
        .released =
          [](void* pointer, graphics::QueueIdentity,
            graphics::SubmissionOutcome,
            graphics::UseReleaseReason reason) noexcept {
            auto& value = *static_cast<ProductVersion*>(pointer);
            std::lock_guard lock(value.pool->mutex);
            assert(value.uses > 0U);
            --value.uses;
            if (reason == graphics::UseReleaseReason::kDeviceLost) {
              value.pool->failed = true;
              value.pool->closed = true;
              value.pool->reuse.Close();
            }
            value.Finalize();
          } });
  }

} // namespace

struct IblGenerationOwner {
  explicit IblGenerationOwner(std::shared_ptr<ProductVersion> value)
    : version(std::move(value))
  {
  }
  ~IblGenerationOwner()
  {
    if (!version->activated)
      return;
    std::lock_guard lock(version->pool->mutex);
    assert(version->owned);
    version->owned = false;
    auto ticket = version->pool->reuse.TryRetire(version->handle);
    assert(ticket.has_value());
    if (ticket)
      version->retirement.emplace(std::move(*ticket));
    version->Finalize();
  }
  IblGenerationOwner(const IblGenerationOwner&) = delete;
  auto operator=(const IblGenerationOwner&) -> IblGenerationOwner& = delete;
  std::shared_ptr<ProductVersion> version;
};

struct IblGpuProcessor::Impl {
  Graphics& graphics;
  std::shared_ptr<ProductPool> pool;
};

IblGpuProcessor::IblGpuProcessor(
  Graphics& graphics, const std::uint32_t capacity)
  : impl_(std::make_unique<Impl>(graphics, std::make_shared<ProductPool>()))
{
  if (capacity == 0U || capacity > kMaximumSlots)
    throw std::invalid_argument("IBL capacity exceeds its generation bound");
  impl_->pool->capacity = capacity;
  impl_->pool->available = capacity;
  for (auto i = 0U; i < capacity; ++i)
    impl_->pool->free[i] = capacity - i - 1U;
}
IblGpuProcessor::~IblGpuProcessor() { Close(); }

auto IblGpuProcessor::Close() noexcept -> void
{
  std::lock_guard lock(impl_->pool->mutex);
  impl_->pool->closed = true;
  impl_->pool->reuse.Close();
  impl_->pool->slots.fill(nullptr);
  impl_->pool->available = 0U;
}

auto IblGpuProcessor::GetStats() const -> Stats
{
  std::lock_guard lock(impl_->pool->mutex);
  auto result = Stats { .capacity = impl_->pool->capacity,
    .available = impl_->pool->available,
    .storage_creations = impl_->pool->storage_creations,
    .reuse = impl_->pool->reuse.GetTelemetrySnapshot() };
  for (const auto& slot : impl_->pool->slots)
    if (slot)
      ++result.allocated;
  return result;
}

auto IblGpuProducts::Attach(graphics::CommandRecorder& recorder,
  graphics::ResourceRegistry& registry) const -> bool
{
  if (!allocation || !producer.IsValid() || !brdf || !brdf->producer.IsValid()
    || !processed_cube || !specular_cube || !diffuse_sh || !metadata
    || recorder.GetTargetQueue()->GetQueueRole()
      != graphics::QueueRole::kGraphics)
    return false;
  for (const auto& registration : registrations)
    if (!recorder.RetainRegistration(registry, registration))
      return false;
  AttachVersion(recorder, allocation->version);
  recorder.RecordDependency(producer);
  Track(recorder, *brdf->texture);
  recorder.RequireResourceState(
    *brdf->texture, ResourceStates::kShaderResource);
  Track(recorder, *processed_cube);
  Track(recorder, *specular_cube);
  Track(recorder, *diffuse_sh);
  Track(recorder, *metadata);
  recorder.RequireResourceState(
    *processed_cube, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *specular_cube, ResourceStates::kShaderResource);
  recorder.RequireResourceState(*diffuse_sh, ResourceStates::kShaderResource);
  recorder.RequireResourceState(*metadata, ResourceStates::kShaderResource);
  return true;
}

auto IblGpuProcessor::Process(const std::shared_ptr<graphics::Texture>& source,
  const graphics::RegistrationLease& source_registration,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision,
  const graphics::CompletionReceipt source_producer)
  -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>
{
  const profiling::CpuProfileScope cpu_scope(
    "Vortex.Environment.IBL.Process", profiling::ProfileCategory::kCompute);
  auto& graphics = impl_->graphics;
  auto& registry = graphics.GetResourceRegistry();
  if (!source || !source_registration || revision == 0U
    || !std::has_single_bit(settings.face_size)
    || !std::isfinite(settings.source_rotation_radians)
    || !std::isfinite(settings.lower_hemisphere_blend_alpha)
    || settings.lower_hemisphere_blend_alpha < 0.0F
    || settings.lower_hemisphere_blend_alpha > 1.0F
    || std::ranges::any_of(settings.lower_hemisphere_color,
      [](float value) { return !std::isfinite(value) || value < 0.0F; }))
    return std::unexpected(IblProcessError::kInvalidSource);
  if (!brdf || !brdf->texture || !brdf->registration || !brdf->srv.IsValid()
    || !brdf->producer.IsValid()
    || registry.InspectManagedIdentity(*brdf->texture)
      != brdf->registration.Identity())
    return std::unexpected(IblProcessError::kBrdfUnavailable);
  const auto identity = registry.InspectManagedIdentity(*source);
  const auto& desc = source->GetDescriptor();
  if (!identity || *identity != source_registration.Identity()
    || desc.texture_type != TextureType::kTextureCube || desc.array_size != 6U
    || desc.width != desc.height || desc.width == 0U
    || settings.face_size > desc.width || !desc.is_shader_resource)
    return std::unexpected(IblProcessError::kInvalidSource);

  try {
    graphics.PollCompletedUses();
    registry.PollManagedRetirements();
    auto version = std::make_shared<ProductVersion>();
    version->pool = impl_->pool;
    auto allocation = std::make_shared<IblGenerationOwner>(version);
    {
      std::lock_guard lock(version->pool->mutex);
      if (version->pool->closed)
        return std::unexpected(IblProcessError::kClosed);
      if (version->pool->available == 0U)
        return std::unexpected(IblProcessError::kPoolBusy);
      const auto index = version->pool->free[--version->pool->available];
      try {
        version->handle = version->pool->reuse.ActivateSlot(index);
      } catch (...) {
        version->pool->free[version->pool->available++] = index;
        throw;
      }
      version->activated = true;
      version->storage = version->pool->slots[index];
    }
    const auto source_view
      = registry.AcquireManagedView<graphics::Texture>(source_registration,
        { .view_type = ResourceViewType::kTexture_SRV,
          .format = desc.format,
          .dimension = TextureType::kTextureCube,
          .sub_resources = graphics::TextureSubResourceSet::EntireTexture() },
        bindless::generated::kTexturesDomain);
    if (!source_view)
      return std::unexpected(IblProcessError::kAllocationFailed);
    const auto size = settings.face_size;
    const auto mips = static_cast<std::uint32_t>(std::bit_width(size));
    const auto tiles = ((size + 7U) / 8U) * ((size + 7U) / 8U) * 6U;
    if (!version->storage || version->storage->size != size) {
      {
        std::lock_guard lock(version->pool->mutex);
        version->pool->slots[version->handle.index].reset();
      }
      version->storage.reset();
      registry.PollManagedRetirements();
      auto storage = std::make_shared<ProductStorage>();
      storage->size = size;
      storage->scratch = MakeTexture(
        graphics, size, 1U, Format::kRGBA32Float, "IBL.SourceScratch");
      storage->processed = MakeTexture(
        graphics, size, mips, Format::kRGBA16Float, "IBL.ProcessedCube");
      storage->specular = MakeTexture(
        graphics, size, mips, Format::kRGBA16Float, "IBL.SpecularCube");
      storage->sh = MakeBuffer(graphics, 8U, 16U, "IBL.DiffuseSH");
      storage->metadata
        = MakeBuffer(graphics, 1U, sizeof(IblProductMetadata), "IBL.Metadata");
      storage->partials
        = MakeBuffer(graphics, tiles * 10U, 16U, "IBL.ReductionScratch");
      storage->constants = MakeBuffer(graphics, 2U * mips + 2U, sizeof(Work),
        "IBL.Work", graphics::BufferMemory::kUpload);
      version->storage = storage;
      std::lock_guard lock(version->pool->mutex);
      version->pool->slots[version->handle.index] = std::move(storage);
      ++version->pool->storage_creations;
    }
    auto& scratch = version->storage->scratch;
    auto& processed = version->storage->processed;
    auto& specular = version->storage->specular;
    auto& sh = version->storage->sh;
    auto& metadata = version->storage->metadata;
    auto& partials = version->storage->partials;
    auto& constants = version->storage->constants;
    auto work = std::vector<Work> {};
    auto base = Work { .source_srv = source_view->shader_visible_index.get(),
      .output_uav = scratch.uavs[0].get(),
      .metadata_uav = metadata.uav.get(),
      .partials_uav = partials.uav.get(),
      .input_uav = scratch.uavs[0].get(),
      .source_size = size,
      .output_size = size,
      .maximum_mip = mips - 1U,
      .partial_count = tiles,
      .revision = revision,
      .hemisphere_enabled = settings.lower_hemisphere_solid_color ? 1U : 0U,
      .lower_hemisphere = { settings.lower_hemisphere_color[0],
        settings.lower_hemisphere_color[1], settings.lower_hemisphere_color[2],
        settings.lower_hemisphere_blend_alpha },
      .source_rotation = settings.source_rotation_radians };
    work.push_back(base); // Prepare and range use record zero.
    base.output_uav = processed.uavs[0].get();
    work.push_back(base); // Normalize.
    for (auto mip = 1U; mip < mips; ++mip) {
      base.input_uav = processed.uavs[mip - 1U].get();
      base.output_uav = processed.uavs[mip].get();
      base.output_size = size >> mip;
      work.push_back(base);
    }
    base.input_uav = processed.uavs[0].get();
    base.output_uav = sh.uav.get();
    const auto sh_work = static_cast<std::uint32_t>(work.size());
    work.push_back(base);
    const auto specular_work = static_cast<std::uint32_t>(work.size());
    for (auto mip = 0U; mip < mips; ++mip) {
      base.source_srv = processed.srv.get();
      base.output_uav = specular.uavs[mip].get();
      base.output_size = size >> mip;
      base.output_mip = mip;
      work.push_back(base);
    }
    constants.resource->Update(work.data(), work.size() * sizeof(Work), 0U);

    auto result = std::make_shared<IblGpuProducts>();
    result->allocation = allocation;
    result->slot = version->handle;
    result->brdf = brdf;
    result->processed_cube = processed.resource;
    result->specular_cube = specular.resource;
    result->diffuse_sh = sh.resource;
    result->metadata = metadata.resource;
    result->processed_srv = processed.srv;
    result->specular_srv = specular.srv;
    result->diffuse_sh_srv = sh.srv;
    result->metadata_srv = metadata.srv;
    result->maximum_mip = mips - 1U;
    result->revision = revision;
    for (const auto& owner : { processed.registration, specular.registration,
           sh.registration, metadata.registration }) {
      auto lease = registry.AcquireManaged(owner.Identity());
      if (!lease)
        return std::unexpected(IblProcessError::kAllocationFailed);
      result->registrations.push_back(std::move(*lease));
    }
    result->registrations.push_back(brdf->registration);

    auto recording = graphics.AcquireCommandRecorder(
      graphics.QueueKeyFor(graphics::QueueRole::kGraphics),
      "Vortex.Environment.IBL", graphics::SubmissionPolicy::kExplicit);
    if (!recording)
      return std::unexpected(IblProcessError::kRecordingFailed);
    auto& recorder = *recording;
    AttachVersion(recorder, version);
    {
      const graphics::GpuEventScope total_scope(recorder,
        "Vortex.Environment.IBL.Process",
        profiling::ProfileGranularity::kTelemetry,
        profiling::ProfileCategory::kCompute);
      recorder.RecordDependency(brdf->producer);
      if (!recorder.RetainRegistration(registry, brdf->registration))
        return std::unexpected(IblProcessError::kBrdfUnavailable);
      if (source_producer.IsValid())
        recorder.RecordDependency(source_producer);
      for (const auto& registration : { source_registration.AllocationOwner(),
             scratch.registration, processed.registration,
             specular.registration, sh.registration, metadata.registration,
             partials.registration, constants.registration }) {
        if (!recorder.RetainRegistration(registry, registration))
          return std::unexpected(IblProcessError::kRecordingFailed);
      }
      Track(recorder, *source);
      recorder.RequireResourceState(*source, ResourceStates::kShaderResource);
      for (const auto& texture :
        { scratch.resource, processed.resource, specular.resource }) {
        Track(recorder, *texture);
        recorder.EnableAutoMemoryBarriers(*texture);
        recorder.RequireResourceState(
          *texture, ResourceStates::kUnorderedAccess);
      }
      for (const auto& buffer :
        { sh.resource, metadata.resource, partials.resource }) {
        Track(recorder, *buffer);
        recorder.EnableAutoMemoryBarriers(*buffer);
        recorder.RequireResourceState(
          *buffer, ResourceStates::kUnorderedAccess);
      }
      Track(recorder, *constants.resource, ResourceStates::kGenericRead);
      const auto bindings = vortex::internal::BuildVortexRootBindings();
      const auto dispatch = [&](const char* entry, std::uint32_t index,
                              std::uint32_t groups, std::uint32_t faces) {
        const graphics::GpuEventScope phase_scope(recorder, entry,
          profiling::ProfileGranularity::kDiagnostic,
          profiling::ProfileCategory::kCompute);
        const auto pipeline
          = graphics::ComputePipelineDesc::Builder {}
              .SetComputeShader({ .stage = ShaderType::kCompute,
                .source_path = "Vortex/Services/Environment/IblProcessing.hlsl",
                .entry_point = entry })
              .SetRootBindings(bindings)
              .SetDebugName(entry)
              .Build();
        recorder.FlushBarriers();
        recorder.SetPipelineState(pipeline);
        recorder.SetComputeRoot32BitConstant(
          static_cast<std::uint32_t>(root::RootParam::kRootConstants), index,
          0U);
        recorder.SetComputeRoot32BitConstant(
          static_cast<std::uint32_t>(root::RootParam::kRootConstants),
          constants.srv.get(), 1U);
        recorder.Dispatch(groups, faces == 6U ? groups : 1U, faces);
        // Explicit repeated UAV use inserts the producer/consumer memory
        // barrier.
        for (const auto& buffer : { partials.resource, metadata.resource })
          recorder.RequireResourceState(
            *buffer, ResourceStates::kUnorderedAccess);
      };
      dispatch("IblInitializeCS", 0U, 1U, 1U);
      dispatch("IblPrepareCS", 0U, (size + 7U) / 8U, 6U);
      recorder.RequireResourceState(
        *scratch.resource, ResourceStates::kUnorderedAccess);
      dispatch("IblRangeCS", 0U, 1U, 1U);
      dispatch("IblNormalizeCS", 1U, (size + 7U) / 8U, 6U);
      for (auto mip = 1U; mip < mips; ++mip) {
        recorder.RequireResourceState(
          *processed.resource, ResourceStates::kUnorderedAccess);
        dispatch("IblMipCS", mip + 1U, ((size >> mip) + 7U) / 8U, 6U);
      }
      recorder.RequireResourceState(
        *processed.resource, ResourceStates::kUnorderedAccess);
      dispatch("IblShCS", sh_work, (size + 7U) / 8U, 6U);
      dispatch("IblShReduceCS", sh_work, 1U, 1U);
      recorder.RequireResourceState(
        *processed.resource, ResourceStates::kShaderResource);
      for (auto mip = 0U; mip < mips; ++mip) {
        dispatch(
          "IblPrefilterCS", specular_work + mip, ((size >> mip) + 7U) / 8U, 6U);
        recorder.RequireResourceState(
          *specular.resource, ResourceStates::kUnorderedAccess);
      }
      dispatch("IblCompleteCS", 0U, 1U, 1U);
      recorder.RequireResourceStateFinal(
        *processed.resource, ResourceStates::kShaderResource);
      recorder.RequireResourceStateFinal(
        *specular.resource, ResourceStates::kShaderResource);
      recorder.RequireResourceStateFinal(
        *sh.resource, ResourceStates::kShaderResource);
      recorder.RequireResourceStateFinal(
        *metadata.resource, ResourceStates::kShaderResource);
    }
    const auto submission = recording.SubmitWithReceipt();
    if (submission.outcome != graphics::SubmissionOutcome::kSubmitted
      || !submission.receipt)
      return std::unexpected(IblProcessError::kSubmissionFailed);
    result->producer = *submission.receipt;
    return result;
  } catch (const std::bad_alloc&) {
    return std::unexpected(IblProcessError::kAllocationFailed);
  } catch (const std::exception&) {
    return std::unexpected(IblProcessError::kRecordingFailed);
  }
}

} // namespace oxygen::vortex::environment::internal
