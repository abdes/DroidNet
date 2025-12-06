//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "pch.h"

#include "Base/LoguruWrapper.h"
#include "EngineRunner.h"
#include "LogHandler.h"

using namespace System;
using namespace System::Diagnostics;
using namespace System::Threading;
using namespace System::Threading::Tasks;
using namespace Microsoft::Extensions::Logging;
using namespace Oxygen::Interop::Logging;
using namespace oxygen::interop::module;

// Map native logging verbosity to Microsoft.Extensions.Logging.LogLevel
// integer.
static auto MapVerbosityToManagedLevel(int verbosity) -> LogLevel {
  if (verbosity <= -3) {
    return LogLevel::Critical;
  }
  if (verbosity == -2) {
    return LogLevel::Error;
  }
  if (verbosity == -1) {
    return LogLevel::Warning;
  }
  if (verbosity == 0) {
    return LogLevel::Information;
  }
  if (verbosity == 1) {
    return LogLevel::Debug;
  }
  return LogLevel::Trace;
}

// Forward declare the indirection function; it's implemented after LogHandler
// so it can safely cast to LogHandler and call its instance method.
namespace {
  void InvokeLogHandler(Object^ obj, const loguru::Message& msg);
} // anonymous namespace

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

namespace {
  // Implementation of the thin indirection which can now safely call into
  // LogHandler because LogHandler is fully defined.
  void InvokeLogHandler(Object^ obj, const loguru::Message& msg) {
    try {
      auto handler = safe_cast<Oxygen::Interop::LogHandler^>(obj);
      if (handler != nullptr) {
        handler->HandleLog(msg);
      }
    }
    catch (...) {
      /* swallow */
    }
  }
} // anonymous namespace

namespace Oxygen::Interop {

  LogHandler::LogHandler()
    : logger_(nullptr), callback_registered_(false),
    self_handle_(IntPtr::Zero) {
    Runtime::InteropServices::GCHandle h =
      Runtime::InteropServices::GCHandle::Alloc(
        this, Runtime::InteropServices::GCHandleType::WeakTrackResurrection);
    self_handle_ = Runtime::InteropServices::GCHandle::ToIntPtr(h);
  }

  void LogHandler::SetLogger(Object^ logger) {
    logger_ = nullptr;
    if (logger == nullptr) {
      return;
    }

    auto ilogger = dynamic_cast<ILogger^>(logger);
    if (ilogger == nullptr) {
      throw gcnew ArgumentException(
        "logger must implement Microsoft.Extensions.Logging.ILogger", "logger");
    }

    logger_ = ilogger;
  }

  bool LogHandler::ConfigureLogging(LoggingConfig^ config) {
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
      Loguru::WriteAndFlush(Loguru::Verbosity::Verbosity_INFO,
        gcnew String(L"Oxygen Editor logging configured."));
      // Keep a copy of the currently applied config for later inspection.
      this->current_config_ = gcnew LoggingConfig();
      this->current_config_->Verbosity = config->Verbosity;
      this->current_config_->IsColored = config->IsColored;
      this->current_config_->ModuleOverrides = config->ModuleOverrides;
    }
    return ok;
  }

  LoggingConfig^ LogHandler::GetCurrentConfig() {
    if (this->current_config_ == nullptr) {
      return gcnew LoggingConfig();
    }

    return this->current_config_;
  }

  // Invoked from native forwarder through the GCHandle.
  void LogHandler::HandleLog(const loguru::Message& message) {
    try {
      std::string composed;
      if (message.preamble && *message.preamble) {
        composed += message.preamble;
        composed += ' ';
      }
      if (message.indentation && *message.indentation) {
        std::string indent = message.indentation;
        size_t pos = 0;
        while ((pos = indent.find(".   ", pos)) != std::string::npos) {
          indent.replace(pos, 4, ". ");
          pos += 2;
        }
        composed += indent;
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
      if (logger_ == nullptr) {
        return;
      }
#endif

      LogLevel lvlValue = MapVerbosityToManagedLevel(message.verbosity);

      if (logger_ != nullptr) {
        logger_->Log<String^>(
          lvlValue, EventId(0), managedMsg, nullptr,
          gcnew Func<String^, Exception^, String^>(&LogHandler::Format));
      }
    }
    catch (...) {
      /* swallow */
    }
  }

  String^ LogHandler::Format(String^ state, System::Exception^ ex) {
    return state;
  }

  void LogHandler::RegisterCallbackIfNeeded() {
    if (callback_registered_) {
      return;
    }
    // Register the native forwarder function with loguru.
    loguru::add_callback("OxygenEditorManagedLogger", &NativeForward,
      self_handle_.ToPointer(), loguru::Verbosity_MAX);
    callback_registered_ = true;
  }

  void LogHandler::ReleaseCallback() {
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

  // EngineRunner methods

  auto EngineRunner::ConfigureLogging(LoggingConfig^ config) -> bool {
    if (log_handler_ == nullptr) {
      log_handler_ = gcnew LogHandler();
    }
    return log_handler_->ConfigureLogging(config);
  }

  auto EngineRunner::GetLoggingConfig(EngineContext^ ctx) -> LoggingConfig^ {
    if (log_handler_ == nullptr) {
      log_handler_ = gcnew LogHandler();
    }

    return log_handler_->GetCurrentConfig();
  }

  auto EngineRunner::ConfigureLogging(LoggingConfig^ config, Object^ logger)
    -> bool {
    if (log_handler_ == nullptr) {
      log_handler_ = gcnew LogHandler();
    }
    log_handler_->SetLogger(logger);
    return ConfigureLogging(config);
  }

} // namespace Oxygen::Interop
