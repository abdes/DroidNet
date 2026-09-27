//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "DemoShell/UI/DroneCameraController.h"
#include <glm/common.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

namespace {

  constexpr double kMinimumPositiveControl = 0.1;
  constexpr float kMinimumPoiRadius = 0.1F;
  constexpr float kDirectionEpsilon = 1e-6F;
  constexpr float kNearVertical = 0.9F;
  //! Evaluate a closed Catmull-Rom spline at parameter u in [0,1].
  auto EvalClosedCatmullRom(const std::vector<glm::vec3>& pts, double u)
    -> glm::vec3
  {
    if (pts.empty()) {
      return glm::vec3(0.0F);
    }
    const auto n = static_cast<int>(pts.size());
    if (n == 1) {
      return pts.front();
    }

    // Wrap u to [0,1)
    u = std::fmod(u, 1.0);
    if (u < 0.0) {
      u += 1.0;
    }

    const double scaled = u * n;
    const int i = static_cast<int>(std::floor(scaled)) % n;
    const auto t = static_cast<float>(scaled - std::floor(scaled));

    // Four control points with wrapping
    const glm::vec3& p0 = pts.at((i - 1 + n) % n);
    const glm::vec3& p1 = pts.at(i);
    const glm::vec3& p2 = pts.at((i + 1) % n);
    const glm::vec3& p3 = pts.at((i + 2) % n);

    // Catmull-Rom matrix multiplication
    const float t2 = t * t;
    const float t3 = t2 * t;

    constexpr float kCubicThree = 3.0F;
    constexpr float kCubicFive = 5.0F;
    return 0.5F
      * (2.0F * p1 + (-p0 + p2) * t
        + (2.0F * p0 - kCubicFive * p1 + 4.0F * p2 - p3) * t2
        + (-p0 + kCubicThree * p1 - kCubicThree * p2 + p3) * t3);
  }

  //! Build arc-length lookup table for constant-speed traversal.
  void BuildArcLengthLut(const std::vector<glm::vec3>& pts, int samples,
    std::vector<double>& u_samples, std::vector<double>& s_samples)
  {
    u_samples.clear();
    s_samples.clear();
    u_samples.reserve(samples + 1);
    s_samples.reserve(samples + 1);

    u_samples.push_back(0.0);
    s_samples.push_back(0.0);

    glm::vec3 prev = EvalClosedCatmullRom(pts, 0.0);
    double cumulative = 0.0;

    for (int i = 1; i <= samples; ++i) {
      const double u = static_cast<double>(i) / samples;
      const glm::vec3 curr = EvalClosedCatmullRom(pts, u);
      cumulative += glm::length(curr - prev);
      u_samples.push_back(u);
      s_samples.push_back(cumulative);
      prev = curr;
    }
  }

  //! Convert arc-length s to parameter u using LUT.
  auto ArcLengthToParamU(double s, const std::vector<double>& u_samples,
    const std::vector<double>& s_samples) -> double
  {
    if (u_samples.empty() || s_samples.empty()) {
      return 0.0;
    }
    const double total_length = s_samples.back();
    if (total_length <= 0.0) {
      return 0.0;
    }

    // Wrap s to [0, total_length)
    s = std::fmod(s, total_length);
    if (s < 0.0) {
      s += total_length;
    }

    // Binary search for interval
    auto it = std::ranges::lower_bound(s_samples, s);
    if (it == s_samples.begin()) {
      return u_samples.front();
    }
    if (it == s_samples.end()) {
      return u_samples.back();
    }

    const auto idx = static_cast<size_t>(std::distance(s_samples.begin(), it));
    const double s0 = s_samples.at(idx - 1);
    const double s1 = s_samples.at(idx);
    const double u0 = u_samples.at(idx - 1);
    const double u1 = u_samples.at(idx);

    if (s1 <= s0) {
      return u0;
    }

    const double t = (s - s0) / (s1 - s0);
    return u0 + (t * (u1 - u0));
  }

} // namespace

