// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>Stable codes for embedded runtime lifetime failures.</summary>
public static class RuntimeDiagnosticCodes
{
    /// <summary>The loop completed without a shutdown request.</summary>
    public const string LoopExited = DiagnosticCodes.RuntimePrefix + "LOOP_EXITED";

    /// <summary>The loop failed with an exception or unexpected cancellation.</summary>
    public const string LoopFaulted = DiagnosticCodes.RuntimePrefix + "LOOP_FAULTED";

    /// <summary>The engine could not be initialized or started.</summary>
    public const string StartFailed = DiagnosticCodes.RuntimePrefix + "START_FAILED";

    /// <summary>The engine was shut down while a project's preview used it.</summary>
    public const string Stopped = DiagnosticCodes.RuntimePrefix + "STOPPED";
}
