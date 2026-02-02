//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <cstring>

#include <fmt/format.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>

namespace {

constexpr std::uint32_t kInvalidShadowIndex = 0xFFFFFFFFu;

[[nodiscard]] auto IsNodeVisible(const oxygen::scene::SceneNodeImpl& node)
  -> bool
{
  return node.GetFlags().GetEffectiveValue(
    oxygen::scene::SceneNodeFlags::kVisible);
}

[[nodiscard]] auto IsNodeShadowEligible(
  const oxygen::scene::SceneNodeImpl& node) -> bool
{
  return node.GetFlags().GetEffectiveValue(
    oxygen::scene::SceneNodeFlags::kCastsShadows);
}

[[nodiscard]] auto IsBakedMobility(
  const oxygen::scene::LightMobility mobility) noexcept -> bool
{
  return mobility == oxygen::scene::LightMobility::kBaked;
}

[[nodiscard]] auto ComputeDirectionWs(
  const oxygen::scene::detail::TransformComponent& transform) -> glm::vec3
{
  const auto rot = transform.GetWorldRotation();
  const glm::vec3 dir = rot * oxygen::space::move::Forward;
  const float len_sq = glm::dot(dir, dir);
  if (len_sq <= oxygen::math::EpsilonDirection) {
    return oxygen::space::move::Forward;
  }
  return glm::normalize(dir);
}

[[nodiscard]] auto PackDirectionalFlags(
  const oxygen::scene::CommonLightProperties& common,
  const bool effective_casts_shadows, const bool environment_contribution,
  const bool is_sun_light) noexcept -> std::uint32_t
{
  using oxygen::engine::DirectionalLightFlags;

  auto flags = DirectionalLightFlags::kNone;
  if (common.affects_world) {
    flags |= DirectionalLightFlags::kAffectsWorld;
  }
  if (effective_casts_shadows) {
    flags |= DirectionalLightFlags::kCastsShadows;
  }
  if (effective_casts_shadows && common.shadow.contact_shadows) {
    flags |= DirectionalLightFlags::kContactShadows;
  }
  if (environment_contribution) {
    flags |= DirectionalLightFlags::kEnvironmentContribution;
  }
  if (is_sun_light) {
    flags |= DirectionalLightFlags::kSunLight;
  }

  return static_cast<std::uint32_t>(flags);
}

[[nodiscard]] auto PackPositionalFlags(
  const oxygen::engine::PositionalLightType type,
  const oxygen::scene::CommonLightProperties& common,
  const bool effective_casts_shadows) noexcept -> std::uint32_t
{
  using oxygen::engine::PositionalLightFlags;

  auto flags
    = static_cast<std::uint32_t>(oxygen::engine::PackPositionalLightType(type));

  if (common.affects_world) {
    flags |= static_cast<std::uint32_t>(PositionalLightFlags::kAffectsWorld);
  }
  if (effective_casts_shadows) {
    flags |= static_cast<std::uint32_t>(PositionalLightFlags::kCastsShadows);
  }
  if (effective_casts_shadows && common.shadow.contact_shadows) {
    flags |= static_cast<std::uint32_t>(PositionalLightFlags::kContactShadows);
  }

  return flags;
}

} // namespace

namespace oxygen::renderer {

LightManager::LightManager(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> provider,
  const observer_ptr<CoordinatorT> inline_transfers)
  : gfx_(gfx)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
  , directional_basic_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::DirectionalLightBasic)),
      inline_transfers_, "LightManager.DirectionalBasic")
  , directional_shadows_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::DirectionalLightShadows)),
      inline_transfers_, "LightManager.DirectionalShadows")
  , positional_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::PositionalLightData)),
      inline_transfers_, "LightManager.Positional")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

LightManager::~LightManager()
{
  LOG_SCOPE_F(INFO, "LightManager Statistics");
  LOG_F(INFO, "frames started  : {}", frames_started_count_);
  if (has_completed_frame_snapshot_) {
    LOG_F(INFO, "last nodes      : {}", last_completed_nodes_visited_count_);
    LOG_F(INFO, "last emitted    : {}", last_completed_lights_emitted_count_);
    LOG_F(INFO, "last dir lights : {}", last_completed_dir_lights_count_);
    LOG_F(INFO, "last pos lights : {}", last_completed_pos_lights_count_);
  } else {
    LOG_F(INFO, "last nodes      : {}", 0);
    LOG_F(INFO, "last emitted    : {}", 0);
    LOG_F(INFO, "last dir lights : {}", 0);
    LOG_F(INFO, "last pos lights : {}", 0);
  }

  LOG_F(INFO, "total nodes     : {}", total_nodes_visited_count_);
  LOG_F(INFO, "total emitted   : {}", total_lights_emitted_count_);
  LOG_F(INFO, "peak dir lights : {}", peak_dir_lights_count_);
  LOG_F(INFO, "peak pos lights : {}", peak_pos_lights_count_);
}

