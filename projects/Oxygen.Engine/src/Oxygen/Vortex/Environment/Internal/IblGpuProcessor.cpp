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
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/BackendLifetime.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Submission.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Nexus/IndexReuse.h>
#include <Oxygen/Nexus/RetirementState.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/IblWorkBudget.h>
#include <Oxygen/Vortex/Environment/Types/IblCaptureLease.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>

namespace oxygen::vortex::environment::internal {
namespace {

  using graphics::ResourceStates;
  using graphics::ResourceViewType;
  namespace root = bindless::generated::d3d12;

  constexpr auto kCubeFaces = 6U;
  constexpr auto kMipZeroWorkKinds = 6U;
  constexpr auto kGroupEdge = 8U;
  constexpr auto kShPartialElements = 10U;
  constexpr auto kProductUseTag = 0x49424c50524f4400ULL;
  constexpr auto kCaptureUseTag = 0x49424c4341505455ULL;
  constexpr auto kSourceUseTag = 0x49424c4355424553ULL;
  constexpr auto kMirrorRoughness = 0.01;
  constexpr auto kSmoothRoughness = 0.1;
  constexpr auto kRoughnessMipScale = 1.2;

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
    std::uint32_t capture_srv { kInvalidBindlessIndex };
    std::uint32_t processed_half_srv { kInvalidBindlessIndex };
    std::uint32_t specular_half_srv { kInvalidBindlessIndex };
    std::array<std::uint32_t, 3> group_origin {};
    float source_scale { 1.0F };
  };
  constexpr auto kWorkRecordBytes = 96U;
  static_assert(sizeof(Work) == kWorkRecordBytes);

  auto WorkRecordCount(const IblProcessSettings& settings) -> std::uint32_t
  {
    auto size = settings.face_size;
    const auto mips = static_cast<std::uint32_t>(std::bit_width(size));
    const auto tile_size = settings.dispatch_tile_size;
    auto count = (4U * mips) + 2U;
    if (tile_size == 0U) {
      return count;
    }
    for (auto mip = 0U; mip < mips; ++mip, size >>= 1U) {
      if (size <= tile_size) {
        continue;
      }
      const auto tiles_per_face = (size / tile_size) * (size / tile_size);
      // At mip zero: capture, normalize, SH, prefilter and both half chains.
      // Later mips: downsample, prefilter and both half chains.
      count
        += (mip == 0U ? kMipZeroWorkKinds : 4U) * kCubeFaces * tiles_per_face;
    }
    return count;
  }

  struct SkySnapshot {
    EnvironmentStaticData environment;
    EnvironmentViewData view;
    std::array<float, 3> origin {};
    float padding {};
  };
  constexpr auto kSkySnapshotBytes = 1008U;
  static_assert(sizeof(SkySnapshot) == kSkySnapshotBytes);

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

  auto MakeTexture(Graphics& graphics, const graphics::TextureDesc& desc)
    -> TextureAllocation
  {
    auto requests = std::vector<graphics::TextureViewRequest> {};
    requests.reserve(desc.mip_levels + 1U);
    requests.push_back({
      .description = { .view_type = ResourceViewType::kTexture_SRV,
        .format = desc.format,
        .dimension = desc.texture_type,
        .sub_resources = graphics::TextureSubResourceSet::EntireTexture(), },
      .domain = bindless::generated::kTexturesDomain,
    });
    for (auto mip = 0U; mip < desc.mip_levels; ++mip) {
      requests.push_back({
        .description = { .view_type = ResourceViewType::kTexture_UAV,
          .format = desc.format,
          .dimension = desc.texture_type == TextureType::kTextureCube
            ? TextureType::kTexture2DArray
            : desc.texture_type,
          .sub_resources = { .base_mip_level = mip,
            .num_mip_levels = 1U,
            .base_array_slice = 0U,
            .num_array_slices = desc.array_size, }, },
        .domain = std::nullopt,
      });
    }
    auto managed = graphics.GetResourceRegistry().RegisterManagedTexture(
      graphics.CreateTexture(desc), requests);
    if (!managed) {
      throw std::bad_alloc {};
    }
    auto result = TextureAllocation {
      .resource = std::move(managed->resource),
      .registration = std::move(managed->registration),
      .srv = managed->views.front().shader_visible_index,
      .uavs = {},
    };
    result.uavs.reserve(desc.mip_levels);
    for (auto mip = 0U; mip < desc.mip_levels; ++mip) {
      result.uavs.push_back(managed->views.at(mip + 1U).shader_visible_index);
    }
    return result;
  }

  auto MakeCube(Graphics& graphics, std::uint32_t size, std::uint32_t mips,
    Format format, const std::string& name) -> TextureAllocation
  {
    return MakeTexture(graphics,
      {
        .width = size,
        .height = size,
        .array_size = kCubeFaces,
        .mip_levels = mips,
        .format = format,
        .texture_type = TextureType::kTextureCube,
        .debug_name = name,
        .is_shader_resource = true,
        .is_uav = true,
        .initial_state = ResourceStates::kCommon,
      });
  }

  auto MakeBuffer(Graphics& graphics, std::uint32_t count, std::uint32_t stride,
    const std::string& name,
    graphics::BufferMemory memory = graphics::BufferMemory::kDeviceLocal)
    -> BufferAllocation
  {
    const auto upload = memory == graphics::BufferMemory::kUpload;
    const auto requests = std::array {
      graphics::BufferViewRequest {
        .description = { .view_type = ResourceViewType::kStructuredBuffer_SRV,
          .stride = stride, },
        .domain = bindless::generated::kGlobalSrvDomain,
      },
      graphics::BufferViewRequest {
        .description = { .view_type = ResourceViewType::kStructuredBuffer_UAV,
          .stride = stride, },
        .domain = std::nullopt,
      },
    };
    auto managed = graphics.GetResourceRegistry().RegisterManagedBuffer(
      graphics.CreateBuffer({
        .size_bytes = static_cast<std::uint64_t>(count) * stride,
        .usage = upload ? graphics::BufferUsage::kNone
                        : graphics::BufferUsage::kStorage,
        .memory = memory,
        .debug_name = name,
      }),
      std::span(requests).first(upload ? 1U : 2U));
    if (!managed) {
      throw std::bad_alloc {};
    }
    return {
      .resource = std::move(managed->resource),
      .registration = std::move(managed->registration),
      .srv = managed->views.front().shader_visible_index,
      .uav = upload ? kInvalidShaderVisibleIndex
                    : managed->views.at(1U).shader_visible_index,
    };
  }

  template <typename Resource>
  auto Track(graphics::CommandRecorder& recorder, const Resource& resource,
    ResourceStates initial = ResourceStates::kCommon) -> void
  {
    // Accepted batches retain their states for the next batch. Final product
    // transitions are explicit and timed before submission closes the list.
    if (!recorder.IsResourceTracked(resource)
      && !recorder.AdoptKnownResourceState(resource)) {
      recorder.BeginTrackingResourceState(resource, initial, false);
    }
  }

  struct ProductStorage {
    std::uint32_t size {};
    TextureAllocation scratch;
    TextureAllocation processed;
    TextureAllocation specular;
    TextureAllocation processed_half;
    TextureAllocation specular_half;
    BufferAllocation sh;
    BufferAllocation metadata;
    BufferAllocation partials;
    BufferAllocation constants;
    BufferAllocation sky_snapshot;
    TextureAllocation sky_lut;
    BufferAllocation distant_lut;
  };

