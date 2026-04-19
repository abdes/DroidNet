//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <tuple>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/packing.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::environment::internal {

namespace {

constexpr float kLocalFogSafeFalloffThreshold = 1.0F;
constexpr float kLocalFogFalloffScaleUi = 0.01F;

struct LocalFogVolumeSortKey {
  std::uint64_t packed_data { 0U };

  [[nodiscard]] auto operator<(const LocalFogVolumeSortKey& rhs) const noexcept
    -> bool
  {
    return packed_data > rhs.packed_data;
  }
};

struct SortedLocalFogVolume {
  LocalFogVolumeGpuInstance instance {};
  LocalFogVolumeCullingInstance culling_instance {};
  LocalFogVolumeSortKey sort_key {};
};

auto FloatBits(const float value) -> std::uint32_t
{
  return std::bit_cast<std::uint32_t>(value);
}

auto PackHalf2(const float a, const float b) -> std::uint32_t
{
  return glm::packHalf2x16(glm::vec2 { a, b });
}

auto PackFloat111110(const float x, const float y, const float z)
  -> std::uint32_t
{
  return glm::packF2x11_1x10(glm::vec3 { x, y, z });
}

auto PackUnorm8888(const glm::vec4& value) -> std::uint32_t
{
  return glm::packUnorm4x8(glm::clamp(value, 0.0F, 1.0F));
}

auto EncodeSortPriority(const int sort_priority) -> std::uint16_t
{
  const auto clamped_priority = std::clamp(sort_priority, -127, 127);
  const auto signed_priority = static_cast<std::int8_t>(clamped_priority);
  return static_cast<std::uint8_t>(127 - signed_priority);
}

auto PackSortKey(const std::uint16_t index, const float distance_to_camera,
  const std::uint16_t priority) -> LocalFogVolumeSortKey
{
  auto key = LocalFogVolumeSortKey {};
  key.packed_data = static_cast<std::uint64_t>(index);
  key.packed_data |= static_cast<std::uint64_t>(FloatBits(distance_to_camera))
    << 16U;
  key.packed_data |= static_cast<std::uint64_t>(priority) << 48U;
  return key;
}

auto MakeGpuInstance(const scene::environment::LocalFogVolume& local_fog,
  const scene::detail::TransformComponent& transform,
  const ResolvedView& resolved_view)
  -> LocalFogVolumeGpuInstance
{
  const auto world_position = transform.GetWorldPosition();
  const auto world_rotation = transform.GetWorldRotation();
  const auto world_scale = glm::abs(transform.GetWorldScale());
  const float maximum_axis_scale
    = std::max(std::max(world_scale.x, world_scale.y), world_scale.z);
  const float uniform_scale = std::max(
    maximum_axis_scale * scene::environment::LocalFogVolume::kBaseVolumeRadiusMeters,
    1.0e-3F);
  const auto inverse_rotation
    = glm::mat3_cast(glm::conjugate(glm::normalize(world_rotation)));
  const auto translated_world_position
    = world_position - resolved_view.CameraPosition();
  const auto x_vec = glm::vec3 {
    inverse_rotation[0][0],
    inverse_rotation[1][0],
    inverse_rotation[2][0],
  };
  const auto y_vec = glm::vec3 {
    inverse_rotation[0][1],
    inverse_rotation[1][1],
    inverse_rotation[2][1],
  };

  auto instance = LocalFogVolumeGpuInstance {};
  instance.data0 = {
    FloatBits(translated_world_position.x),
    FloatBits(translated_world_position.y),
    FloatBits(translated_world_position.z),
    FloatBits(uniform_scale),
  };
  instance.data1 = {
    PackHalf2(x_vec.x, x_vec.y),
    PackHalf2(x_vec.z, y_vec.x),
    PackHalf2(y_vec.y, y_vec.z),
    0U,
  };
  instance.data2 = {
    PackFloat111110(local_fog.GetRadialFogExtinction(),
      local_fog.GetHeightFogExtinction(),
      std::max(local_fog.GetHeightFogFalloff(), kLocalFogSafeFalloffThreshold)
        * kLocalFogFalloffScaleUi),
    PackFloat111110(local_fog.GetFogEmissive().x, local_fog.GetFogEmissive().y,
      local_fog.GetFogEmissive().z),
    PackUnorm8888(glm::vec4 {
      local_fog.GetFogAlbedo().x,
      local_fog.GetFogAlbedo().y,
      local_fog.GetFogAlbedo().z,
      local_fog.GetFogPhaseG(),
    }),
    FloatBits(local_fog.GetHeightFogOffset()),
  };
  return instance;
}

auto MakeCullingInstance(const scene::detail::TransformComponent& transform)
  -> LocalFogVolumeCullingInstance
{
  const auto world_position = transform.GetWorldPosition();
  const auto world_scale = glm::abs(transform.GetWorldScale());
  const float maximum_axis_scale
    = std::max(std::max(world_scale.x, world_scale.y), world_scale.z);
  const float uniform_scale = std::max(
    maximum_axis_scale * scene::environment::LocalFogVolume::kBaseVolumeRadiusMeters,
    1.0e-3F);

  return LocalFogVolumeCullingInstance {
    .sphere_world = {
      world_position.x,
      world_position.y,
      world_position.z,
      uniform_scale,
    },
  };
}

} // namespace

