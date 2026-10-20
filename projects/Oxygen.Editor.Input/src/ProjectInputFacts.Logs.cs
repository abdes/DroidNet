// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// Logging helpers for <see cref="ProjectInputFacts"/>.
/// </summary>
internal sealed partial class ProjectInputFacts
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Checking {Count} input sources of `{ProjectRoot}`")]
    private static partial void LogProjectScan(ILogger logger, FileSystemPath projectRoot, int count);

    private void LogProjectScan(int count) => LogProjectScan(this.logger, this.Project.ProjectRoot, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Could not read input source `{Path}`; it is read again on its next change")]
    private static partial void LogSourceUnreadable(ILogger logger, FileSystemPath path, Exception exception);

    [Conditional("DEBUG")]
    private void LogSourceUnreadable(FileSystemPath path, Exception exception) => LogSourceUnreadable(this.logger, path, exception);
}
