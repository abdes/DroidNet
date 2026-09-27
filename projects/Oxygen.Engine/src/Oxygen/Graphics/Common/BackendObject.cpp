//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>
#include <memory>
#include <utility>

#include <Oxygen/Graphics/Common/BackendObject.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>

namespace oxygen::graphics {
namespace {
  struct BackendObjectDeleter {
    BackendObjectDestroy destroy;
    std::shared_ptr<void> lifetime;

    auto operator()(void* object) noexcept -> void
    {
      // Empty the stored deleter before entering backend code. A weak observer
      // may keep this control block alive after the native object has died.
      const auto keep_alive = std::move(lifetime);
      destroy(object);
    }
  };

  template <typename T>
  auto Adopt(T* object, BackendObjectDestroy destroy,
    std::shared_ptr<void> lifetime) -> std::shared_ptr<T>
  {
    assert(object != nullptr && destroy != nullptr);
    return {
      object,
      BackendObjectDeleter {
        .destroy = destroy,
        .lifetime = std::move(lifetime),
      },
    };
  }

  template <typename T>
  auto Adopt(std::unique_ptr<T> object, std::shared_ptr<void> lifetime)
    -> std::shared_ptr<T>
  {
    return Adopt(
      object.release(),
      [](void* pointer) noexcept -> void {
        const auto owner = std::unique_ptr<T>(static_cast<T*>(pointer));
      },
      std::move(lifetime));
  }
} // namespace

auto AdoptBackendObject(std::unique_ptr<Graphics> object,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Graphics>
{
  return Adopt(std::move(object), std::move(lifetime));
}

auto AdoptBackendObject(std::unique_ptr<Buffer> object,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Buffer>
{
  return Adopt(std::move(object), std::move(lifetime));
}

auto AdoptBackendObject(std::unique_ptr<Texture> object,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Texture>
{
  return Adopt(std::move(object), std::move(lifetime));
}

auto AdoptBackendObject(void* object, BackendObjectDestroy destroy,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<void>
{
  return Adopt(object, destroy, std::move(lifetime));
}

auto AdoptBackendObject(Buffer* object, BackendObjectDestroy destroy,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Buffer>
{
  return Adopt(object, destroy, std::move(lifetime));
}

auto AdoptBackendObject(Graphics* object, BackendObjectDestroy destroy,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Graphics>
{
  return Adopt(object, destroy, std::move(lifetime));
}

auto AdoptBackendObject(Texture* object, BackendObjectDestroy destroy,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Texture>
{
  return Adopt(object, destroy, std::move(lifetime));
}
auto AdoptBackendObject(GpuBufferReadback* object, BackendObjectDestroy destroy,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<GpuBufferReadback>
{
  return Adopt(object, destroy, std::move(lifetime));
}
auto AdoptBackendObject(GpuTextureReadback* object,
  BackendObjectDestroy destroy, std::shared_ptr<void> lifetime)
  -> std::shared_ptr<GpuTextureReadback>
{
  return Adopt(object, destroy, std::move(lifetime));
}
} // namespace oxygen::graphics