auto LocalFogVolumeGpuInstance::GetUniformScale() const noexcept -> float
{
  return std::bit_cast<float>(data0[3]);
}

LocalFogVolumeState::LocalFogVolumeState(Renderer& renderer)
  : renderer_(renderer)
  , instance_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(LocalFogVolumeGpuInstance)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.LocalFogVolume.Instances")
  , instance_culling_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(LocalFogVolumeCullingInstance)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.LocalFogVolume.CullingInstances")
{
}

LocalFogVolumeState::~LocalFogVolumeState() = default;

auto LocalFogVolumeState::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  current_products_ = {};
  cpu_instances_.clear();
  cpu_culling_instances_.clear();
  instance_buffer_.OnFrameStart(sequence, slot);
  instance_culling_buffer_.OnFrameStart(sequence, slot);
}

auto LocalFogVolumeState::Prepare(RenderContext& ctx) -> ViewProducts&
{
  current_products_ = { .view_id = ctx.current_view.view_id };
  cpu_instances_.clear();
  cpu_culling_instances_.clear();

  if (!ctx.current_view.with_local_fog) {
    return current_products_;
  }

  const auto scene_ptr = ctx.GetScene();
  const auto resolved_view = ctx.current_view.resolved_view.get();
  if (ctx.current_view.view_id == kInvalidViewId || scene_ptr == nullptr
    || resolved_view == nullptr) {
    return current_products_;
  }

  const auto max_instances_per_tile = renderer_.GetLocalFogTileMaxInstanceCount();
  current_products_.prepared = true;
  current_products_.max_instances_per_tile = max_instances_per_tile;

  auto gathered_instances = std::vector<SortedLocalFogVolume> {};
  static_cast<void>(scene_ptr->Traverse().Traverse(
    [&gathered_instances, resolved_view](const scene::ConstVisitedNode& visited,
      const bool dry_run)
      -> scene::VisitResult {
      if (dry_run || visited.node_impl == nullptr) {
        return scene::VisitResult::kContinue;
      }

      if (!visited.node_impl->HasComponent<scene::environment::LocalFogVolume>()) {
        return scene::VisitResult::kContinue;
      }

      const auto& local_fog
        = visited.node_impl->GetComponent<scene::environment::LocalFogVolume>();
      if (!local_fog.IsEnabled()
        || (local_fog.GetRadialFogExtinction() <= 0.0F
          && local_fog.GetHeightFogExtinction() <= 0.0F)) {
        return scene::VisitResult::kContinue;
      }

      const auto& transform
        = visited.node_impl->GetComponent<scene::detail::TransformComponent>();
      auto gathered = SortedLocalFogVolume {
        .instance = MakeGpuInstance(local_fog, transform, *resolved_view),
        .culling_instance = MakeCullingInstance(transform),
        .sort_key = PackSortKey(
          static_cast<std::uint16_t>(gathered_instances.size()),
          glm::length(
            transform.GetWorldPosition() - resolved_view->CameraPosition()),
          EncodeSortPriority(local_fog.GetSortPriority())),
      };
      gathered_instances.push_back(gathered);
      return scene::VisitResult::kContinue;
    }));

  std::ranges::sort(gathered_instances,
    [](const SortedLocalFogVolume& lhs, const SortedLocalFogVolume& rhs) {
      return lhs.sort_key < rhs.sort_key;
    });

  const auto instance_count_total
    = static_cast<std::uint32_t>(gathered_instances.size());
  const auto discarded_offset = instance_count_total > max_instances_per_tile
    ? instance_count_total - max_instances_per_tile
    : 0U;
  const auto kept_instance_count = instance_count_total - discarded_offset;

  current_products_.instance_count = kept_instance_count;
  current_products_.kept_instance_offset = discarded_offset;

  cpu_instances_.reserve(kept_instance_count);
  cpu_culling_instances_.reserve(kept_instance_count);
  for (std::uint32_t index = discarded_offset; index < instance_count_total;
       ++index) {
    const auto& gathered = gathered_instances[index];
    cpu_instances_.push_back(gathered.instance);
    cpu_culling_instances_.push_back(gathered.culling_instance);
  }

  if (cpu_instances_.empty()) {
    return current_products_;
  }

  auto allocation = instance_buffer_.Allocate(current_products_.instance_count);
  auto culling_allocation
    = instance_culling_buffer_.Allocate(current_products_.instance_count);
  if (!allocation.has_value() || !culling_allocation.has_value()) {
    return current_products_;
  }
  if (!allocation->TryWriteRange(
        std::span<const LocalFogVolumeGpuInstance>(cpu_instances_))) {
    return current_products_;
  }
  if (!culling_allocation->TryWriteRange(
        std::span<const LocalFogVolumeCullingInstance>(cpu_culling_instances_))) {
    return current_products_;
  }

  current_products_.buffer_ready = true;
  current_products_.instance_buffer_slot = allocation->srv;
  current_products_.instance_culling_buffer_slot = culling_allocation->srv;
  return current_products_;
}

} // namespace oxygen::vortex::environment::internal