  struct ProductPool {
    std::mutex mutex;
    std::shared_ptr<graphics::BackendLifetime> backend;
    nexus::IndexReuse<std::uint32_t> reuse;
    std::array<std::shared_ptr<ProductStorage>, IblGpuProcessor::kMaximumSlots>
      slots;
    std::array<std::uint32_t, IblGpuProcessor::kMaximumSlots> free {};
    std::uint32_t capacity {};
    std::uint32_t available {};
    std::uint32_t normal_capacity {};
    std::uint32_t normal_in_use {};
    std::uint32_t capture_capacity {};
    std::uint32_t captured_generations {};
    std::uint64_t storage_creations {};
    bool closed { false };
    bool failed { false };
  };

  auto BackendAvailable(const ProductPool& pool) -> bool
  {
    return pool.backend->State() == graphics::BackendLifecycle::kActive
      && !pool.backend->IsFaulted();
  }

  struct ProductVersion {
    std::shared_ptr<ProductPool> pool;
    std::shared_ptr<ProductStorage> storage;
    nexus::VersionedIndex<std::uint32_t> handle;
    nexus::RetirementState<std::uint32_t> retirement;

    // Caller holds pool.mutex. Completion callbacks neither allocate nor add
    // frame delays; admission and free-index publication remain pool policy.
    auto Finalize() noexcept -> void
    {
      if (retirement.BeginRetirement()) {
        auto ticket = pool->reuse.TryRetire(handle);
        assert(ticket.has_value());
        [[maybe_unused]] const auto installed = retirement.SetRetirement(
          ticket ? std::optional { std::move(*ticket) } : std::nullopt);
        assert(installed);
      }
      if (retirement.TakeOrdinaryDrained()) {
        assert(pool->normal_in_use > 0U);
        --pool->normal_in_use;
      }
      const auto result = retirement.Finalize();
      if (result && result->index && !pool->closed) {
        assert(pool->available < pool->capacity);
        *std::next(pool->free.begin(),
          static_cast<std::ptrdiff_t>(pool->available++)) = *result->index;
      }
    }
  };

  auto AttachVersion(graphics::CommandRecorder& recorder,
    const std::shared_ptr<ProductVersion>& version) -> void
  {
    recorder.RetainOpaqueUse(version, kProductUseTag, version.get(),
      {
        .prepare = [](void* pointer, graphics::QueueIdentity) -> void {
          auto& value = *static_cast<ProductVersion*>(pointer);
          std::scoped_lock lock(value.pool->mutex);
          if (value.pool->failed || !value.retirement.AcquireUse()) {
            throw std::logic_error("IBL generation is closed");
          }
        },
        .valid = [](const void* pointer) noexcept -> bool {
          const auto& value = *static_cast<const ProductVersion*>(pointer);
          std::scoped_lock lock(value.pool->mutex);
          return !value.retirement.IsResolved() && !value.pool->failed;
        },
        .submitted
        = [](void* pointer, graphics::QueueIdentity,
            const graphics::SubmissionResult& result) noexcept -> void {
          if (result.outcome
            != graphics::SubmissionOutcome::kExecutionUncertain) {
            return;
          }
          auto& value = *static_cast<ProductVersion*>(pointer);
          std::scoped_lock lock(value.pool->mutex);
          value.pool->failed = true;
          value.pool->closed = true;
          value.pool->reuse.Close();
        },
        .released = [](void* pointer, graphics::QueueIdentity,
                      graphics::SubmissionOutcome,
                      graphics::UseReleaseReason reason) noexcept -> void {
          auto& value = *static_cast<ProductVersion*>(pointer);
          std::scoped_lock lock(value.pool->mutex);
          if (!value.retirement.ReleaseUse()) {
            assert(false && "IBL use already released");
            return;
          }
          if (reason == graphics::UseReleaseReason::kDeviceLost) {
            value.pool->failed = true;
            value.pool->closed = true;
            value.pool->reuse.Close();
          }
          value.Finalize();
        },
      });
  }

} // namespace

struct IblGenerationOwner {
  explicit IblGenerationOwner(std::shared_ptr<ProductVersion> value)
    : version(std::move(value))
  {
  }
  ~IblGenerationOwner()
  {
    std::scoped_lock lock(version->pool->mutex);
    if (!version->retirement.IsActive()) {
      return;
    }
    if (!version->retirement.ReleaseOwner()) {
      assert(false && "IBL owner already released");
      return;
    }
    version->Finalize();
  }
  OXYGEN_MAKE_NON_COPYABLE(IblGenerationOwner)
  OXYGEN_MAKE_NON_MOVABLE(IblGenerationOwner)
  std::shared_ptr<ProductVersion> version;
};

// Recording batches retain internal pins, never external registration leases.
struct IblCapturePin {
  explicit IblCapturePin(std::shared_ptr<ProductVersion> captured_version)
    : version(std::move(captured_version))
  {
  }
  ~IblCapturePin()
  {
    if (!admitted) {
      return;
    }
    std::scoped_lock lock(version->pool->mutex);
    if (!version->retirement.ReleaseRetainedPin()) {
      assert(false && "IBL capture already released");
      return;
    }
    if (version->retirement.RetainedPinCount() == 0U) {
      assert(version->pool->captured_generations > 0U);
      --version->pool->captured_generations;
    }
    version->Finalize();
  }
  OXYGEN_MAKE_NON_COPYABLE(IblCapturePin)
  OXYGEN_MAKE_NON_MOVABLE(IblCapturePin)
  std::shared_ptr<ProductVersion> version;
  bool admitted { false };
};

struct IblCaptureLeaseState {
  IblGpuProducts products;
  std::shared_ptr<IblCapturePin> pin;
};

auto IblGpuProducts::AcquireCapture() const
  -> Result<IblCaptureLease, IblCaptureError>
try {
  if (!allocation || !producer.IsValid()) {
    return Err(IblCaptureError::kUnavailable);
  }
  const auto version = allocation->version;
  const auto operation = version->pool->backend->AcquireOperation();
  std::scoped_lock lock(version->pool->mutex);
  auto& pool = *version->pool;
  if (pool.closed || pool.failed || !BackendAvailable(pool)
    || !version->retirement.CanAcquireUse()) {
    return Err(IblCaptureError::kClosed);
  }
  if (version->retirement.RetainedPinCount() == 0U
    && pool.captured_generations >= pool.capture_capacity) {
    return Err(IblCaptureError::kBusy);
  }
  auto pin = std::make_shared<IblCapturePin>(version);
  auto state = std::make_shared<IblCaptureLeaseState>(*this, pin);
  // Capture ownership is independent of ordinary renderer ownership. Direct
  // aggregate construction copies products without a noexcept vector move.
  state->products.allocation.reset();
  const bool first_capture = version->retirement.RetainedPinCount() == 0U;
  if (!version->retirement.AcquireRetainedPin()) {
    return Err(IblCaptureError::kClosed);
  }
  if (first_capture) {
    ++pool.captured_generations;
  }
  pin->admitted = true;
  return Ok(IblCaptureLease(std::move(state)));
} catch (const std::bad_alloc&) {
  return Err(IblCaptureError::kAllocationFailed);
} catch (const std::exception&) {
  return Err(IblCaptureError::kClosed);
}

struct IblGpuJob {
  std::shared_ptr<ProductVersion> version;
  std::shared_ptr<IblGpuProducts> products;
  std::shared_ptr<const graphics::Texture> source;
  graphics::RegistrationLease source_registration;
  graphics::CompletionReceipt source_producer;
  std::shared_ptr<const void> resident_owner;
  std::optional<IblSkySource> sky;
  std::vector<IblGpuDispatch> dispatches;
  std::size_t next_dispatch {};
  graphics::CompletionReceipt previous_submission;
};