//! Private implementation struct.
struct DroneCameraController::Impl {
  PathGenerator path_generator;
  std::vector<glm::vec3> path_points;
  double path_length { 0.0 };

  // Arc-length LUT
  std::vector<double> lut_u;
  std::vector<double> lut_s;

  // Current state
  double path_s { 0.0 }; // Arc-length position
  glm::vec3 current_pos { 0.0F };
  glm::quat current_rot { 1.0F, 0.0F, 0.0F, 0.0F };
  bool initialized { false };
  bool flying { false };

  // Ramp-in state
  double ramp_time { 2.0 };
  double ramp_elapsed { 0.0 };

  // Speed and dynamics
  double speed { 6.0 };
  double damping { 8.0 };

  // Focus target
  glm::vec3 focus_target { 0.0F, 0.0F, 0.8F };
  float focus_strength { 0.8F };
  glm::vec3 previous_tangent { 0.0F };
  bool tangent_valid { false };

  // POI slowdown
  std::vector<glm::vec3> pois;
  float poi_radius { 5.0F };
  float poi_min_speed_factor { 0.3F };

  // Cinematics
  double bob_amp { 0.06 };
  double bob_freq { 1.6 };
  double noise_amp { 0.03 };
  double bank_factor { 0.045 };
  double max_bank { 0.45 };

  // Noise smoothing state
  glm::vec2 noise_state { 0.0F, 0.0F };
  float noise_response { 8.0F };

  // Animation time accumulator
  double anim_time { 0.0 };

  // Path preview
  bool show_path_preview { false };
};

DroneCameraController::DroneCameraController()
  : impl_(std::make_unique<Impl>())
{
}

DroneCameraController::~DroneCameraController() = default;

void DroneCameraController::SetPathGenerator(PathGenerator generator)
{
  impl_->path_generator = std::move(generator);

  impl_->path_points = impl_->path_generator ? impl_->path_generator()
                                             : std::vector<glm::vec3> {};
  const bool finite
    = std::ranges::all_of(impl_->path_points, [](const auto& point) -> auto {
        return std::isfinite(point.x) && std::isfinite(point.y)
          && std::isfinite(point.z);
      });
  impl_->lut_u.clear();
  impl_->lut_s.clear();
  impl_->path_length = 0.0;
  impl_->path_s = 0.0;
  impl_->tangent_valid = false;
  if (!finite || impl_->path_points.size() < 2U) {
    impl_->path_points.clear();
    impl_->flying = false;
    return;
  }
  constexpr int kLutSamples = 512;
  BuildArcLengthLut(
    impl_->path_points, kLutSamples, impl_->lut_u, impl_->lut_s);
  impl_->path_length = impl_->lut_s.back();
  if (impl_->path_length <= std::numeric_limits<double>::epsilon()) {
    impl_->flying = false;
  }
}

auto DroneCameraController::GetPathLength() const noexcept -> double
{
  return impl_->path_length;
}

auto DroneCameraController::SetProgress(const double progress) -> void
{
  if (!std::isfinite(progress) || !HasPath()) {
    return;
  }
  impl_->path_s = (progress - std::floor(progress)) * impl_->path_length;
  impl_->tangent_valid = false;
}

auto DroneCameraController::SetFocusStrength(const float strength) -> void
{
  if (std::isfinite(strength)) {
    impl_->focus_strength = std::clamp(strength, 0.0F, 1.0F);
  }
}

auto DroneCameraController::GetFocusStrength() const noexcept -> float
{
  return impl_->focus_strength;
}

auto DroneCameraController::HasPath() const noexcept -> bool
{
  return !impl_->path_points.empty() && impl_->path_length > 0.0;
}

auto DroneCameraController::GetPathPoints() const noexcept
  -> const std::vector<glm::vec3>&
{
  return impl_->path_points;
}

void DroneCameraController::SetSpeed(double units_per_sec)
{
  impl_->speed = std::max(kMinimumPositiveControl, units_per_sec);
}

auto DroneCameraController::GetSpeed() const noexcept -> double
{
  return impl_->speed;
}

