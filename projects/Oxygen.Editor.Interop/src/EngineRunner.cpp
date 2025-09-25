//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "EngineRunner.h"

#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/EditorInterface/Api.h>

using namespace System;
using namespace System::Diagnostics;

// Map native logging verbosity to Microsoft.Extensions.Logging.LogLevel
// integer.
static auto MapVerbosityToManagedLevel(int verbosity) -> int {
  if (verbosity <= -3) {
    return 5; // Critical
  }
  if (verbosity == -2) {
    return 4; // Error
  }
  if (verbosity == -1) {
    return 3; // Warning
  }
  if (verbosity == 0) {
    return 2; // Information
  }
  if (verbosity == 1) {
    return 1; // Debug
  }
  return 0; // Trace
}

// Forward declare the indirection function; it's implemented after LogHandler
// so it can safely cast to LogHandler and call its instance method.
static void InvokeLogHandler(Object^ obj, const loguru::Message& msg);

// Native function registered with loguru. Declared here so LogHandler can call
// add_callback with its address. It will obtain the GCHandle target and then
// call InvokeLogHandler.
static void NativeForward(void* user_data, const loguru::Message& msg) {
  try {
    if (!user_data) {
      return;
    }
    Runtime::InteropServices::GCHandle handle =
      Runtime::InteropServices::GCHandle::FromIntPtr(IntPtr(user_data));
    if (!handle.IsAllocated) {
      return;
    }
    auto target = handle.Target;
    if (target != nullptr) {
      InvokeLogHandler(target, msg);
    }
  }
  catch (...) {
    /* swallow */
  }
}

namespace Oxygen::Editor::EngineInterface {

  // Managed helper that encapsulates all logging-related state and behavior so
  // the EngineRunner header doesn't need to include or reference native logging
  // internals.
  public
    ref class LogHandler sealed {
    public:
      LogHandler()
        : logger_(nullptr), log_method_(nullptr), formatter_delegate_(nullptr),
        callback_registered_(false), self_handle_(IntPtr::Zero) {
        Runtime::InteropServices::GCHandle h =
          Runtime::InteropServices::GCHandle::Alloc(
            this,
            Runtime::InteropServices::GCHandleType::WeakTrackResurrection);
        self_handle_ = Runtime::InteropServices::GCHandle::ToIntPtr(h);
      }

      ~LogHandler() { ReleaseCallback(); }

      !LogHandler() { ReleaseCallback(); }

      // Simple formatter moved here to avoid a separate helper type.
      static String^
        Format(Object^ s, Exception^) {
        return s == nullptr ? String::Empty : s->ToString();
      }

      void SetLogger(Object^ logger) {
        logger_ = logger;
        CacheLoggerArtifacts();
      }

      bool ConfigureLogging(LoggingConfig^ config) {
        namespace op = oxygen::engine::interop;
        op::LoggingConfig native_config{};
        native_config.verbosity = config->Verbosity;
        native_config.is_colored = config->IsColored;
        native_config.vmodules = nullptr;
        std::string vmodules;
        if (config->ModuleOverrides != nullptr) {
          vmodules =
            msclr::interop::marshal_as<std::string>(config->ModuleOverrides);
          if (!vmodules.empty()) {
            native_config.vmodules = vmodules.c_str();
          }
        }
        bool ok = op::ConfigureLogging(native_config);
        if (ok) {
          RegisterCallbackIfNeeded();
        }
        // Log an INFO message, used for testing and also nice to have for the
        // status.
        op::LogInfoMessage("Oxygen Editor logging configured.");
        return ok;
      }

      // Invoked from native forwarder through the GCHandle.
      void HandleLog(const loguru::Message& message) {
        try {
          std::string composed;
          if (message.preamble && *message.preamble) {
            composed += message.preamble;
            composed += ' ';
          }
          if (message.prefix && *message.prefix) {
            composed += message.prefix;
          }
          if (message.message && *message.message) {
            composed += message.message;
          }
          auto managedMsg = gcnew String(composed.c_str());
#if defined(_DEBUG) || !defined(NDEBUG)
          if (logger_ == nullptr) {
            Debug::WriteLine(managedMsg);
            return;
          }
#else
          if (logger_ == nullptr)
            return;
#endif

          if (log_method_ != nullptr) {
            int lvlValue = MapVerbosityToManagedLevel(message.verbosity);
            auto ps = log_method_->GetParameters();
            Object^ levelBoxed = Enum::ToObject(ps[0]->ParameterType, lvlValue);
            Object^ eventId = Activator::CreateInstance(ps[1]->ParameterType);
            Object^ state = managedMsg;
            Object^ exception = nullptr;
            auto args = gcnew array<Object^>(5) {
              levelBoxed, eventId, state,
                exception, formatter_delegate_
            };
            log_method_->Invoke(logger_, args);
            return;
          }
#if defined(_DEBUG) || !defined(NDEBUG)
          Debug::WriteLine(managedMsg);
#endif
        }
        catch (...) {
          /* swallow */
        }
      }

    private:
      void CacheLoggerArtifacts() {
        log_method_ = nullptr;
        formatter_delegate_ = nullptr;
        if (logger_ == nullptr) {
          return;
        }
        try {
          auto logger_type = logger_->GetType();
          for each(Reflection::MethodInfo ^
            method_info in logger_type->GetMethods()) {
            if (method_info->Name != "Log") {
              continue;
            }
            auto ps = method_info->GetParameters();
            if (ps->Length != 5) {
              continue;
            }
            if (!ps[0]->ParameterType->IsEnum) {
              continue;
            }
            auto format_method = LogHandler::typeid->GetMethod("Format");
            formatter_delegate_ =
              Delegate::CreateDelegate(ps[4]->ParameterType, format_method);
            log_method_ = method_info;
            break;
          }
        }
        catch (...) {
          log_method_ = nullptr;
          formatter_delegate_ = nullptr;
        }
      }

      void RegisterCallbackIfNeeded() {
        if (callback_registered_) {
          return;
        }
        // Register the native forwarder function with loguru.
        loguru::add_callback("OxygenEditorManagedLogger", &NativeForward,
          self_handle_.ToPointer(), loguru::Verbosity_MAX);
        callback_registered_ = true;
      }

      void ReleaseCallback() {
        if (callback_registered_) {
          loguru::remove_callback("OxygenEditorManagedLogger");
          callback_registered_ = false;
        }
        if (self_handle_ != IntPtr::Zero) {
          Runtime::InteropServices::GCHandle h =
            Runtime::InteropServices::GCHandle::FromIntPtr(self_handle_);
          if (h.IsAllocated) {
            h.Free();
          }
          self_handle_ = IntPtr::Zero;
        }
      }

      // Instance state
      Object^ logger_;
      Reflection::MethodInfo^ log_method_;
      Delegate^ formatter_delegate_;
      bool callback_registered_;
      IntPtr self_handle_; // GCHandle to this (for callback user_data)
  };

} // namespace Oxygen::Editor::EngineInterface

