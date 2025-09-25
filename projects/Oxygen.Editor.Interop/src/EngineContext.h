//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <memory>

#include <Oxygen/EditorInterface/EngineContext.h>

// Disable C4793: 'function' : native code generation for 'function' is disabled
// We have no choice here because the problem is coming from deep inside `asio`
// and we can't isolate that particular function:
// 'asio::detail::winsock_init_base::throw_on_error'
#pragma warning(push)
#pragma warning(disable:4793)
#include <Oxygen/Platform/Platform.h>
#pragma warning(pop)

namespace Oxygen::Editor::EngineInterface {

  public ref class EngineContext {
  private:
    using NativeEngineContext = oxygen::engine::interop::EngineContext;
    std::shared_ptr<NativeEngineContext>* native_ctx_;
  public:
    EngineContext(std::shared_ptr<NativeEngineContext> ctx)
      : native_ctx_(nullptr) {
      if (ctx) {
        native_ctx_ = new std::shared_ptr<NativeEngineContext>(std::move(ctx));
      }
    }

  public:
    // Copy-like constructor (managed semantic “copy” = share underlying native)
    EngineContext(const EngineContext% other)
      : native_ctx_(nullptr)
    {
      if (other.native_ctx_ != nullptr) {
        native_ctx_ = new std::shared_ptr<NativeEngineContext>(*other.native_ctx_);
      }
    }

    // Destructor (deterministic)
    ~EngineContext() {
      this->!EngineContext();
    }

    // Finalizer (fallback)
    !EngineContext() {
      if (native_ctx_ != nullptr) {
        delete native_ctx_;
        native_ctx_ = nullptr;
      }
    }

    property bool IsValid {
      bool get() {
        return native_ctx_ != nullptr && native_ctx_->get() != nullptr;
      }
    }

  internal:
    // Internal accessors for other interop code
    std::shared_ptr<NativeEngineContext>& NativeShared() {
      if (native_ctx_ == nullptr) {
        throw gcnew System::ObjectDisposedException("EngineContext");
      }
      return *native_ctx_;
    }

    NativeEngineContext* NativePtr() {
      return (native_ctx_ && native_ctx_->get()) ? native_ctx_->get() : nullptr;
    }

  };

} // namespace Oxygen::Editor::EngineInterface