auto IblGpuDispatch::CanShareBatchWith(
  const IblGpuDispatch& other) const noexcept -> bool
{
  if (!shader || !other.shader || std::string_view(shader) != other.shader) {
    return false;
  }
  // Each source mip reads the preceding mip. Other spatial outputs are
  // independent tiles/mips; prefilter metadata updates are commutative atomics.
  return std::string_view(shader) != "IblMipCS"
    || output_size == other.output_size;
}

struct IblGpuProcessor::Impl {
  observer_ptr<Graphics> graphics;
  std::shared_ptr<ProductPool> pool;
  observer_ptr<DiagnosticsService> diagnostics { nullptr };
  std::shared_ptr<IblWorkBudget> budget;
  frame::SequenceNumber timing_frame;
};

IblGpuProcessor::IblGpuProcessor(
  Graphics& graphics, const std::uint32_t capacity)
  : impl_(std::make_unique<Impl>(
      make_observer(&graphics), std::make_shared<ProductPool>()))
{
  if (capacity == 0U || capacity > kMaximumSlots) {
    throw std::invalid_argument("IBL capacity exceeds its generation bound");
  }
  impl_->pool->capacity = capacity;
  impl_->pool->backend = graphics.GetBackendLifetime();
  impl_->pool->normal_capacity = std::min(capacity, kNormalSlots);
  impl_->pool->capture_capacity = capacity - impl_->pool->normal_capacity;
  impl_->pool->available = capacity;
  for (auto i = 0U; i < capacity; ++i) {
    impl_->pool->free.at(i) = capacity - i - 1U;
  }
}
IblGpuProcessor::~IblGpuProcessor() { Close(); }

IblGpuProcessor::IblGpuProcessor(
  Renderer& renderer, const std::uint32_t capacity)
  : IblGpuProcessor(*renderer.GetGraphics(), capacity)
{
  impl_->diagnostics = observer_ptr { &renderer.GetDiagnosticsService() };
}

auto IblGpuProcessor::Close() noexcept -> void
{
  std::scoped_lock lock(impl_->pool->mutex);
  impl_->pool->closed = true;
  impl_->pool->reuse.Close();
  impl_->pool->slots.fill(nullptr);
  impl_->pool->available = 0U;
}

auto IblGpuProcessor::IsOpen() const -> bool
{
  std::scoped_lock lock(impl_->pool->mutex);
  return !impl_->pool->closed && !impl_->pool->failed
    && BackendAvailable(*impl_->pool);
}

auto IblGpuProcessor::SetTimingContext(std::shared_ptr<IblWorkBudget> budget,
  const frame::SequenceNumber sequence) -> void
{
  impl_->budget = std::move(budget);
  impl_->timing_frame = sequence;
}

auto IblGpuProcessor::GetStats() const -> Stats
{
  std::scoped_lock lock(impl_->pool->mutex);
  auto result = Stats {
    .capacity = impl_->pool->capacity,
    .available = impl_->pool->available,
    .storage_creations = impl_->pool->storage_creations,
    .reuse = impl_->pool->reuse.GetTelemetrySnapshot(),
    .normal_capacity = impl_->pool->normal_capacity,
    .normal_in_use = impl_->pool->normal_in_use,
    .capture_capacity = impl_->pool->capture_capacity,
    .captured_generations = impl_->pool->captured_generations,
  };
  for (const auto& slot : impl_->pool->slots) {
    if (slot) {
      ++result.allocated;
    }
  }
  return result;
}

namespace {
  auto RetainProductRegistrations(const IblGpuProducts& p,
    graphics::CommandRecorder& recorder, graphics::ResourceRegistry& registry)
    -> bool
  {
    if (!p.producer.IsValid() || !p.brdf || !p.brdf->producer.IsValid()
      || !p.processed_cube || !p.specular_cube || !p.processed_half_cube
      || !p.specular_half_cube || !p.diffuse_sh || !p.metadata
      || recorder.GetTargetQueue()->GetQueueRole()
        != graphics::QueueRole::kGraphics) {
      return false;
    }
    for (const auto& registration : p.registrations) {
      if (!recorder.RetainRegistration(registry, registration)) {
        return false;
      }
    }
    return true;
  }

  auto BindProductResources(
    const IblGpuProducts& p, graphics::CommandRecorder& recorder) -> void
  {
    recorder.RecordDependency(p.producer);
    Track(recorder, *p.brdf->texture);
    recorder.RequireResourceState(
      *p.brdf->texture, ResourceStates::kShaderResource);
    Track(recorder, *p.processed_cube);
    Track(recorder, *p.specular_cube);
    Track(recorder, *p.processed_half_cube);
    Track(recorder, *p.specular_half_cube);
    Track(recorder, *p.diffuse_sh);
    Track(recorder, *p.metadata);
    recorder.RequireResourceState(
      *p.processed_cube, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *p.specular_cube, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *p.processed_half_cube, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *p.specular_half_cube, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *p.diffuse_sh, ResourceStates::kShaderResource);
    recorder.RequireResourceState(*p.metadata, ResourceStates::kShaderResource);
  }
} // namespace

auto IblGpuProducts::Attach(graphics::CommandRecorder& recorder,
  graphics::ResourceRegistry& registry) const -> bool
{
  if (!allocation || !RetainProductRegistrations(*this, recorder, registry)) {
    return false;
  }
  AttachVersion(recorder, allocation->version);
  BindProductResources(*this, recorder);
  return true;
}

static auto AttachCapture(const std::shared_ptr<IblCaptureLeaseState>& state,
  graphics::CommandRecorder& recorder, graphics::ResourceRegistry& registry)
  -> Result<void, IblCaptureError>
try {
  if (!state) {
    return Err(IblCaptureError::kClosed);
  }
  const auto& pin = state->pin;
  if (!BackendAvailable(*pin->version->pool)) {
    return Err(IblCaptureError::kClosed);
  }
  const auto operation = pin->version->pool->backend->AcquireOperation();
  {
    std::scoped_lock lock(pin->version->pool->mutex);
    if (pin->version->pool->failed || pin->version->retirement.IsResolved()) {
      return Err(IblCaptureError::kClosed);
    }
  }
  if (!RetainProductRegistrations(state->products, recorder, registry)) {
    return Err(IblCaptureError::kRecordingFailed);
  }
  BindProductResources(state->products, recorder);
  recorder.RetainOpaqueUse(pin, kCaptureUseTag, pin.get(),
    {
      .prepare = [](void* pointer, graphics::QueueIdentity) -> void {
        auto& value = *static_cast<IblCapturePin*>(pointer);
        std::scoped_lock lock(value.version->pool->mutex);
        if (!value.admitted || value.version->retirement.IsResolved()
          || value.version->pool->failed
          || !BackendAvailable(*value.version->pool)) {
          throw std::logic_error("IBL capture is closed");
        }
      },
      .valid = [](const void* pointer) noexcept -> bool {
        const auto& value = *static_cast<const IblCapturePin*>(pointer);
        std::scoped_lock lock(value.version->pool->mutex);
        return !value.version->retirement.IsResolved()
          && !value.version->pool->failed
          && BackendAvailable(*value.version->pool);
      },
      .submitted
      = [](void* pointer, graphics::QueueIdentity,
          const graphics::SubmissionResult& result) noexcept -> void {
        if (result.outcome
          != graphics::SubmissionOutcome::kExecutionUncertain) {
          return;
        }
        auto& pool = *static_cast<IblCapturePin*>(pointer)->version->pool;
        std::scoped_lock lock(pool.mutex);
        pool.failed = pool.closed = true;
        pool.reuse.Close();
      },
      .released
      = [](void* pointer, graphics::QueueIdentity, graphics::SubmissionOutcome,
          graphics::UseReleaseReason reason) noexcept -> void {
        if (reason != graphics::UseReleaseReason::kDeviceLost) {
          return;
        }
        auto& pool = *static_cast<IblCapturePin*>(pointer)->version->pool;
        std::scoped_lock lock(pool.mutex);
        pool.failed = pool.closed = true;
        pool.reuse.Close();
      },
    });
  return {};
} catch (const std::bad_alloc&) {
  return Err(IblCaptureError::kAllocationFailed);
} catch (const std::exception&) {
  return Err(IblCaptureError::kRecordingFailed);
}