auto LightManager::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  if (frames_started_count_ > 0ULL) {
    has_completed_frame_snapshot_ = true;
    last_completed_nodes_visited_count_ = nodes_visited_count_;
    last_completed_lights_emitted_count_ = lights_emitted_count_;
    last_completed_dir_lights_count_ = dir_basic_.size();
    last_completed_pos_lights_count_ = positional_.size();
  }

  ++frames_started_count_;
  nodes_visited_count_ = 0ULL;
  lights_emitted_count_ = 0ULL;

  directional_basic_buffer_.OnFrameStart(sequence, slot);
  directional_shadows_buffer_.OnFrameStart(sequence, slot);
  positional_buffer_.OnFrameStart(sequence, slot);

  directional_basic_srv_ = kInvalidShaderVisibleIndex;
  directional_shadows_srv_ = kInvalidShaderVisibleIndex;
  positional_srv_ = kInvalidShaderVisibleIndex;

  uploaded_this_frame_ = false;
  Clear();
}

auto LightManager::Clear() noexcept -> void
{
  dir_basic_.clear();
  dir_shadows_.clear();
  positional_.clear();
}

auto LightManager::CollectFromNode(const scene::SceneNodeImpl& node) -> void
{
  ++nodes_visited_count_;
  ++total_nodes_visited_count_;

  if (!IsNodeVisible(node)) {
    return;
  }

  if (!node.HasComponent<scene::detail::TransformComponent>()) {
    return;
  }

  const auto& transform
    = node.GetComponent<scene::detail::TransformComponent>();

  if (node.HasComponent<scene::DirectionalLight>()) {
    const auto& light = node.GetComponent<scene::DirectionalLight>();
    const auto& common = light.Common();
    if (!common.affects_world || IsBakedMobility(common.mobility)) {
      return;
    }

    const bool effective_casts_shadows
      = common.casts_shadows && IsNodeShadowEligible(node);

    engine::DirectionalLightBasic out {};
    out.color_rgb = common.color_rgb;
    out.intensity_lux = light.GetIntensityLux();
    out.direction_ws = ComputeDirectionWs(transform);
    out.angular_size_radians = light.GetAngularSizeRadians();
    out.shadow_index = effective_casts_shadows
      ? static_cast<std::uint32_t>(dir_shadows_.size())
      : kInvalidShadowIndex;
    out.flags = PackDirectionalFlags(common, effective_casts_shadows,
      light.GetEnvironmentContribution(), light.IsSunLight());

    dir_basic_.push_back(out);

    engine::DirectionalLightShadows shadows {};
    shadows.cascade_count = light.CascadedShadows().cascade_count;
    shadows.distribution_exponent
      = light.CascadedShadows().distribution_exponent;
    shadows.cascade_distances = light.CascadedShadows().cascade_distances;

    // Placeholder matrices; shadow pass will populate later.
    for (auto& m : shadows.cascade_view_proj) {
      m = glm::mat4 { 1.0F };
    }

    dir_shadows_.push_back(shadows);

    ++lights_emitted_count_;
    ++total_lights_emitted_count_;
    peak_dir_lights_count_
      = std::max(peak_dir_lights_count_, dir_basic_.size());
    return;
  }

  if (node.HasComponent<scene::PointLight>()) {
    const auto& light = node.GetComponent<scene::PointLight>();
    const auto& common = light.Common();
    if (!common.affects_world || IsBakedMobility(common.mobility)) {
      return;
    }

    const bool effective_casts_shadows
      = common.casts_shadows && IsNodeShadowEligible(node);

    engine::PositionalLightData out {};
    out.position_ws = transform.GetWorldPosition();
    out.range = light.GetRange();
    out.color_rgb = common.color_rgb;
    out.luminous_flux_lm = light.GetLuminousFluxLm();
    out.direction_ws = ComputeDirectionWs(transform);
    out.flags = PackPositionalFlags(
      engine::PositionalLightType::kPoint, common, effective_casts_shadows);

    out.inner_cone_cos = 0.0F;
    out.outer_cone_cos = 0.0F;
    out.source_radius = light.GetSourceRadius();
    out.decay_exponent = light.GetDecayExponent();

    out.attenuation_model
      = static_cast<std::uint32_t>(light.GetAttenuationModel());
    out.mobility = static_cast<std::uint32_t>(common.mobility);
    out.shadow_resolution_hint
      = static_cast<std::uint32_t>(common.shadow.resolution_hint);
    out.shadow_flags = 0U;
    out.shadow_bias = common.shadow.bias;
    out.shadow_normal_bias = common.shadow.normal_bias;
    out.exposure_compensation_ev = common.exposure_compensation_ev;
    out.shadow_map_index = effective_casts_shadows ? 0U : kInvalidShadowIndex;

    positional_.push_back(out);

    ++lights_emitted_count_;
    ++total_lights_emitted_count_;
    peak_pos_lights_count_
      = std::max(peak_pos_lights_count_, positional_.size());
    return;
  }

  if (node.HasComponent<scene::SpotLight>()) {
    const auto& light = node.GetComponent<scene::SpotLight>();
    const auto& common = light.Common();
    if (!common.affects_world || IsBakedMobility(common.mobility)) {
      return;
    }

    const bool effective_casts_shadows
      = common.casts_shadows && IsNodeShadowEligible(node);

    engine::PositionalLightData out {};
    out.position_ws = transform.GetWorldPosition();
    out.range = light.GetRange();
    out.color_rgb = common.color_rgb;
    out.luminous_flux_lm = light.GetLuminousFluxLm();
    out.direction_ws = ComputeDirectionWs(transform);
    out.flags = PackPositionalFlags(
      engine::PositionalLightType::kSpot, common, effective_casts_shadows);

    out.inner_cone_cos = std::cos(light.GetInnerConeAngleRadians());
    out.outer_cone_cos = std::cos(light.GetOuterConeAngleRadians());
    out.source_radius = light.GetSourceRadius();
    out.decay_exponent = light.GetDecayExponent();

    out.attenuation_model
      = static_cast<std::uint32_t>(light.GetAttenuationModel());
    out.mobility = static_cast<std::uint32_t>(common.mobility);
    out.shadow_resolution_hint
      = static_cast<std::uint32_t>(common.shadow.resolution_hint);
    out.shadow_flags = 0U;
    out.shadow_bias = common.shadow.bias;
    out.shadow_normal_bias = common.shadow.normal_bias;
    out.exposure_compensation_ev = common.exposure_compensation_ev;
    out.shadow_map_index = effective_casts_shadows ? 0U : kInvalidShadowIndex;

    positional_.push_back(out);

    ++lights_emitted_count_;
    ++total_lights_emitted_count_;
    peak_pos_lights_count_
      = std::max(peak_pos_lights_count_, positional_.size());
  }
}

