// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Classifies native interop exceptions that can be reported as live-sync failures.
/// </summary>
internal static class EngineInteropExceptionPolicy
{
    /// <summary>
    /// Returns <see langword="true"/> when the exception is an expected engine interop failure.
    /// </summary>
    /// <param name="exception">The exception to classify.</param>
    /// <returns><see langword="true"/> when live sync can report and continue.</returns>
    public static bool IsRecoverable(Exception exception)
        => exception is ArgumentException
            or InvalidOperationException
            or NotImplementedException
            or NotSupportedException
            or TimeoutException
            or System.Runtime.InteropServices.COMException;
}
