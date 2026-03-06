//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#if defined(OXGN_PHYS_BACKEND_JOLT)
#  include <Jolt/Jolt.h> // Must always be first (keep separate)
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>

#if defined(OXGN_PHYS_BACKEND_JOLT)
#  include <Jolt/Core/Memory.h>
#  include <Jolt/Core/StreamWrapper.h>
#  include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#  include <Jolt/Physics/Vehicle/TrackedVehicleController.h>
#  include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#  include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#  include <Oxygen/Core/Constants.h>
#endif

#include <Oxygen/Physics/Test/TestBlobBuilders.h>

namespace oxygen::physics::test {

#if defined(OXGN_PHYS_BACKEND_JOLT)
namespace {

  auto ToBytes(std::string blob) -> std::vector<uint8_t>
  {
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(blob.data()),
      reinterpret_cast<const uint8_t*>(blob.data()) + blob.size());
  }

  auto EnsureJoltAllocatorReady() -> void
  {
    static std::once_flag once {};
    std::call_once(once, [] { JPH::RegisterDefaultAllocator(); });
  }

} // namespace
#endif

auto MakeSoftBodySettingsBlob(const uint32_t cluster_count)
  -> std::vector<uint8_t>
{
#if defined(OXGN_PHYS_BACKEND_JOLT)
  EnsureJoltAllocatorReady();
  const auto grid_size = std::clamp(cluster_count, 2U, 8U);
  auto shared = JPH::SoftBodySharedSettings::sCreateCube(
    grid_size, 0.25F / static_cast<float>(grid_size));
  if (shared == nullptr) {
    return {};
  }
  const JPH::SoftBodySharedSettings::VertexAttributes vertex_attributes(0.0F,
    0.0F, std::numeric_limits<float>::max(),
    JPH::SoftBodySharedSettings::ELRAType::None, 1.0F);
  shared->CreateConstraints(&vertex_attributes, 1U);
  for (auto& volume : shared->mVolumeConstraints) {
    volume.mCompliance = 1.0e-6F;
  }
  shared->CalculateVolumeConstraintVolumes();
  shared->Optimize();

  auto stream = std::ostringstream(std::ios::out | std::ios::binary);
  auto wrapped = JPH::StreamOutWrapper(stream);
  shared->SaveBinaryState(wrapped);
  if (wrapped.IsFailed()) {
    return {};
  }
  return ToBytes(stream.str());
#else
  (void)cluster_count;
  return std::vector<uint8_t> { 0x1U };
#endif
}

