//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/logging.h>
#include <vcclr.h>

using System::IntPtr;
using System::String;
using namespace System::Runtime::InteropServices;

namespace Oxygen::Interop::Logging {

public
ref class MessageWrapper {
public:
    property int Verbosity;
    property String ^ Filename;
    property unsigned Line;
    property String ^ Preamble;
    property String ^ Indentation;
    property String ^ Prefix;
    property String ^ Message;
};

// Declare the cdecl function that will be used
void cdecl_log_handler(void* user_data, const loguru::Message& message);

public
ref class Loguru {
public:
    // The BufferAllocator delegate declaration, available to any clr language
    // [In, Out] attributes needed (?) to pass the pointer as reference
    delegate void LogHandler(MessageWrapper ^ buffer);

    enum class Verbosity : int {
        // Prefer to use ABORT_F or ABORT_S over LOG_F(FATAL) or LOG_S(FATAL).
        Verbosity_FATAL = -3,
        Verbosity_ERROR = -2,
        Verbosity_WARNING = -1,

        // Normal messages. By default written to stderr.
        Verbosity_INFO = 0,

        // Same as Verbosity_INFO in every way.
        Verbosity_0 = 0,

        // Verbosity levels 1-9 are generally not written to stderr, but are written
        // to file.
        Verbosity_1 = +1,
        Verbosity_2 = +2,
        Verbosity_3 = +3,
        Verbosity_4 = +4,
        Verbosity_5 = +5,
        Verbosity_6 = +6,
        Verbosity_7 = +7,
        Verbosity_8 = +8,
        Verbosity_9 = +9,
    };

    internal :
        // The stored delegate ref to be used later
        LogHandler
        ^ _handle_log;

private:
    // Native handle of the ref Library class, castable to void *
    gcroot<Loguru ^>* _native_handle;

public:
    Loguru()
    {
        // Construct the native handle
        _native_handle = new gcroot<Loguru ^>(this);
        // Null the _handle_log delegate instance
        _handle_log = nullptr;
    }

    ~Loguru()
    {
        delete _native_handle;
    }

    // The clr callback setter equivalent to the C counterpart, don't need
    // the context because in CLR we have closures
    void AddLogHandlerCallback(LogHandler ^ handleLog, Verbosity verbosity)
    {
        if (verbosity < Verbosity::Verbosity_FATAL || verbosity > Verbosity::Verbosity_9) {
            throw gcnew System::ArgumentOutOfRangeException("verbosity", "Verbosity value is out of range.");
        }

        _handle_log = handleLog;
        // Call the C lib callback setter. Use _native_handle pointer as the opaque data
        loguru::add_callback("interop", cdecl_log_handler, _native_handle, (loguru::Verbosity)verbosity);
    }

    void RemoveLogHandlerCallback()
    {
        // Call the C lib callback remover. Use _native_handle pointer as the opaque data
        loguru::remove_callback("interop");
        _handle_log = nullptr;
    }

    void LogInfo(String ^ message) { LogMessage(Verbosity::Verbosity_INFO, message); }
    void LogError(String ^ message) { LogMessage(Verbosity::Verbosity_ERROR, message); }
    void LogWarning(String ^ message) { LogMessage(Verbosity::Verbosity_WARNING, message); }
    void LogMessage(Verbosity level, String ^ message)
    {
        // Convert the managed string to a native string
        const char* native_message = (const char*)(Marshal::StringToHGlobalAnsi(message)).ToPointer();
        // Log the message
        loguru::log((loguru::Verbosity)level, __FILE__, __LINE__, native_message);
        // Free the native string
        Marshal::FreeHGlobal(IntPtr((void*)native_message));
    }
};
} // namespace Oxygen::Interop::Logging
