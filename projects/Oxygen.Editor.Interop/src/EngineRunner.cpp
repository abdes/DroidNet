//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Clean, instance-based implementation
//===----------------------------------------------------------------------===//

#include "EngineRunner.h"

#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>
#include <vcclr.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/EditorInterface/Api.h>

using namespace System;              // NOLINT
using namespace System::Diagnostics; // NOLINT

// Map loguru verbosity to Microsoft.Extensions.Logging.LogLevel integer.
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

// Formatter helper
private
ref class ManagedLogFormatter abstract
sealed{public : static auto Format(Object ^ s, Exception ^)->String ^
       {return s == nullptr ? String::Empty : s->ToString();
}
}
;

// Forwarder thunk structure
struct OxygenEditorForwarder {
  static auto Forward(void *user_data, const loguru::Message &msg) -> void {
    try {
      if (!user_data) {
        return;
      }
      Runtime::InteropServices::GCHandle handle =
          Runtime::InteropServices::GCHandle::FromIntPtr(IntPtr(user_data));
      if (!handle.IsAllocated) {
        return;
      }
      auto runner = safe_cast<Oxygen::Editor::EngineInterface::EngineRunner ^>(
          handle.Target);
      if (runner != nullptr) {
        runner->HandleLog(msg);
      }
    } catch (...) { /* swallow */
    }
  }
};

namespace Oxygen::Editor::EngineInterface {

EngineRunner::EngineRunner()
    : _logger(nullptr), _logMethod(nullptr), _formatterDelegate(nullptr),
      _callbackRegistered(false), _selfHandle(IntPtr::Zero), _disposed(false) {
  Runtime::InteropServices::GCHandle h =
      Runtime::InteropServices::GCHandle::Alloc(
          this, Runtime::InteropServices::GCHandleType::WeakTrackResurrection);
  _selfHandle = Runtime::InteropServices::GCHandle::ToIntPtr(h);
}

EngineRunner::~EngineRunner() {
  if (!_disposed) {
    ReleaseCallback();
    _disposed = true;
  }
}

EngineRunner::!EngineRunner() { ReleaseCallback(); }

auto EngineRunner::ReleaseCallback() -> void {
  if (_callbackRegistered) {
    loguru::remove_callback("OxygenEditorManagedLogger");
    _callbackRegistered = false;
  }
  if (_selfHandle != IntPtr::Zero) {
    Runtime::InteropServices::GCHandle h =
        Runtime::InteropServices::GCHandle::FromIntPtr(_selfHandle);
    if (h.IsAllocated) {
      h.Free();
    }
    _selfHandle = IntPtr::Zero;
  }
}

auto EngineRunner::RegisterCallbackIfNeeded() -> void {
  if (_callbackRegistered) {
    return;
  }
  loguru::add_callback("OxygenEditorManagedLogger",
                       &OxygenEditorForwarder::Forward, _selfHandle.ToPointer(),
                       loguru::Verbosity_MAX);
  _callbackRegistered = true;
}

auto EngineRunner::CacheLoggerArtifacts() -> void {
  _logMethod = nullptr;
  _formatterDelegate = nullptr;
  if (_logger == nullptr) {
    return;
  }
  try {
    auto loggerType = _logger->GetType();
    for each (Reflection::MethodInfo ^ mi in loggerType->GetMethods()) {
      if (mi->Name != "Log") {
        continue;
      }
      auto ps = mi->GetParameters();
      if (ps->Length != 5) {
        continue;
      }
      if (!ps[0]->ParameterType->IsEnum) {
        continue;
      }
      auto formatMethod = ManagedLogFormatter::typeid->GetMethod("Format");
      auto del = Delegate::CreateDelegate(ps[4]->ParameterType, formatMethod);
      _logMethod = mi;
      _formatterDelegate = del;
      break;
    }
  } catch (...) {
    _logMethod = nullptr;
    _formatterDelegate = nullptr;
  }
}

auto EngineRunner::HandleLog(const loguru::Message &message) -> void {
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
    if (_logger == nullptr) {
      Debug::WriteLine(managedMsg);
      return;
    }
#else
    if (_logger == nullptr)
      return;
#endif
    if (_logMethod != nullptr) {
      int lvlValue = MapVerbosityToManagedLevel(message.verbosity);
      auto ps = _logMethod->GetParameters();
      Object ^ levelBoxed = Enum::ToObject(ps[0]->ParameterType, lvlValue);
      Object ^ eventId = Activator::CreateInstance(ps[1]->ParameterType);
      Object ^ state = managedMsg;
      Object ^ exception = nullptr;
      auto args = gcnew array<Object ^>(5){levelBoxed, eventId, state,
                                           exception, _formatterDelegate};
      _logMethod->Invoke(_logger, args);
      return;
    }
#if defined(_DEBUG) || !defined(NDEBUG)
    Debug::WriteLine(managedMsg);
#endif
  } catch (...) { /* swallow */
  }
}

auto EngineRunner::ConfigureLogging(LoggingConfig ^ config) -> bool {
  namespace op = oxygen::engine::interop;
  op::LoggingConfig native_config{};
  native_config.verbosity = config->Verbosity;
  native_config.is_colored = config->IsColored;
  native_config.vmodules = nullptr;
  std::string vmodules;
  if (config->ModuleOverrides != nullptr) {
    vmodules = msclr::interop::marshal_as<std::string>(config->ModuleOverrides);
    if (!vmodules.empty()) {
      native_config.vmodules = vmodules.c_str();
    }
  }
  bool ok = op::ConfigureLogging(native_config);
  if (ok) {
    RegisterCallbackIfNeeded();
  }
  // Log an INFO message, used for testing and also nice to have for the status.
  op::LogInfoMessage("Oxygen Editor logging configured.");
  return ok;
}

auto EngineRunner::ConfigureLogging(LoggingConfig ^ config, Object ^ logger)
    -> bool {
  _logger = logger;
  CacheLoggerArtifacts();
  return ConfigureLogging(config);
}

auto EngineRunner::CreateEngine(EngineConfig ^) -> bool { return false; }

} // namespace Oxygen::Editor::EngineInterface
