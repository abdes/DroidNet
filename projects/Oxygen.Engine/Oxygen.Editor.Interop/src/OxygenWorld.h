//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/world/entity.h"
#include "oxygen/world/transform.h"

using namespace System;
using namespace System::Numerics;


namespace Oxygen::Interop::World {

  public
  ref class TransformDescriptor
  {
  public:
    Vector3 Position;
    Vector3 Rotation;
    Vector3 Scale;

    TransformDescriptor() :
      Position(Vector3(0.0f, 0.0f, 0.0f)),
      Rotation(Vector3(0.0f, 0.0f, 0.0f)),
      Scale(Vector3(1.0f, 1.0f, 1.0f))
    {
    }
  };

  public
  ref class GameEntityDescriptor
  {
  public:
    TransformDescriptor^ Transform;

    GameEntityDescriptor()
      : Transform(gcnew TransformDescriptor())
    {
    }
  };

  public
  ref class TransformHandle : IDisposable
  {
    using Transform = oxygen::world::Transform;

    Transform* transform_;

  public:
    TransformHandle(Transform&& native_transform)
      : transform_(new Transform(std::move(native_transform)))
    {
    }

    ~TransformHandle() {}

    property Vector3 Position
    {
      Vector3 get()
      {
        const auto pos = transform_->GetPosition();
        return Vector3(pos.x, pos.y, pos.z);
      }

      void set(Vector3 value)
      {
        transform_->SetPosition(glm::vec3(value.X, value.Y, value.Z));
      }
    }

    /// <summary>
    /// Gets or sets the transform rotation in the engine using Euler angles.
    /// </summary>
    /// <remarks>
    /// pitch is a rotation about the rigid body's x axis
    /// yaw is a rotation about the rigid body's y axis (pointing up),
    /// roll is a rotation about the rigid body's z axis
    property Vector3 Rotation
    {
      Vector3 get();
      void set(Vector3 value);
    }

    property Vector3 Scale
    {
      Vector3 get()
      {
        const auto scale = transform_->GetScale();
        return Vector3(scale.x, scale.y, scale.z);
      }

      void set(Vector3 value)
      {
        transform_->SetScale(glm::vec3(value.X, value.Y, value.Z));
      }
    }

  internal:
    void OnInvalidated()
    {
      // Transform handles are invalidated when the entity is removed, and that
      // is the only time when the native transform is deleted. We do not
      // implement this in the destructor/finalizer to avoid unplanned disposal
      // of the resources through a call to Dispose().
      delete transform_;
      transform_ = nullptr;
    }
  };

  public
  ref class GameEntityHandle
  {
    using GameEntity = oxygen::world::GameEntity;

    TransformHandle^ transform_;
    GameEntity* entity_;
    bool disposed_ = false;

  public:
    GameEntityHandle(GameEntity&& native_entity) :
      transform_(gcnew TransformHandle(native_entity.GetTransform())),
      entity_(new GameEntity(std::move(native_entity)))
    {
    }

    ~GameEntityHandle() {
      // Destructors in a reference type do a deterministic clean-up of resources.
      // Finalizers clean up unmanaged resources, and can be called either
      // deterministically by the destructor or nondeterministically by the
      // garbage collector.
      this->!GameEntityHandle();
    }

    !GameEntityHandle() {
      // Finalizer, called by the garbage collector or deterministically by the
      // destructor when this object Dispose() is called from the client code.
      if (entity_ == nullptr)
      {
        return;
      }

      // remove the entity from the world if it has not been removed.
      Remove();
    }

    TransformHandle^ GetTransform() { return transform_; }

    size_t Remove() {
      const auto removed = oxygen::world::entity::RemoveGameEntity(*entity_);
      ReleaseTransform();
      ReleaseEntity();
      return removed;
    }

  private:
    void ReleaseTransform() {
      transform_->OnInvalidated();
      delete transform_;
      transform_ = nullptr;
    }

    void ReleaseEntity() {
      delete entity_;
      entity_ = nullptr;
    }
  };

  public
  ref class OxygenWorld
  {
  public:
    static GameEntityHandle^ CreateGameEntity(GameEntityDescriptor^ desc);

    static size_t RemoveGameEntity(GameEntityHandle^ entity)
    {
      return entity->Remove();
    }

  public:
    static property float Precision
    {
      float get() { return glm::epsilon<float>(); }
    }

  public:
    static property float PrecisionLow
    {
      float get()
      {
        static float epsilon = glm::epsilon<float>() * 1e4f;
        return epsilon;
      }
    }
  };

} // namespace oxygen::interop::world
