// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Logging helpers for <see cref="ProjectScriptFacts"/>.
/// </summary>
internal sealed partial class ProjectScriptFacts
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Analyzing {Count} scripts of `{ProjectRoot}`")]
    private static partial void LogProjectScan(ILogger logger, FileSystemPath projectRoot, int count);

    private void LogProjectScan(int count) => LogProjectScan(this.logger, this.Project.ProjectRoot, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not analyze script `{Path}`; the scan goes on with the other scripts")]
    private static partial void LogScriptScanFailed(ILogger logger, FileSystemPath path, Exception exception);

    private void LogScriptScanFailed(FileSystemPath path, Exception exception) => LogScriptScanFailed(this.logger, path, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Could not read script `{Path}`; it is analyzed again on its next change")]
    private static partial void LogScriptUnreadable(ILogger logger, FileSystemPath path, Exception exception);

    [Conditional("DEBUG")]
    private void LogScriptUnreadable(FileSystemPath path, Exception exception) => LogScriptUnreadable(this.logger, path, exception);
}