auto LightManager::EnsureFrameResources() -> void
{
  if (uploaded_this_frame_) {
    return;
  }

  if (!dir_basic_.empty()) {
    const auto count = static_cast<std::uint32_t>(dir_basic_.size());

    auto hot_res = directional_basic_buffer_.Allocate(count);
    if (!hot_res) {
      LOG_F(ERROR, "Failed to allocate directional hot buffer: {}",
        hot_res.error().message());
    } else {
      const auto& alloc = *hot_res;
      DLOG_F(2, "LightManager writing {} directional hot to {}", count,
        fmt::ptr(alloc.mapped_ptr));
      std::memcpy(alloc.mapped_ptr, dir_basic_.data(),
        dir_basic_.size() * sizeof(engine::DirectionalLightBasic));
      directional_basic_srv_ = alloc.srv;
    }

    auto cold_res = directional_shadows_buffer_.Allocate(count);
    if (!cold_res) {
      LOG_F(ERROR, "Failed to allocate directional shadows buffer: {}",
        cold_res.error().message());
    } else {
      const auto& alloc = *cold_res;
      std::memcpy(alloc.mapped_ptr, dir_shadows_.data(),
        dir_shadows_.size() * sizeof(engine::DirectionalLightShadows));
      directional_shadows_srv_ = alloc.srv;
    }
  }

  if (!positional_.empty()) {
    const auto count = static_cast<std::uint32_t>(positional_.size());

    auto res = positional_buffer_.Allocate(count);
    if (!res) {
      LOG_F(ERROR, "Failed to allocate positional lights buffer: {}",
        res.error().message());
    } else {
      const auto& alloc = *res;
      DLOG_F(2, "LightManager writing {} positional to {}", count,
        fmt::ptr(alloc.mapped_ptr));
      std::memcpy(alloc.mapped_ptr, positional_.data(),
        positional_.size() * sizeof(engine::PositionalLightData));
      positional_srv_ = alloc.srv;
    }
  }

  uploaded_this_frame_ = true;
}

auto LightManager::GetDirectionalLightsSrvIndex() const -> ShaderVisibleIndex
{
  if (directional_basic_srv_ == kInvalidShaderVisibleIndex) {
    // NOLINTNEXTLINE(*-pro-type-const-cast)
    const_cast<LightManager*>(this)->EnsureFrameResources();
  }
  return directional_basic_srv_;
}

auto LightManager::GetDirectionalShadowsSrvIndex() const -> ShaderVisibleIndex
{
  if (directional_shadows_srv_ == kInvalidShaderVisibleIndex) {
    // NOLINTNEXTLINE(*-pro-type-const-cast)
    const_cast<LightManager*>(this)->EnsureFrameResources();
  }
  return directional_shadows_srv_;
}

auto LightManager::GetPositionalLightsSrvIndex() const -> ShaderVisibleIndex
{
  if (positional_srv_ == kInvalidShaderVisibleIndex) {
    // NOLINTNEXTLINE(*-pro-type-const-cast)
    const_cast<LightManager*>(this)->EnsureFrameResources();
  }
  return positional_srv_;
}

auto LightManager::GetDirectionalLights() const noexcept
  -> std::span<const engine::DirectionalLightBasic>
{
  return dir_basic_;
}

auto LightManager::GetDirectionalShadows() const noexcept
  -> std::span<const engine::DirectionalLightShadows>
{
  return dir_shadows_;
}

auto LightManager::GetPositionalLights() const noexcept
  -> std::span<const engine::PositionalLightData>
{
  return positional_;
}

} // namespace oxygen::renderer