void DroneCameraController::SetDamping(double factor)
{
  impl_->damping = std::max(kMinimumPositiveControl, factor);
}

auto DroneCameraController::GetDamping() const noexcept -> double
{
  return impl_->damping;
}

void DroneCameraController::SetRampTime(double seconds)
{
  impl_->ramp_time = std::max(0.0, seconds);
}

auto DroneCameraController::GetRampTime() const noexcept -> double
{
  return impl_->ramp_time;
}

void DroneCameraController::SetFocusTarget(glm::vec3 target)
{
  impl_->focus_target = target;
}

auto DroneCameraController::GetFocusTarget() const noexcept -> glm::vec3
{
  return impl_->focus_target;
}

void DroneCameraController::SetFocusHeight(float height)
{
  impl_->focus_target.z = height;
}

auto DroneCameraController::GetFocusHeight() const noexcept -> float
{
  return impl_->focus_target.z;
}

void DroneCameraController::SetPOIs(std::vector<glm::vec3> pois)
{
  impl_->pois = std::move(pois);
}

void DroneCameraController::SetPOISlowdownRadius(float radius)
{
  impl_->poi_radius = std::max(kMinimumPoiRadius, radius);
}

auto DroneCameraController::GetPOISlowdownRadius() const noexcept -> float
{
  return impl_->poi_radius;
}

void DroneCameraController::SetPOIMinSpeedFactor(float factor)
{
  impl_->poi_min_speed_factor = glm::clamp(factor, 0.0F, 1.0F);
}

auto DroneCameraController::GetPOIMinSpeedFactor() const noexcept -> float
{
  return impl_->poi_min_speed_factor;
}

void DroneCameraController::SetBobAmplitude(double amp)
{
  impl_->bob_amp = std::max(0.0, amp);
}

auto DroneCameraController::GetBobAmplitude() const noexcept -> double
{
  return impl_->bob_amp;
}

void DroneCameraController::SetBobFrequency(double hz)
{
  impl_->bob_freq = std::max(kMinimumPositiveControl, hz);
}

auto DroneCameraController::GetBobFrequency() const noexcept -> double
{
  return impl_->bob_freq;
}

void DroneCameraController::SetNoiseAmplitude(double amp)
{
  impl_->noise_amp = std::max(0.0, amp);
}

auto DroneCameraController::GetNoiseAmplitude() const noexcept -> double
{
  return impl_->noise_amp;
}

void DroneCameraController::SetBankFactor(double factor)
{
  impl_->bank_factor = std::max(0.0, factor);
}

auto DroneCameraController::GetBankFactor() const noexcept -> double
{
  return impl_->bank_factor;
}

void DroneCameraController::SetMaxBank(double radians)
{
  impl_->max_bank = std::max(0.0, radians);
}

auto DroneCameraController::GetMaxBank() const noexcept -> double
{
  return impl_->max_bank;
}

void DroneCameraController::Start()
{
  if (!HasPath()) {
    return;
  }
  impl_->flying = true;
  impl_->ramp_elapsed = 0.0;
}

void DroneCameraController::Stop() { impl_->flying = false; }

auto DroneCameraController::IsFlying() const noexcept -> bool
{
  return impl_->flying;
}

auto DroneCameraController::GetProgress() const noexcept -> double
{
  if (impl_->path_length <= 0.0) {
    return 0.0;
  }
  return impl_->path_s / impl_->path_length;
}

void DroneCameraController::SetShowPathPreview(bool show)
{
  impl_->show_path_preview = show;
}

auto DroneCameraController::GetShowPathPreview() const noexcept -> bool
{
  return impl_->show_path_preview;
}

