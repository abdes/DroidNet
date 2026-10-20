// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// Logging methods for <see cref="InputFacts"/> and its <see cref="ProjectInputFacts"/>.
/// </summary>
public sealed partial class InputFacts
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Checking {Count} input sources of `{ProjectRoot}`")]
    internal static partial void LogProjectScan(ILogger logger, FileSystemPath projectRoot, int count);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Could not read input source `{Path}`; it is read again on its next change")]
    internal static partial void LogSourceUnreadable(ILogger logger, FileSystemPath path, Exception exception);

    [LoggerMessage(Level = LogLevel.Error, Message = "Could not read the input sources of `{ProjectRoot}`")]
    private partial void LogProjectScanFailed(FileSystemPath projectRoot, Exception exception);
}
