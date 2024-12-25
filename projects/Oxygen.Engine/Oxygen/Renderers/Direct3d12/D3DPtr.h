//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "Oxygen/Renderers/Direct3d12/Detail/DeferredRelease.h"

namespace oxygen::renderer::d3d12 {

  /**
   * Deleter for Direct3D objects.
   *
   * This struct provides a custom deleter for Direct3D objects that ensures the
   * object's `Release` method is called when the object is deleted. It is
   * intended to be used with uniquely owned smart pointers such as
   * `std::unique_ptr`.
   *
   * @tparam T The type of the Direct3D object to release.
   */
  template<typename T>
  struct D3DPointerDeleter
  {
    void operator()(T* pointer) const
    {
      if (pointer)
      {
        pointer->Release();
      }
    }
  };

  /**
   * Deferred release deleter for Direct3D objects.
   *
   * This struct provides a custom deleter for Direct3D objects that ensures the
   * object's `Release` method is called when the object is no longer used by
   * the GPU. It is intended to be used with uniquely owned smart pointers such
   * as `std::unique_ptr`.
   *
   * @tparam T The type of the Direct3D object to release.
   */
  template<typename T>
  struct D3DPointerDeferredDeleter
  {
    void operator()(T* pointer) const
    {
      if (pointer)
      {
        detail::DeferredObjectRelease(pointer);
      }
    }
  };

  /**
   * Unique pointer type for Direct3D objects.
   *
   * Another option is to use a smart pointer class with reference counting,
   * such as ComPtr, from `#include <wrl/client.h>`.
   *
   * @note If you use `#include <wrl.h>` then all the WRL types will be included
   * which is a reasonable choice for apps that need it. If you just plan to
   * use `ComPtr` then the client header is all you need.
   *
   * @note In keeping with C++ best practice, you should use fully-qualified
   * names in `.h` header files. For example, `Microsoft::WRL::ComPtr field_`.
   * In .cpp source files, after including all required headers you can add the
   * following to your module to make it less verbose to use `ComPtr`:
   *
   *      using Microsoft::WRL::ComPtr;
   *      ComPtr<T> variable;
   *
   * @tparam T The type of the Direct3D object to release.
   * @param pp A pointer to the Direct3D object to release.
   *
   * @see https://github.com/microsoft/DirectXTK/wiki/ComPtr
   *
   * @{
   */

  template<typename T>
  using D3DPtr = std::unique_ptr<T, D3DPointerDeleter<T>>;

  template<typename T>
  using D3DDeferredPtr = std::unique_ptr<T, D3DPointerDeferredDeleter<T>>;

  /** @} */

  /**
   * Helper function to safely release a Direct3D object when manual reference
   * management is needed. Otherwise, use `D3DPtr` or `ComPtr` from WRL.
   *
   * @tparam T The type of the Direct3D object to release.
   * @param pp A pointer to the Direct3D object to release.
   */
  template <class T> void SafeRelease(T** pp)
  {
    if (*pp)
    {
      (*pp)->Release();
      *pp = NULL;
    }
  }

}  // namespace oxygen::renderer::d3d12
