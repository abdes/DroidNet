//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#if defined(OXGN_PHYS_BACKEND_JOLT)
#  include <Jolt/Jolt.h> // Must always be first (keep separate)
#endif

#include <algorithm>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>

#if defined(OXGN_PHYS_BACKEND_JOLT)
#  include <Jolt/Core/Memory.h>
#  include <Jolt/Core/StreamWrapper.h>
#  include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
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

auto MakeVehicleConstraintSettingsBlob(const size_t wheel_count)
  -> std::vector<uint8_t>
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

  for (size_t i = 0; i < wheel_count; ++i) {
    (void)i;
    auto wheel_settings
      = JPH::Ref<JPH::WheelSettingsWV> { new JPH::WheelSettingsWV() };
    settings.mWheels.push_back(
      JPH::Ref<JPH::WheelSettings> { wheel_settings.GetPtr() });
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
  return std::vector<uint8_t> { 0x1U };
#endif
}

} // namespace oxygen::physics::test
