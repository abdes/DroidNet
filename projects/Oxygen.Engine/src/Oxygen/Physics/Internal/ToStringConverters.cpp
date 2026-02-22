//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <string_view>

#include <Oxygen/Physics/Backend.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/CollisionLayers.h>
#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/Joint/JointDesc.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/Query/Overlap.h>
#include <Oxygen/Physics/Query/Sweep.h>

auto oxygen::physics::to_string(WorldId value) -> std::string
{
  return "WorldId{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(BodyId value) -> std::string
{
  return "BodyId{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(CharacterId value) -> std::string
{
  return "CharacterId{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(ShapeId value) -> std::string
{
  return "ShapeId{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(ShapeInstanceId value) -> std::string
{
  return "ShapeInstanceId{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(AreaId value) -> std::string
{
  return "AreaId{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(JointId value) -> std::string
{
  return "JointId{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(AggregateId value) -> std::string
{
  return "AggregateId{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(const CollisionLayer value) -> std::string
{
  return "CollisionLayer{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(const CollisionMask value) -> std::string
{
  return "CollisionMask{" + std::to_string(value.get()) + "}";
}

auto oxygen::physics::to_string(PhysicsBackend value) noexcept
  -> std::string_view
{
  switch (value) {
  case PhysicsBackend::kNone:
    return "none";
  case PhysicsBackend::kJolt:
    return "jolt";
  }
  return "__NotSupported__";
}

auto oxygen::physics::to_string(PhysicsError value) noexcept -> std::string_view
{
  switch (value) {
  case PhysicsError::kInvalidArgument:
    return "InvalidArgument";
  case PhysicsError::kWorldNotFound:
    return "WorldNotFound";
  case PhysicsError::kBodyNotFound:
    return "BodyNotFound";
  case PhysicsError::kCharacterNotFound:
    return "CharacterNotFound";
  case PhysicsError::kInvalidCollisionMask:
    return "InvalidCollisionMask";
  case PhysicsError::kBufferTooSmall:
    return "BufferTooSmall";
  case PhysicsError::kAlreadyExists:
    return "AlreadyExists";
  case PhysicsError::kNotInitialized:
    return "NotInitialized";
  case PhysicsError::kBackendInitFailed:
    return "BackendInitFailed";
  case PhysicsError::kNotImplemented:
    return "NotImplemented";
  case PhysicsError::kBackendUnavailable:
    return "BackendUnavailable";
  }
  return "__NotSupported__";
}

auto oxygen::physics::body::to_string(BodyType value) noexcept -> const char*
{
  switch (value) {
  case BodyType::kStatic:
    return "Static";
  case BodyType::kDynamic:
    return "Dynamic";
  case BodyType::kKinematic:
    return "Kinematic";
  }
  return "__NotSupported__";
}

auto oxygen::physics::body::to_string(BodyFlags value) -> std::string
{
  if (value == BodyFlags::kNone) {
    return "None";
  }
  if (value == BodyFlags::kAll) {
    return "All";
  }

  std::string result;
  const auto add_flag = [&](const BodyFlags flag, const char* name) {
    if ((value & flag) != BodyFlags::kNone) {
      if (!result.empty()) {
        result += "|";
      }
      result += name;
    }
  };

  add_flag(BodyFlags::kEnableGravity, "EnableGravity");
  add_flag(BodyFlags::kIsTrigger, "IsTrigger");
  add_flag(BodyFlags::kEnableContinuousCollisionDetection,
    "EnableContinuousCollisionDetection");

  return result.empty() ? "None" : result;
}

auto oxygen::physics::events::to_string(PhysicsEventType value) noexcept
  -> std::string_view
{
  switch (value) {
  case PhysicsEventType::kContactBegin:
    return "ContactBegin";
  case PhysicsEventType::kContactEnd:
    return "ContactEnd";
  case PhysicsEventType::kTriggerBegin:
    return "TriggerBegin";
  case PhysicsEventType::kTriggerEnd:
    return "TriggerEnd";
  }
  return "__NotSupported__";
}

auto oxygen::physics::joint::to_string(JointType value) noexcept
  -> std::string_view
{
  switch (value) {
  case JointType::kFixed:
    return "Fixed";
  case JointType::kDistance:
    return "Distance";
  case JointType::kHinge:
    return "Hinge";
  case JointType::kSlider:
    return "Slider";
  case JointType::kSpherical:
    return "Spherical";
  }
  return "__NotSupported__";
}
