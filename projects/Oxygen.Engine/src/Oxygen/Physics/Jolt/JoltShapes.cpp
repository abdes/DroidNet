//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Oxygen/Physics/Jolt/JoltShapes.h>

auto oxygen::physics::jolt::JoltShapes::CreateShape(
  const shape::ShapeDesc& desc) -> PhysicsResult<ShapeId>
{
  const auto shape_result = MakeShape(desc.geometry);
  if (shape_result.has_error()) {
    return Err(shape_result.error());
  }
  const auto transformed_shape_result
    = ApplyShapeLocalTransform(desc.geometry, shape_result.value(),
      desc.local_position, desc.local_rotation, desc.local_scale);
  if (transformed_shape_result.has_error()) {
    return Err(transformed_shape_result.error());
  }

  std::scoped_lock lock(mutex_);
  if (next_shape_id_ == std::numeric_limits<uint32_t>::max()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto shape_id = ShapeId { next_shape_id_++ };
  shapes_.emplace(shape_id,
    ShapeState {
      .desc = desc,
      .shape = transformed_shape_result.value(),
      .attachment_count = 0U,
    });
  return Ok(shape_id);
}

auto oxygen::physics::jolt::JoltShapes::GetShapeDesc(
  const ShapeId shape_id) const -> PhysicsResult<shape::ShapeDesc>
{
  std::scoped_lock lock(mutex_);
  const auto it = shapes_.find(shape_id);
  if (it == shapes_.end()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  return Ok(it->second.desc);
}

auto oxygen::physics::jolt::JoltShapes::DestroyShape(const ShapeId shape_id)
  -> PhysicsResult<void>
{
  std::scoped_lock lock(mutex_);
  const auto it = shapes_.find(shape_id);
  if (it == shapes_.end()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (it->second.attachment_count != 0U) {
    return Err(PhysicsError::kAlreadyExists);
  }
  shapes_.erase(it);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltShapes::TryGetShape(
  const ShapeId shape_id) const -> PhysicsResult<JPH::RefConst<JPH::Shape>>
{
  std::scoped_lock lock(mutex_);
  const auto it = shapes_.find(shape_id);
  if (it == shapes_.end()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  return Ok(it->second.shape);
}

auto oxygen::physics::jolt::JoltShapes::AddAttachment(const ShapeId shape_id)
  -> PhysicsResult<void>
{
  std::scoped_lock lock(mutex_);
  const auto it = shapes_.find(shape_id);
  if (it == shapes_.end()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  it->second.attachment_count += 1U;
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltShapes::RemoveAttachment(const ShapeId shape_id)
  -> PhysicsResult<void>
{
  std::scoped_lock lock(mutex_);
  const auto it = shapes_.find(shape_id);
  if (it == shapes_.end()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (it->second.attachment_count == 0U) {
    return Err(PhysicsError::kInvalidArgument);
  }
  it->second.attachment_count -= 1U;
  return PhysicsResult<void>::Ok();
}