auto IblGpuProcessor::Process(const std::shared_ptr<graphics::Texture>& source,
  const graphics::RegistrationLease& source_registration,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision,
  const graphics::CompletionReceipt source_producer)
  -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>
{
  return ProcessSource(source, source_registration, brdf, settings, revision,
    source_producer, nullptr);
}

auto IblGpuProcessor::ProcessSky(const IblSkySource& source,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision)
  -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>
{
  const auto& cube = source.sky_sphere_cube;
  return ProcessSource(cube.texture, {}, brdf, settings, revision,
    source.producer, &source, cube.srv, cube.owner);
}

auto IblGpuProcessor::ProcessCubeView(
  const std::shared_ptr<const graphics::Texture>& source,
  const ShaderVisibleIndex srv, std::shared_ptr<const void> owner,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision)
  -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>
{
  if (!srv.IsValid() || !owner) {
    return std::unexpected(IblProcessError::kInvalidSource);
  }
  return ProcessSource(
    source, {}, brdf, settings, revision, {}, nullptr, srv, std::move(owner));
}

auto IblGpuProcessor::ProcessSource(
  const std::shared_ptr<const graphics::Texture>& source,
  const graphics::RegistrationLease& source_registration,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision,
  const graphics::CompletionReceipt source_producer, const IblSkySource* sky,
  const ShaderVisibleIndex resident_srv,
  std::shared_ptr<const void> resident_owner)
  -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>
{
  auto job = PrepareSource(source, source_registration, brdf, settings,
    revision, source_producer, sky, resident_srv, std::move(resident_owner));
  if (!job) {
    return std::unexpected(job.error());
  }
  const auto completed = Advance(*job, UINT32_MAX);
  if (!completed) {
    return std::unexpected(completed.error());
  }
  return completed->products;
}

auto IblGpuProcessor::Begin(const std::shared_ptr<graphics::Texture>& source,
  const graphics::RegistrationLease& source_registration,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision,
  const graphics::CompletionReceipt source_producer)
  -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>
{
  auto job = PrepareSource(source, source_registration, brdf, settings,
    revision, source_producer, nullptr);
  if (!job) {
    return std::unexpected(job.error());
  }
  const auto initialized = Advance(*job, 1U);
  if (!initialized) {
    return std::unexpected(initialized.error());
  }
  return job;
}

auto IblGpuProcessor::BeginSky(const IblSkySource& source,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision)
  -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>
{
  const auto& cube = source.sky_sphere_cube;
  auto job = PrepareSource(cube.texture, {}, brdf, settings, revision,
    source.producer, &source, cube.srv, cube.owner);
  if (!job) {
    return std::unexpected(job.error());
  }
  const auto initialized = Advance(*job, 1U);
  if (!initialized) {
    return std::unexpected(initialized.error());
  }
  return job;
}

auto IblGpuProcessor::BeginCubeView(
  const std::shared_ptr<const graphics::Texture>& source,
  const ShaderVisibleIndex srv, std::shared_ptr<const void> owner,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision)
  -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>
{
  if (!srv.IsValid() || !owner) {
    return std::unexpected(IblProcessError::kInvalidSource);
  }
  auto job = PrepareSource(
    source, {}, brdf, settings, revision, {}, nullptr, srv, std::move(owner));
  if (!job) {
    return std::unexpected(job.error());
  }
  const auto initialized = Advance(*job, 1U);
  if (!initialized) {
    return std::unexpected(initialized.error());
  }
  return job;
}

auto IblGpuProcessor::PendingDispatches(const IblGpuJob& job) const
  -> std::span<const IblGpuDispatch>
{
  if (job.version->pool != impl_->pool) {
    return {};
  }
  return std::span(job.dispatches).subspan(job.next_dispatch);
}

auto IblGpuProcessor::Advance(
  const std::shared_ptr<IblGpuJob>& job, const std::uint32_t dispatch_limit)
  -> std::expected<IblGpuAdvance, IblProcessError>
