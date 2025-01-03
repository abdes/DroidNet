//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "OxygenWorld.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/euler_angles.hpp"
#include "glm/gtx/quaternion.hpp"
#undef GLM_ENABLE_EXPERIMENTAL

namespace {

// Convert a quaternion to a vector whose components represent the pitch, yaw
// and roll angles, in that order, in radians. Assumes the application is
// pitch as the rotation around the X axis, yaw as the rotation around the Y
// axis and roll as the rotation around the Z axis.
Vector3 QuaternionToAnglesPYR(const glm::quat& q)
{
  // Convert the quaternion to a rotation matrix in order to call the
  // next function.
  glm::mat4 m = mat4_cast(q);

  // Now get the YPM angles. The `euler_angles.hpp` contains many similar
  // functions, but this one is the inverse of the `yawPitchRoll` function.
  float y, p, r;
  glm::extractEulerAngleYXZ(m, y, p, r);

  // Transform from radians to degrees and package them as a vector in the
  // order PYR, similarly to glm::eulerAngles or glm::eulerAngleXYX.
  auto angles = glm::vec3(glm::degrees(p), glm::degrees(y), glm::degrees(r));
  return Vector3(angles.x, angles.y, angles.z);
}

// Convert a vector whose components represent the pitch, yaw and roll angles,
// in that order, in degrees, to a quaternion.
glm::quat AnglesPYRToQuaternion(const Vector3& angles)
{
  // As we used `extractEulerAngleYXZ` in `QuaternionToAngles`, we use the
  // inverse function `yawPitchRoll` to create the rotation matrix. The angles
  // need to be converted to radians this time.
  auto rot = glm::yawPitchRoll(glm::radians(angles.Y), glm::radians(angles.X), glm::radians(angles.Z));
  return glm::toQuat(rot);
}

} // namespace

using namespace Oxygen::Interop::World;

Vector3 TransformHandle::Rotation::get()
{
  const auto q = transform_->GetRotation();
  return QuaternionToAnglesPYR(q);
}

void TransformHandle::Rotation::set(Vector3 value)
{
  transform_->SetRotation(AnglesPYRToQuaternion(value));
}

GameEntityHandle ^ OxygenWorld::CreateGameEntity(GameEntityDescriptor ^ desc)
{
  using NativeGameEntityDescriptor = oxygen::world::GameEntityDescriptor;
  using NativeTransformDescriptor = oxygen::world::TransformDescriptor;

  NativeTransformDescriptor native_transform_desc {};
  const NativeGameEntityDescriptor native_desc {
    .transform = &native_transform_desc
  };

  auto pos = desc->Transform->Position;
  native_desc.transform->position = glm::vec3(pos.X, pos.Y, pos.Z);
  auto angles = desc->Transform->Rotation;
  native_desc.transform->rotation = AnglesPYRToQuaternion(angles);
  auto scale = desc->Transform->Scale;
  native_desc.transform->scale = glm::vec3(scale.X, scale.Y, scale.Z);

  return gcnew GameEntityHandle(oxygen::world::entity::CreateGameEntity(native_desc));
}