auto MakeSoftBodyUvSphereSettingsBlob(const uint32_t latitude_segments,
  const uint32_t longitude_segments, const float edge_compliance,
  const float shear_compliance, const float bend_compliance)
  -> std::vector<uint8_t>
{
#if defined(OXGN_PHYS_BACKEND_JOLT)
  EnsureJoltAllocatorReady();
  if (latitude_segments < 3U || longitude_segments < 3U
    || !std::isfinite(edge_compliance) || edge_compliance < 0.0F
    || !std::isfinite(shear_compliance) || shear_compliance < 0.0F
    || !std::isfinite(bend_compliance) || bend_compliance < 0.0F) {
    return {};
  }

  constexpr float kPi = 3.14159265358979323846F;
  constexpr float kRadius = 0.5F;
  auto shared = JPH::Ref<JPH::SoftBodySharedSettings> {
    new JPH::SoftBodySharedSettings {}
  };
  const auto stride = longitude_segments + 1U;
  const auto vertex_count = (latitude_segments + 1U) * stride;
  shared->mVertices.reserve(vertex_count);
  for (uint32_t lat = 0U; lat <= latitude_segments; ++lat) {
    const auto theta
      = kPi * static_cast<float>(lat) / static_cast<float>(latitude_segments);
    const auto sin_theta = std::sin(theta);
    const auto cos_theta = std::cos(theta);
    for (uint32_t lon = 0U; lon <= longitude_segments; ++lon) {
      const auto phi = 2.0F * kPi * static_cast<float>(lon)
        / static_cast<float>(longitude_segments);
      const auto sin_phi = std::sin(phi);
      const auto cos_phi = std::cos(phi);
      shared->mVertices.emplace_back(
        JPH::Float3 { sin_theta * cos_phi * kRadius,
          sin_theta * sin_phi * kRadius, cos_theta * kRadius },
        JPH::Float3 { 0.0F, 0.0F, 0.0F }, 1.0F);
    }
  }

  shared->mFaces.reserve(
    static_cast<size_t>(latitude_segments) * longitude_segments * 2U);
  const auto add_face
    = [&](const uint32_t i0, const uint32_t i1, const uint32_t i2) {
        if (i0 == i1 || i0 == i2 || i1 == i2 || i0 >= shared->mVertices.size()
          || i1 >= shared->mVertices.size() || i2 >= shared->mVertices.size()) {
          return;
        }
        const auto& p0 = shared->mVertices[i0].mPosition;
        const auto& p1 = shared->mVertices[i1].mPosition;
        const auto& p2 = shared->mVertices[i2].mPosition;
        const auto e10 = JPH::Float3 { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        const auto e20 = JPH::Float3 { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
        const auto cross = JPH::Float3 { e10.y * e20.z - e10.z * e20.y,
          e10.z * e20.x - e10.x * e20.z, e10.x * e20.y - e10.y * e20.x };
        const auto area2_sq
          = cross.x * cross.x + cross.y * cross.y + cross.z * cross.z;
        constexpr float kMinArea2Sq = 1.0e-10F;
        if (area2_sq <= kMinArea2Sq) {
          return;
        }
        shared->mFaces.emplace_back(i0, i1, i2);
      };

  for (uint32_t lat = 0U; lat < latitude_segments; ++lat) {
    for (uint32_t lon = 0U; lon < longitude_segments; ++lon) {
      const auto i0 = lat * stride + lon;
      const auto i1 = (lat + 1U) * stride + lon;
      const auto i2 = i0 + 1U;
      const auto i3 = i1 + 1U;
      add_face(i0, i1, i2);
      add_face(i2, i1, i3);
    }
  }
  if (shared->mFaces.empty()) {
    return {};
  }

  const JPH::SoftBodySharedSettings::VertexAttributes vertex_attributes(
    edge_compliance, shear_compliance, bend_compliance,
    JPH::SoftBodySharedSettings::ELRAType::None, 1.0F);
  shared->CreateConstraints(&vertex_attributes, 1U);
  shared->Optimize();

  auto stream = std::ostringstream(std::ios::out | std::ios::binary);
  auto wrapped = JPH::StreamOutWrapper(stream);
  shared->SaveBinaryState(wrapped);
  if (wrapped.IsFailed()) {
    return {};
  }
  return ToBytes(stream.str());
#else
  (void)latitude_segments;
  (void)longitude_segments;
  (void)edge_compliance;
  (void)shear_compliance;
  (void)bend_compliance;
  return std::vector<uint8_t> { 0x1U };
#endif
}

auto MakeVehicleConstraintSettingsBlob(
  const size_t wheel_count, const bool tracked) -> std::vector<uint8_t>
{
#if defined(OXGN_PHYS_BACKEND_JOLT)
  EnsureJoltAllocatorReady();
  auto settings = JPH::VehicleConstraintSettings {};
  settings.mUp = JPH::Vec3 {
    oxygen::space::move::Up.x,
    oxygen::space::move::Up.y,
    oxygen::space::move::Up.z,
  };
  settings.mForward = JPH::Vec3 {
    oxygen::space::move::Forward.x,
    oxygen::space::move::Forward.y,
    oxygen::space::move::Forward.z,
  };

  if (tracked) {
    auto controller = JPH::Ref<JPH::TrackedVehicleControllerSettings> {
      new JPH::TrackedVehicleControllerSettings()
    };
    auto left_wheels = std::vector<uint32_t> {};
    auto right_wheels = std::vector<uint32_t> {};
    left_wheels.reserve((wheel_count + 1U) / 2U);
    right_wheels.reserve(wheel_count / 2U);
    for (size_t i = 0; i < wheel_count; ++i) {
      if ((i % 2U) == 0U) {
        left_wheels.push_back(static_cast<uint32_t>(i));
      } else {
        right_wheels.push_back(static_cast<uint32_t>(i));
      }
    }
    if (left_wheels.empty() && wheel_count > 0U) {
      left_wheels.push_back(0U);
    }
    if (right_wheels.empty() && wheel_count > 1U) {
      right_wheels.push_back(1U);
    }
    if (!left_wheels.empty()) {
      auto& left_track
        = controller->mTracks[static_cast<int>(JPH::ETrackSide::Left)];
      left_track.mDrivenWheel = left_wheels.front();
      left_track.mWheels.clear();
      for (const auto index : left_wheels) {
        left_track.mWheels.push_back(index);
      }
    }
    if (!right_wheels.empty()) {
      auto& right_track
        = controller->mTracks[static_cast<int>(JPH::ETrackSide::Right)];
      right_track.mDrivenWheel = right_wheels.front();
      right_track.mWheels.clear();
      for (const auto index : right_wheels) {
        right_track.mWheels.push_back(index);
      }
    }
    settings.mController = controller;
  } else {
    auto controller = JPH::Ref<JPH::WheeledVehicleControllerSettings> {
      new JPH::WheeledVehicleControllerSettings()
    };
    if (wheel_count >= 2U) {
      auto differential = JPH::VehicleDifferentialSettings {};
      differential.mLeftWheel = 0;
      differential.mRightWheel = 1;
      differential.mEngineTorqueRatio = 1.0F;
      controller->mDifferentials.push_back(differential);
    }
    settings.mController = controller;
  }

  for (size_t i = 0; i < wheel_count; ++i) {
    (void)i;
    if (tracked) {
      auto wheel_settings
        = JPH::Ref<JPH::WheelSettingsTV> { new JPH::WheelSettingsTV() };
      settings.mWheels.push_back(
        JPH::Ref<JPH::WheelSettings> { wheel_settings.GetPtr() });
    } else {
      auto wheel_settings
        = JPH::Ref<JPH::WheelSettingsWV> { new JPH::WheelSettingsWV() };
      settings.mWheels.push_back(
        JPH::Ref<JPH::WheelSettings> { wheel_settings.GetPtr() });
    }
  }

  auto stream = std::ostringstream(std::ios::out | std::ios::binary);
  auto wrapped = JPH::StreamOutWrapper(stream);
  settings.SaveBinaryState(wrapped);
  if (wrapped.IsFailed()) {
    return {};
  }
  return ToBytes(stream.str());
#else
  (void)wheel_count;
  (void)tracked;
  return std::vector<uint8_t> { 0x1U };
#endif
}

} // namespace oxygen::physics::test