try {
  if (!job || job->version->pool != impl_->pool) {
    return std::unexpected(IblProcessError::kInvalidSource);
  }
  {
    std::scoped_lock lock(impl_->pool->mutex);
    if (impl_->pool->closed || impl_->pool->failed
      || !BackendAvailable(*impl_->pool)) {
      return std::unexpected(IblProcessError::kClosed);
    }
  }
  const auto remaining
    = static_cast<std::uint32_t>(PendingDispatches(*job).size());
  if (remaining == 0U) {
    return IblGpuAdvance { .products = job->products };
  }
  if (dispatch_limit == 0U) {
    return IblGpuAdvance { .remaining_dispatches = remaining, .products = {} };
  }
  const auto operation = impl_->pool->backend->AcquireOperation();
  const profiling::CpuProfileScope cpu_scope(
    "Vortex.Environment.IBL.Advance", profiling::ProfileCategory::kCompute);
  auto& graphics = *impl_->graphics;
  auto& registry = graphics.GetResourceRegistry();
  auto& storage = *job->version->storage;
  const auto count = std::min(remaining, dispatch_limit);
  bool accepted = false;
  if (impl_->budget) {
    impl_->budget->Record(
      impl_->timing_frame, PendingDispatches(*job).first(count));
  }
  const ScopeGuard feedback([&] noexcept -> void {
    if (!accepted) {
      if (impl_->budget) {
        impl_->budget->Reject(impl_->timing_frame);
      }
      if (impl_->diagnostics) {
        impl_->diagnostics->InvalidateIblTiming();
      }
    }
  });
  auto recording = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex.Environment.IBL", graphics::SubmissionPolicy::kExplicit);
  if (!recording) {
    return std::unexpected(IblProcessError::kRecordingFailed);
  }
  auto& recorder = *recording;
  if (impl_->diagnostics) {
    impl_->diagnostics->AttachIblTimelineCollector(recorder);
  }
  AttachVersion(recorder, job->version);
  {
    const graphics::GpuEventScope total_scope(recorder,
      "Vortex.Environment.IBL.Process",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kCompute);
    recorder.RecordDependency(job->products->brdf->producer);
    if (job->previous_submission.IsValid()) {
      recorder.RecordDependency(job->previous_submission);
    }
    if (job->source_producer.IsValid()) {
      recorder.RecordDependency(job->source_producer);
    }
    if (!recorder.RetainRegistration(
          registry, job->products->brdf->registration)) {
      return std::unexpected(IblProcessError::kBrdfUnavailable);
    }
    for (const auto& registration : {
           storage.scratch.registration,
           storage.processed.registration,
           storage.specular.registration,
           storage.processed_half.registration,
           storage.specular_half.registration,
           storage.sh.registration,
           storage.metadata.registration,
           storage.partials.registration,
           storage.constants.registration,
         }) {
      if (!recorder.RetainRegistration(registry, registration)) {
        return std::unexpected(IblProcessError::kRecordingFailed);
      }
    }
    if (job->source) {
      if (job->source_registration
        && !recorder.RetainRegistration(registry, job->source_registration)) {
        return std::unexpected(IblProcessError::kRecordingFailed);
      }
      if (job->resident_owner) {
        recorder.RetainOpaqueUse(job->resident_owner, kSourceUseTag);
      }
      Track(recorder, *job->source);
      recorder.RequireResourceState(
        *job->source, ResourceStates::kShaderResource);
    }
    if (job->sky) {
      if (!recorder.RetainRegistration(
            registry, storage.sky_snapshot.registration)) {
        return std::unexpected(IblProcessError::kRecordingFailed);
      }
      Track(
        recorder, *storage.sky_snapshot.resource, ResourceStates::kGenericRead);
    }
    if (job->sky && job->sky->environment.atmosphere.enabled != 0U) {
      if (!recorder.RetainRegistration(registry, storage.sky_lut.registration)
        || !recorder.RetainRegistration(
          registry, storage.distant_lut.registration)) {
        return std::unexpected(IblProcessError::kRecordingFailed);
      }
      Track(recorder, *storage.sky_lut.resource);
      Track(recorder, *storage.distant_lut.resource);
      if (job->next_dispatch == 0U) {
        constexpr auto kSkyCopyUse = 0x49424c534f555243ULL;
        recorder.RetainOpaqueUse(job->sky->sky_view, kSkyCopyUse);
        recorder.RetainOpaqueUse(job->sky->distant_sky, kSkyCopyUse);
        Track(recorder, *job->sky->sky_view);
        Track(recorder, *job->sky->distant_sky);
        recorder.RequireResourceState(
          *job->sky->sky_view, ResourceStates::kCopySource);
        recorder.RequireResourceState(
          *job->sky->distant_sky, ResourceStates::kCopySource);
        recorder.RequireResourceState(
          *storage.sky_lut.resource, ResourceStates::kCopyDest);
        recorder.RequireResourceState(
          *storage.distant_lut.resource, ResourceStates::kCopyDest);
        recorder.FlushBarriers();
        recorder.CopyTexture(
          *job->sky->sky_view, {}, {}, *storage.sky_lut.resource, {}, {});
        recorder.CopyBuffer(
          *storage.distant_lut.resource, 0U, *job->sky->distant_sky, 0U, 16U);
        recorder.RequireResourceStateFinal(
          *job->sky->sky_view, ResourceStates::kShaderResource);
        recorder.RequireResourceStateFinal(
          *job->sky->distant_sky, ResourceStates::kShaderResource);
      }
      recorder.RequireResourceState(
        *storage.sky_lut.resource, ResourceStates::kShaderResource);
      recorder.RequireResourceState(
        *storage.distant_lut.resource, ResourceStates::kShaderResource);
    }
    const auto textures = std::array {
      storage.scratch.resource,
      storage.processed.resource,
      storage.specular.resource,
      storage.processed_half.resource,
      storage.specular_half.resource,
    };
    const auto buffers = std::array {
      storage.sh.resource,
      storage.metadata.resource,
      storage.partials.resource,
    };
    for (const auto& texture : textures) {
      Track(recorder, *texture);
      recorder.EnableAutoMemoryBarriers(*texture);
    }
    for (const auto& buffer : buffers) {
      Track(recorder, *buffer);
      recorder.EnableAutoMemoryBarriers(*buffer);
    }
    Track(recorder, *storage.constants.resource, ResourceStates::kGenericRead);
    const auto bindings = vortex::internal::BuildVortexRootBindings();
    auto steps = PendingDispatches(*job).first(count);
    while (!steps.empty()) {
      const auto& first = steps.front();
      const auto end = std::ranges::find_if(std::next(steps.begin()),
        steps.end(), [&](const IblGpuDispatch& step) -> bool {
          return !first.CanShareBatchWith(step);
        });
      const auto batch_size = static_cast<std::size_t>(end - steps.begin());
      const auto batch = steps.first(batch_size);
      const auto shader = std::string_view(first.shader);
      const graphics::GpuEventScope phase_scope(recorder,
        IblWorkBudget::TimingLabel(first.shader),
        impl_->budget ? profiling::ProfileGranularity::kTelemetry
                      : profiling::ProfileGranularity::kDiagnostic,
        profiling::ProfileCategory::kCompute);
      const auto uav = [&](const auto& resource) -> auto {
        recorder.RequireResourceState(
          *resource, ResourceStates::kUnorderedAccess);
      };
      const bool prepare
        = shader == "IblPrepareCS" || shader == "IblCapturePrepareCS";
      const bool narrow = shader == "IblNarrowCS";
      const bool precision = shader == "IblPrecisionRangeCS";
      if (prepare || shader == "IblNormalizeCS") {
        uav(storage.scratch.resource);
      }
      if (shader == "IblNormalizeCS" || shader == "IblMipCS"
        || shader == "IblShCS" || narrow || precision) {
        uav(storage.processed.resource);
      }
      if (shader == "IblPrefilterCS") {
        recorder.RequireResourceState(
          *storage.processed.resource, ResourceStates::kShaderResource);
      }
      if (shader == "IblPrefilterCS" || narrow || precision) {
        uav(storage.specular.resource);
      }
      if (narrow || precision) {
        uav(storage.processed_half.resource);
        uav(storage.specular_half.resource);
      }
      if (shader == "IblShReduceCS") {
        uav(storage.sh.resource);
      }
      if (shader == "IblInitializeCS" || shader == "IblRangeCS"
        || shader == "IblNormalizeCS" || shader == "IblShReduceCS"
        || shader == "IblPrefilterCS" || precision
        || shader == "IblCompleteCS") {
        uav(storage.metadata.resource);
      }
      if (prepare || shader == "IblRangeCS" || shader == "IblShCS"
        || shader == "IblShReduceCS" || precision
        || shader == "IblPrecisionReduceCS" || shader == "IblCompleteCS") {
        uav(storage.partials.resource);
      }
      const auto pipeline
        = graphics::ComputePipelineDesc::Builder {}
            .SetComputeShader({
              .stage = ShaderType::kCompute,
              .source_path = "Vortex/Services/Environment/IblProcessing.hlsl",
              .entry_point = first.shader,
            })
            .SetRootBindings(bindings)
            .SetDebugName(first.shader)
            .Build();
      recorder.FlushBarriers();
      recorder.SetPipelineState(pipeline);
      recorder.SetComputeRoot32BitConstant(
        static_cast<std::uint32_t>(root::RootParam::kRootConstants),
        storage.constants.srv.get(), 1U);
      for (const auto& step : batch) {
        const graphics::GpuEventScope dispatch_scope(recorder, step.shader,
          profiling::ProfileGranularity::kDiagnostic,
          profiling::ProfileCategory::kCompute);
        recorder.SetComputeRoot32BitConstant(
          static_cast<std::uint32_t>(root::RootParam::kRootConstants),
          step.work_index, 0U);
        recorder.Dispatch(
          step.groups.at(0), step.groups.at(1), step.groups.at(2));
      }
      steps = steps.subspan(batch_size);
    }
    if (count == remaining) {
      for (const auto& texture : {
             storage.processed.resource,
             storage.specular.resource,
             storage.processed_half.resource,
             storage.specular_half.resource,
           }) {
        recorder.RequireResourceStateFinal(
          *texture, ResourceStates::kShaderResource);
      }
      for (const auto& buffer :
        { storage.sh.resource, storage.metadata.resource }) {
        recorder.RequireResourceStateFinal(
          *buffer, ResourceStates::kShaderResource);
      }
      recorder.FlushBarriers();
    }
  }
  const auto submitted = recording.SubmitWithReceipt();
  if (submitted.outcome != graphics::SubmissionOutcome::kSubmitted
    || !submitted.receipt) {
    return std::unexpected(IblProcessError::kSubmissionFailed);
  }
  job->previous_submission = *submitted.receipt;
  accepted = true;
  job->next_dispatch += count;
  const bool complete = count == remaining;
  if (complete) {
    job->products->producer = *submitted.receipt;
  }
  return IblGpuAdvance {
    .recorded_dispatches = count,
    .remaining_dispatches = remaining - count,
    .products = complete ? job->products : nullptr,
  };
} catch (const std::bad_alloc&) {
  return std::unexpected(IblProcessError::kAllocationFailed);
} catch (const std::exception&) {
  return std::unexpected(IblProcessError::kRecordingFailed);
}

