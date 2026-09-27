//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen {
class Graphics;
}
namespace oxygen::graphics {
class Buffer;
class Texture;
class GpuBufferReadback;
class GpuTextureReadback;

//! Backend cleanup called while the supplied lifetime token remains owned.
using BackendObjectDestroy = void (*)(void*) noexcept;

//! Adopt an object with a control block and outer deleter compiled in Common.
/*!
 The lifetime token is released only after backend cleanup returns. The stored
 deleter drops its strong token at last-object destruction, so weak observers
 cannot retain a dead backend incarnation. Ownership transfers on entry,
 including when control-block allocation fails. The object and destroy function
 must be non-null. Typed overloads preserve Texture's shared_from_this contract.
*/
OXGN_GFX_NDAPI auto AdoptBackendObject(
  void* object, BackendObjectDestroy destroy, std::shared_ptr<void> lifetime)
  -> std::shared_ptr<void>;
OXGN_GFX_NDAPI auto AdoptBackendObject(Graphics* object,
  BackendObjectDestroy destroy, std::shared_ptr<void> lifetime)
  -> std::shared_ptr<Graphics>;
OXGN_GFX_NDAPI auto AdoptBackendObject(
  Buffer* object, BackendObjectDestroy destroy, std::shared_ptr<void> lifetime)
  -> std::shared_ptr<Buffer>;
OXGN_GFX_NDAPI auto AdoptBackendObject(
  Texture* object, BackendObjectDestroy destroy, std::shared_ptr<void> lifetime)
  -> std::shared_ptr<Texture>;

OXGN_GFX_NDAPI auto AdoptBackendObject(GpuBufferReadback* object,
  BackendObjectDestroy destroy, std::shared_ptr<void> lifetime)
  -> std::shared_ptr<GpuBufferReadback>;
OXGN_GFX_NDAPI auto AdoptBackendObject(GpuTextureReadback* object,
  BackendObjectDestroy destroy, std::shared_ptr<void> lifetime)
  -> std::shared_ptr<GpuTextureReadback>;

//! Transfer public polymorphic ownership into the Common control block.
OXGN_GFX_NDAPI auto AdoptBackendObject(std::unique_ptr<Graphics> object,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Graphics>;
OXGN_GFX_NDAPI auto AdoptBackendObject(std::unique_ptr<Buffer> object,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Buffer>;
OXGN_GFX_NDAPI auto AdoptBackendObject(std::unique_ptr<Texture> object,
  std::shared_ptr<void> lifetime) -> std::shared_ptr<Texture>;

} // namespace oxygen::graphics