// Implementation of the thin indirection which can now safely call into
// LogHandler because LogHandler is fully defined.
static void InvokeLogHandler(Object^ obj, const loguru::Message& msg) {
  try {
    auto handler =
      safe_cast<Oxygen::Editor::EngineInterface::LogHandler^>(obj);
    if (handler != nullptr) {
      handler->HandleLog(msg);
    }
  }
  catch (...) {
    /* swallow */
  }
}

// EngineRunner implementation now delegates logging concerns to LogHandler.
namespace Oxygen::Editor::EngineInterface {

  EngineRunner::EngineRunner() : log_handler_(nullptr), disposed_(false) {
    log_handler_ = gcnew LogHandler();
  }

  EngineRunner::~EngineRunner() {
    if (!disposed_) {
      if (log_handler_ != nullptr) {
        delete log_handler_;
        log_handler_ = nullptr;
      }
      disposed_ = true;
    }
  }

  EngineRunner::!EngineRunner() {
    if (log_handler_ != nullptr) {
      delete log_handler_;
      log_handler_ = nullptr;
    }
  }

  auto EngineRunner::ConfigureLogging(LoggingConfig^ config) -> bool {
    if (log_handler_ == nullptr) {
      log_handler_ = gcnew LogHandler();
    }
    return log_handler_->ConfigureLogging(config);
  }

  auto EngineRunner::ConfigureLogging(LoggingConfig^ config, Object^ logger)
    -> bool {
    if (log_handler_ == nullptr) {
      log_handler_ = gcnew LogHandler();
    }
    log_handler_->SetLogger(logger);
    return ConfigureLogging(config);
  }

  auto EngineRunner::CreateEngine(EngineConfig^ engine_cfg) -> EngineContext^ {
    if (disposed_) {
      throw gcnew System::ObjectDisposedException("EngineRunner");
    }

    try {
      // Translate (currently empty) managed EngineConfig into native config.
      oxygen::EngineConfig native_cfg = engine_cfg->ToNative();

      // Create the native engine context (unique ownership from factory).
      auto native_unique = oxygen::engine::interop::CreateEngine(native_cfg);
      if (!native_unique) {
        return nullptr; // creation failed
      }

      // Promote unique_ptr to shared_ptr for the managed wrapper lifetime model.
      std::shared_ptr<oxygen::engine::interop::EngineContext> shared(native_unique.release());

      return gcnew EngineContext(shared);
    }
    catch (const std::exception& ex) {
#if defined(_DEBUG) || !defined(NDEBUG)
      System::Diagnostics::Debug::WriteLine(gcnew System::String(ex.what()));
#endif
      return nullptr;
    }
    catch (...) {
#if defined(_DEBUG) || !defined(NDEBUG)
      System::Diagnostics::Debug::WriteLine("Unknown exception in EngineRunner::CreateEngine");
#endif
      return nullptr;
    }
  }

  auto EngineRunner::RunEngine(EngineContext^ ctx) -> void
  {
    // This call will not return until the engine is stopped or exits.
    oxygen::engine::interop::RunEngine(ctx->NativeShared());
  }

  auto EngineRunner::StopEngine(EngineContext^ ctx) -> void
  {
    // This call will call any thread that started the RunEngine call to exit.
    oxygen::engine::interop::StopEngine(ctx->NativeShared());
  }

} // namespace Oxygen::Editor::EngineInterface