auto IblGpuProcessor::PrepareSource(
  const std::shared_ptr<const graphics::Texture>& source,
  const graphics::RegistrationLease& source_registration,
  const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision,
  const graphics::CompletionReceipt source_producer, const IblSkySource* sky,
  const ShaderVisibleIndex resident_srv,
  std::shared_ptr<const void> resident_owner)
  -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>
{
  const profiling::CpuProfileScope cpu_scope(
    "Vortex.Environment.IBL.Process", profiling::ProfileCategory::kCompute);
  auto& graphics = *impl_->graphics;
  auto& registry = graphics.GetResourceRegistry();
  if ((!sky && (!source || (!source_registration && !resident_owner)))
    || revision == 0U || !std::has_single_bit(settings.face_size)
    || (settings.dispatch_tile_size != 0U
      && (settings.dispatch_tile_size < 8U
        || !std::has_single_bit(settings.dispatch_tile_size)))
    || !std::isfinite(settings.source_rotation_radians)
    || !std::isfinite(settings.source_scale) || settings.source_scale < 0.0F
    || !std::isfinite(settings.lower_hemisphere_blend_alpha)
    || settings.lower_hemisphere_blend_alpha < 0.0F
    || settings.lower_hemisphere_blend_alpha > 1.0F
    || std::ranges::any_of(
      settings.lower_hemisphere_color, [](float value) -> bool {
        return !std::isfinite(value) || value < 0.0F;
      })) {
    return std::unexpected(IblProcessError::kInvalidSource);
  }
  if (!brdf || !brdf->texture || !brdf->registration || !brdf->srv.IsValid()
    || !brdf->producer.IsValid()
    || registry.InspectManagedIdentity(*brdf->texture)
      != brdf->registration.Identity()) {
    return std::unexpected(IblProcessError::kBrdfUnavailable);
  }
  if (source) {
    const auto identity = registry.InspectManagedIdentity(*source);
    const auto& desc = source->GetDescriptor();
    if ((source_registration
          && (!identity || *identity != source_registration.Identity()))
      || desc.texture_type != TextureType::kTextureCube
      || desc.array_size != kCubeFaces || desc.width != desc.height
      || desc.width == 0U || settings.face_size > desc.width
      || !desc.is_shader_resource) {
      return std::unexpected(IblProcessError::kInvalidSource);
    }
  }
  const bool atmosphere
    = sky != nullptr && sky->environment.atmosphere.enabled != 0U;
  constexpr auto capture_fog_flags = kGpuFogFlagEnabled
    | kGpuFogFlagHeightFogEnabled | kGpuFogFlagVisibleInRealTimeSkyCaptures;
  const bool capture_fog = sky != nullptr
    && (sky->environment.fog.flags & capture_fog_flags) == capture_fog_flags
    && (sky->environment.fog.primary_density > 0.0F
      || sky->environment.fog.secondary_density > 0.0F)
    && sky->environment.fog.min_transmittance < 1.0F;
  if (sky
    && (settings.source_rotation_radians != 0.0F
      || std::ranges::any_of(sky->origin,
        [](float value) -> bool { return !std::isfinite(value); }))) {
    return std::unexpected(IblProcessError::kInvalidSource);
  }
  // A captured Sky Sphere replaces the atmosphere; a cubemap one is the cube
  // source of this sky capture, and nothing else is.
  const auto& sphere
    = sky != nullptr ? sky->environment.sky_sphere : GpuSkySphereParams {};
  const bool sky_sphere = sphere.enabled != 0U;
  const bool sky_sphere_cube
    = sky_sphere && sphere.source == kSkySphereSourceCubemap;
  if (sky
    && ((sky_sphere && atmosphere) || sky_sphere_cube != (source != nullptr)
      || (sky_sphere_cube && (!resident_srv.IsValid() || !resident_owner))
      || (sky_sphere
        && (!std::isfinite(sphere.rotation_radians)
          || !std::isfinite(sphere.intensity) || sphere.intensity < 0.0F
          || std::ranges::any_of(
            sphere.solid_color_rgb,
            [](float value) -> bool {
              return !std::isfinite(value) || value < 0.0F;
            }))))) {
    return std::unexpected(IblProcessError::kInvalidSource);
  }
  if (atmosphere
    && (!sky->sky_view || !sky->distant_sky
      || sky->sky_view->GetDescriptor().texture_type != TextureType::kTexture2D
      || sky->sky_view->GetDescriptor().format != Format::kRGBA32Float
      || sky->sky_view->GetDescriptor().width == 0U
      || sky->sky_view->GetDescriptor().height == 0U
      || sky->sky_view->GetDescriptor().array_size != 1U
      || sky->sky_view->GetDescriptor().mip_levels != 1U
      || !sky->sky_view->GetDescriptor().is_shader_resource
      || sky->distant_sky->GetDescriptor().size_bytes != 16U)) {
    return std::unexpected(IblProcessError::kInvalidSource);
  }

  try {
    graphics.PollCompletedUses();
    registry.PollManagedRetirements();
    auto version = std::make_shared<ProductVersion>();
    version->pool = impl_->pool;
    auto allocation = std::make_shared<IblGenerationOwner>(version);
    {
      std::scoped_lock lock(version->pool->mutex);
      if (version->pool->closed) {
        return std::unexpected(IblProcessError::kClosed);
      }
      if (version->pool->available == 0U
        || version->pool->normal_in_use >= version->pool->normal_capacity) {
        return std::unexpected(IblProcessError::kPoolBusy);
      }
      const auto index = version->pool->free.at(--version->pool->available);
      try {
        version->handle = version->pool->reuse.ActivateSlot(index);
      } catch (...) {
        version->pool->free.at(version->pool->available++) = index;
        throw;
      }
      [[maybe_unused]] const auto activated = version->retirement.Activate(1U);
      assert(activated);
      ++version->pool->normal_in_use;
      version->storage = version->pool->slots.at(index);
    }
    auto source_srv = resident_srv;
    if (source && !source_srv.IsValid()) {
      const auto source_view
        = registry.AcquireManagedView<graphics::Texture>(source_registration,
          {
            .view_type = ResourceViewType::kTexture_SRV,
            .format = source->GetDescriptor().format,
            .dimension = TextureType::kTextureCube,
            .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
          },
          bindless::generated::kTexturesDomain);
      if (!source_view) {
        return std::unexpected(IblProcessError::kAllocationFailed);
      }
      source_srv = source_view->shader_visible_index;
    }
    const auto size = settings.face_size;
    const auto mips = static_cast<std::uint32_t>(std::bit_width(size));
    const auto work_records = WorkRecordCount(settings);
    const auto tiles = ((size + (kGroupEdge - 1U)) / 8U)
      * ((size + (kGroupEdge - 1U)) / 8U) * kCubeFaces;
    auto precision_tiles = 0U;
    for (auto mip = 0U; mip < mips; ++mip) {
      const auto groups = ((size >> mip) + (kGroupEdge - 1U)) / 8U;
      precision_tiles += 2U * groups * groups * kCubeFaces;
    }
    if (!version->storage || version->storage->size != size) {
      {
        std::scoped_lock lock(version->pool->mutex);
        version->pool->slots.at(version->handle.index).reset();
      }
      version->storage.reset();
      registry.PollManagedRetirements();
      auto storage = std::make_shared<ProductStorage>();
      storage->size = size;
      storage->scratch = MakeCube(
        graphics, size, 1U, Format::kRGBA32Float, "IBL.SourceScratch");
      storage->processed = MakeCube(
        graphics, size, mips, Format::kRGBA32Float, "IBL.ProcessedCube");
      storage->specular = MakeCube(
        graphics, size, mips, Format::kRGBA32Float, "IBL.SpecularCube");
      storage->processed_half = MakeCube(
        graphics, size, mips, Format::kRGBA16Float, "IBL.ProcessedHalfCube");
      storage->specular_half = MakeCube(
        graphics, size, mips, Format::kRGBA16Float, "IBL.SpecularHalfCube");
      storage->sh = MakeBuffer(graphics, 8U, 16U, "IBL.DiffuseSH");
      storage->metadata
        = MakeBuffer(graphics, 1U, sizeof(IblProductMetadata), "IBL.Metadata");
      storage->partials = MakeBuffer(graphics,
        std::max((tiles * kShPartialElements) + 1U, precision_tiles), 16U,
        "IBL.ReductionScratch");
      storage->constants = MakeBuffer(graphics, work_records, sizeof(Work),
        "IBL.Work", graphics::BufferMemory::kUpload);
      version->storage = storage;
      std::scoped_lock lock(version->pool->mutex);
      version->pool->slots.at(version->handle.index) = std::move(storage);
      ++version->pool->storage_creations;
    }
    auto& scratch = version->storage->scratch;
    auto& processed = version->storage->processed;
    auto& specular = version->storage->specular;
    auto& processed_half = version->storage->processed_half;
    auto& specular_half = version->storage->specular_half;
    auto& sh = version->storage->sh;
    auto& metadata = version->storage->metadata;
    auto& partials = version->storage->partials;
    auto& constants = version->storage->constants;
    if (!constants.resource
      || constants.resource->GetDescriptor().size_bytes
        < work_records * sizeof(Work)) {
      constants = {};
      registry.PollManagedRetirements();
      constants = MakeBuffer(graphics, work_records, sizeof(Work), "IBL.Work",
        graphics::BufferMemory::kUpload);
    }
    auto& snapshot = version->storage->sky_snapshot;
    if (sky) {
      if (!snapshot.resource) {
        snapshot = MakeBuffer(graphics, 1U, sizeof(SkySnapshot),
          "IBL.SkySnapshot", graphics::BufferMemory::kUpload);
      }
      auto data = SkySnapshot {
        .environment = sky->environment,
        .view = sky->view,
        .origin = sky->origin,
      };
      data.environment.sky_sphere.cubemap_slot
        = sky_sphere_cube ? source_srv.get() : kInvalidBindlessIndex;
      data.environment.atmosphere.sky_view_lut_slot = kInvalidBindlessIndex;
      data.environment.atmosphere.distant_sky_light_lut_slot
        = kInvalidBindlessIndex;
      if (atmosphere) {
        auto& sky_lut = version->storage->sky_lut;
        auto& distant_lut = version->storage->distant_lut;
        const auto& desc = sky->sky_view->GetDescriptor();
        if (!sky_lut.resource
          || sky_lut.resource->GetDescriptor().width != desc.width
          || sky_lut.resource->GetDescriptor().height != desc.height) {
          sky_lut = MakeTexture(graphics,
            {
              .width = desc.width,
              .height = desc.height,
              .format = Format::kRGBA32Float,
              .texture_type = TextureType::kTexture2D,
              .debug_name = "IBL.FrozenSkyView",
              .is_shader_resource = true,
              .is_uav = true,
              .initial_state = ResourceStates::kCommon,
            });
        }
        if (!distant_lut.resource) {
          distant_lut = MakeBuffer(graphics, 1U, 16U, "IBL.FrozenDistantSky");
        }
        data.environment.atmosphere.sky_view_lut_slot = sky_lut.srv.get();
        data.environment.atmosphere.distant_sky_light_lut_slot
          = distant_lut.srv.get();
        data.environment.atmosphere.sky_view_lut_width
          = static_cast<float>(desc.width);
        data.environment.atmosphere.sky_view_lut_height
          = static_cast<float>(desc.height);
      }
      snapshot.resource->Update(&data, sizeof(data), 0U);
    }
    auto work = std::vector<Work> {};
    auto base = Work { .source_srv = source_srv.get(),
      .output_uav = scratch.uavs.at(0).get(),
      .metadata_uav = metadata.uav.get(),
      .partials_uav = partials.uav.get(),
      .input_uav = scratch.uavs.at(0).get(),
      .source_size = size,
      .output_size = size,
      .maximum_mip = mips - 1U,
      .partial_count = tiles,
      .revision = revision,
      .hemisphere_enabled = settings.lower_hemisphere_solid_color
          && (!sky || atmosphere || capture_fog || sky_sphere)
        ? 1U
        : 0U,
      .lower_hemisphere = { settings.lower_hemisphere_color.at(0),
        settings.lower_hemisphere_color.at(1), settings.lower_hemisphere_color.at(2),
        settings.lower_hemisphere_blend_alpha, },
      .source_rotation = settings.source_rotation_radians,
      .capture_srv = sky ? snapshot.srv.get() : kInvalidBindlessIndex,
      .processed_half_srv = processed_half.srv.get(),
      .specular_half_srv = specular_half.srv.get(),
      .source_scale = settings.source_scale, };
    work.push_back(base); // Prepare and range use record zero.
    base.output_uav = processed.uavs.at(0).get();
    work.push_back(base); // Normalize.
    for (auto mip = 1U; mip < mips; ++mip) {
      base.input_uav = processed.uavs.at(mip - 1U).get();
      base.output_uav = processed.uavs.at(mip).get();
      base.output_size = size >> mip;
      work.push_back(base);
    }
    base.input_uav = processed.uavs.at(0).get();
    base.output_uav = sh.uav.get();
    const auto sh_work = static_cast<std::uint32_t>(work.size());
    work.push_back(base);
    const auto specular_work = static_cast<std::uint32_t>(work.size());
    for (auto mip = 0U; mip < mips; ++mip) {
      base.source_srv = processed.srv.get();
      base.output_uav = specular.uavs.at(mip).get();
      base.output_size = size >> mip;
      base.output_mip = mip;
      work.push_back(base);
    }
    const auto half_work = static_cast<std::uint32_t>(work.size());
    for (auto mip = 0U; mip < mips; ++mip) {
      base.output_size = size >> mip;
      base.output_mip = mip;
      base.input_uav = processed.uavs.at(mip).get();
      base.output_uav = processed_half.uavs.at(mip).get();
      work.push_back(base);
      base.input_uav = specular.uavs.at(mip).get();
      base.output_uav = specular_half.uavs.at(mip).get();
      work.push_back(base);
    }
    auto result = std::make_shared<IblGpuProducts>();
    result->allocation = allocation;
    result->slot = version->handle;
    result->brdf = brdf;
    result->processed_cube = processed.resource;
    result->specular_cube = specular.resource;
    result->processed_half_cube = processed_half.resource;
    result->specular_half_cube = specular_half.resource;
    result->diffuse_sh = sh.resource;
    result->metadata = metadata.resource;
    result->processed_srv = processed.srv;
    result->specular_srv = specular.srv;
    result->diffuse_sh_srv = sh.srv;
    result->metadata_srv = metadata.srv;
    result->maximum_mip = mips - 1U;
    result->revision = revision;
    for (const auto& owner : {
           processed.registration,
           specular.registration,
           processed_half.registration,
           specular_half.registration,
           sh.registration,
           metadata.registration,
         }) {
      auto lease = registry.AcquireManaged(owner.Identity());
      if (!lease) {
        return std::unexpected(IblProcessError::kAllocationFailed);
      }
      result->registrations.push_back(std::move(*lease));
    }
    result->registrations.push_back(brdf->registration);

    auto job = std::make_shared<IblGpuJob>();
    job->version = version;
    job->products = result;
    job->source = source;
    job->source_registration = source_registration;
    job->source_producer = source_producer;
    job->resident_owner = std::move(resident_owner);
    if (sky) {
      job->sky = *sky;
    }
    struct DispatchShape {
      std::uint32_t groups { 1U };
      std::uint32_t faces { 1U };
      std::uint32_t rows { 0U };
    };
    const auto dispatch_raw = [&](const char* entry, std::uint32_t index,
                                DispatchShape shape) -> void {
      const auto [groups, faces, rows] = shape;
      auto y = rows;
      if (y == 0U) {
        y = faces == kCubeFaces ? groups : 1U;
      }
      const auto shader = std::string_view(entry);
      const auto& record = work.at(index);
      const auto edge
        = shader == "IblShCS" ? record.source_size : record.output_size;
      std::uint64_t units = 1U;
      if (shader == "IblRangeCS") {
        units = record.partial_count;
      } else if (shader == "IblShReduceCS") {
        units = static_cast<std::uint64_t>(record.partial_count)
          * kShPartialElements;
      } else if (shader == "IblPrecisionRangeCS") {
        units = static_cast<std::uint64_t>(precision_tiles) * 64U;
      } else if (shader == "IblPrecisionReduceCS") {
        units = precision_tiles;
      } else if (shader != "IblInitializeCS" && shader != "IblCompleteCS") {
        units = static_cast<std::uint64_t>(std::min(
                  groups * 8U, edge - (record.group_origin.at(0) * 8U)))
          * std::min(y * 8U, edge - (record.group_origin.at(1) * 8U)) * faces;
        if (shader == "IblPrefilterCS") {
          const auto roughness = std::exp2(
            (static_cast<double>(record.output_mip) + 2.0 - record.maximum_mip)
            / kRoughnessMipScale);
          auto sample_count = 64U;
          if (roughness < kMirrorRoughness) {
            sample_count = 1U;
          } else if (roughness < kSmoothRoughness) {
            sample_count = 32U;
          }
          units *= sample_count;
        }
      }
      job->dispatches.push_back({
        .shader = entry,
        .work_index = index,
        .groups = { groups, y, faces },
        .output_size = edge,
        .work_units = units,
      });
    };
    const auto dispatch = [&](const char* entry, std::uint32_t index,
                            DispatchShape shape) -> void {
      const auto [groups, faces, rows] = shape;
      const auto tile_groups = settings.dispatch_tile_size / 8U;
      if (tile_groups == 0U || faces != kCubeFaces || rows != 0U
        || groups <= tile_groups) {
        dispatch_raw(entry, index, shape);
        return;
      }
      const auto canonical = work.at(index);
      for (auto face = 0U; face < faces; ++face) {
        for (auto y = 0U; y < groups; y += tile_groups) {
          for (auto x = 0U; x < groups; x += tile_groups) {
            auto tile = canonical;
            tile.group_origin = { x, y, face };
            const auto tile_index = static_cast<std::uint32_t>(work.size());
            work.push_back(tile);
            dispatch_raw(entry, tile_index,
              {
                .groups = std::min(tile_groups, groups - x),
                .faces = 1U,
                .rows = std::min(tile_groups, groups - y),
              });
          }
        }
      }
    };
    const auto cube_shape = [](std::uint32_t edge) -> DispatchShape {
      return {
        .groups = (edge + kGroupEdge - 1U) / kGroupEdge,
        .faces = kCubeFaces,
      };
    };
    dispatch("IblInitializeCS", 0U, {});
    dispatch(
      sky ? "IblCapturePrepareCS" : "IblPrepareCS", 0U, cube_shape(size));
    dispatch("IblRangeCS", 0U, {});
    dispatch("IblNormalizeCS", 1U, cube_shape(size));
    for (auto mip = 1U; mip < mips; ++mip) {
      dispatch("IblMipCS", mip + 1U, cube_shape(size >> mip));
    }
    dispatch("IblShCS", sh_work, cube_shape(size));
    dispatch("IblShReduceCS", sh_work, {});
    for (auto mip = 0U; mip < mips; ++mip) {
      dispatch("IblPrefilterCS", specular_work + mip, cube_shape(size >> mip));
    }
    for (auto mip = 0U; mip < mips; ++mip) {
      dispatch("IblNarrowCS", half_work + (2U * mip), cube_shape(size >> mip));
      dispatch(
        "IblNarrowCS", half_work + (2U * mip) + 1U, cube_shape(size >> mip));
    }
    // Scan both complete chains into disjoint partials, then reduce once.
    // Rows keep larger specified cubes within D3D12's dispatch limit.
    constexpr auto kMaximumDispatchGroups = 65535U;
    dispatch("IblPrecisionRangeCS", half_work,
      {
        .groups = std::min(precision_tiles, kMaximumDispatchGroups),
        .faces = 1U,
        .rows = (precision_tiles + kMaximumDispatchGroups - 1U)
          / kMaximumDispatchGroups,
      });
    dispatch("IblPrecisionReduceCS", 0U, {});
    dispatch("IblCompleteCS", 0U, {});
    assert(work.size() == work_records);
    constants.resource->Update(work.data(), work.size() * sizeof(Work), 0U);
    return job;
  } catch (const std::bad_alloc&) {
    return std::unexpected(IblProcessError::kAllocationFailed);
  } catch (const std::exception&) {
    return std::unexpected(IblProcessError::kRecordingFailed);
  }
}

} // namespace oxygen::vortex::environment::internal