void DroneCameraController::SyncFromTransform(scene::SceneNode& camera)
{
  if (!camera.IsAlive()) {
    return;
  }
  if (const auto pos = camera.GetTransform().GetLocalPosition()) {
    impl_->current_pos = *pos;
  }
  if (const auto rot = camera.GetTransform().GetLocalRotation()) {
    impl_->current_rot = *rot;
  }
  impl_->initialized = true;
  impl_->tangent_valid = false;
  if (HasPath()) {
    auto nearest_distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0U; index < impl_->lut_u.size(); ++index) {
      const auto offset
        = EvalClosedCatmullRom(impl_->path_points, impl_->lut_u.at(index))
        - impl_->current_pos;
      const auto distance = glm::dot(offset, offset);
      if (distance < nearest_distance) {
        nearest_distance = distance;
        impl_->path_s = impl_->lut_s.at(index);
      }
    }
  }
}

void DroneCameraController::Update(
  scene::SceneNode& camera, time::CanonicalDuration delta_time)
{
  if (!camera.IsAlive() || !HasPath() || !impl_->flying) {
    return;
  }

  const double dt
    = std::min(std::chrono::duration<double>(delta_time.get()).count(), 0.05);

  if (!(dt > 0.0) || !std::isfinite(dt)) {
    return;
  }
  impl_->anim_time += dt;

  // Compute effective speed with POI slowdown
  double effective_speed = impl_->speed;
  if (!impl_->pois.empty()) {
    float min_dist = std::numeric_limits<float>::max();
    for (const auto& poi : impl_->pois) {
      const float dist = glm::length(impl_->current_pos - poi);
      min_dist = std::min(min_dist, dist);
    }
    if (min_dist < impl_->poi_radius) {
      const float t = min_dist / impl_->poi_radius;
      const float factor = glm::mix(impl_->poi_min_speed_factor, 1.0F, t);
      effective_speed *= factor;
    }
  }

  // Ramp-in for smooth start
  if (impl_->flying && impl_->ramp_time > 0.0
    && impl_->ramp_elapsed < impl_->ramp_time) {
    impl_->ramp_elapsed += dt;
    const double ramp_factor
      = glm::clamp(impl_->ramp_elapsed / impl_->ramp_time, 0.0, 1.0);
    effective_speed *= ramp_factor;
  }

  // Advance along path only if flying
  if (impl_->flying) {
    impl_->path_s
      = std::fmod(impl_->path_s + (effective_speed * dt), impl_->path_length);
    if (impl_->path_s < 0.0) {
      impl_->path_s += impl_->path_length;
    }
  }

  // Compute position from arc-length
  const double u = ArcLengthToParamU(impl_->path_s, impl_->lut_u, impl_->lut_s);
  glm::vec3 base_pos = EvalClosedCatmullRom(impl_->path_points, u);

  // Compute tangent for forward direction
  const double eps_s = impl_->path_length * 1e-3;
  const double u_eps
    = ArcLengthToParamU(impl_->path_s + eps_s, impl_->lut_u, impl_->lut_s);
  const glm::vec3 p_ahead = EvalClosedCatmullRom(impl_->path_points, u_eps);
  glm::vec3 tangent = p_ahead - base_pos;
  if (glm::length(tangent) > kDirectionEpsilon) {
    tangent = glm::normalize(tangent);
  } else {
    tangent = space::move::Up;
  }

  // Apply vertical bob
  const auto bob_offset = static_cast<float>(impl_->bob_amp
    * std::sin(impl_->anim_time * impl_->bob_freq * glm::two_pi<double>()));
  base_pos.z += bob_offset;

  // Apply lateral noise (smoothed)
  auto right = glm::cross(tangent, space::move::Up);
  if (glm::dot(right, right) <= std::numeric_limits<float>::epsilon()) {
    right = impl_->current_rot * space::move::Right;
  } else {
    right = glm::normalize(right);
  }
  const auto noise_target_x
    = static_cast<float>(impl_->noise_amp * std::sin(impl_->anim_time * 2.3));
  const auto noise_target_y
    = static_cast<float>(impl_->noise_amp * std::cos(impl_->anim_time * 1.7));
  const auto noise_smooth = static_cast<float>(
    glm::clamp(1.0 - std::exp(-dt * impl_->noise_response), 0.0, 1.0));
  impl_->noise_state.x
    = glm::mix(impl_->noise_state.x, noise_target_x, noise_smooth);
  impl_->noise_state.y
    = glm::mix(impl_->noise_state.y, noise_target_y, noise_smooth);
  base_pos += right * impl_->noise_state.x;
  base_pos.z += impl_->noise_state.y;

  // Compute desired rotation toward focus target
  glm::vec3 focus_dir = impl_->focus_target - base_pos;
  if (glm::length(focus_dir) > kDirectionEpsilon) {
    focus_dir = glm::normalize(focus_dir);
  } else {
    focus_dir = tangent;
  }

  // Blend between tangent and focus direction
  constexpr float max_rot = glm::radians(180.0F);
  const float dotv = glm::clamp(glm::dot(tangent, focus_dir), -1.0F, 1.0F);
  const float ang = std::acos(dotv);
  const float apply_angle = glm::min(max_rot, ang * impl_->focus_strength);
  glm::vec3 axis = glm::cross(tangent, focus_dir);
  if (glm::length(axis) < kDirectionEpsilon) {
    axis = (std::abs(tangent.z) > kNearVertical) ? space::move::Right
                                                 : space::move::Up;
  } else {
    axis = glm::normalize(axis);
  }
  const glm::quat focus_rot = glm::angleAxis(apply_angle, axis);
  glm::vec3 final_fwd = glm::normalize(focus_rot * tangent);

  // Clamp pitch
  auto ClampPitch = [](glm::vec3 fwd) -> glm::vec3 {
    constexpr float max_pitch = glm::radians(45.0F);
    auto horiz = glm::vec3(fwd.x, fwd.y, 0.0F);
    if (glm::dot(horiz, horiz) <= std::numeric_limits<float>::epsilon()) {
      horiz = space::move::Forward;
    } else {
      horiz = glm::normalize(horiz);
    }
    const float current_pitch = std::asin(glm::clamp(fwd.z, -1.0F, 1.0F));
    if (std::abs(current_pitch) <= max_pitch) {
      return fwd;
    }
    const float clamped = glm::sign(current_pitch) * max_pitch;
    const float z = std::sin(clamped);
    const float scale = std::cos(clamped);
    return glm::normalize(glm::vec3(horiz.x * scale, horiz.y * scale, z));
  };
  final_fwd = ClampPitch(final_fwd);

  constexpr glm::vec3 base_up = space::move::Up;
  glm::quat desired_rot = glm::quatLookAtRH(final_fwd, base_up);

  // Bank with route curvature, not positional smoothing error.
  if (impl_->tangent_valid && impl_->bank_factor > 0.0) {
    const auto sine
      = glm::dot(glm::cross(impl_->previous_tangent, tangent), base_up);
    const auto cosine
      = glm::clamp(glm::dot(impl_->previous_tangent, tangent), -1.0F, 1.0F);
    const auto turn_rate = std::atan2(sine, cosine) / dt;
    const auto bank_angle = static_cast<float>(
      std::clamp(turn_rate * effective_speed * impl_->bank_factor,
        -impl_->max_bank, impl_->max_bank));
    desired_rot = glm::angleAxis(bank_angle, final_fwd) * desired_rot;
  }
  impl_->previous_tangent = tangent;
  impl_->tangent_valid = true;

  // Smooth position and rotation
  const auto smooth_t = static_cast<float>(
    glm::clamp(1.0 - std::exp(-dt * impl_->damping), 0.0, 1.0));

  if (!impl_->initialized) {
    impl_->current_pos = base_pos;
    impl_->current_rot = desired_rot;
    impl_->initialized = true;
  } else {
    impl_->current_pos = glm::mix(impl_->current_pos, base_pos, smooth_t);
    impl_->current_rot = glm::slerp(impl_->current_rot, desired_rot, smooth_t);
  }

  // Apply to camera
  camera.GetTransform().SetLocalPosition(impl_->current_pos);
  camera.GetTransform().SetLocalRotation(impl_->current_rot);
}

} // namespace oxygen::examples::ui