namespace oxygen::vortex::environment {

IblCaptureLease::IblCaptureLease(
  std::shared_ptr<internal::IblCaptureLeaseState> state) noexcept
  : state_(std::move(state))
{
}

auto IblCaptureLease::Attach(graphics::CommandRecorder& recorder,
  graphics::ResourceRegistry& registry) const -> Result<void, IblCaptureError>
{
  return internal::AttachCapture(state_, recorder, registry);
}

auto IblCaptureLease::Revision() const noexcept -> std::uint32_t
{
  return state_ ? state_->products.revision : 0U;
}

auto IblCaptureLease::ProcessedCube() const
  -> std::shared_ptr<const graphics::Texture>
{
  return state_ ? state_->products.processed_cube : nullptr;
}
auto IblCaptureLease::SpecularCube() const
  -> std::shared_ptr<const graphics::Texture>
{
  return state_ ? state_->products.specular_cube : nullptr;
}
auto IblCaptureLease::ProcessedHalfCube() const
  -> std::shared_ptr<const graphics::Texture>
{
  return state_ ? state_->products.processed_half_cube : nullptr;
}
auto IblCaptureLease::SpecularHalfCube() const
  -> std::shared_ptr<const graphics::Texture>
{
  return state_ ? state_->products.specular_half_cube : nullptr;
}
auto IblCaptureLease::DiffuseSh() const
  -> std::shared_ptr<const graphics::Buffer>
{
  return state_ ? state_->products.diffuse_sh : nullptr;
}
auto IblCaptureLease::Metadata() const
  -> std::shared_ptr<const graphics::Buffer>
{
  return state_ ? state_->products.metadata : nullptr;
}

} // namespace oxygen::vortex::environment
