// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Logging helpers for <see cref="ScriptFacts"/>.
/// </summary>
public sealed partial class ScriptFacts
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Scripts are linted only: the SDK has no API definitions at `{Path}`")]
    private static partial void LogDefinitionsMissing(ILogger logger, string path);

    private void LogDefinitionsMissing(string path) => LogDefinitionsMissing(this.logger, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Could not read library script `{Script}`")]
    private static partial void LogLibraryScriptUnreadable(ILogger logger, AssetPath script, Exception exception);

    private void LogLibraryScriptUnreadable(AssetPath script, Exception exception) => LogLibraryScriptUnreadable(this.logger, script, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "The script analyzer cannot run: {Reason}")]
    private static partial void LogAnalyzerUnavailable(ILogger logger, string reason);

    private void LogAnalyzerUnavailable(string reason) => LogAnalyzerUnavailable(this.logger, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not analyze the scripts of `{ProjectRoot}`")]
    private static partial void LogProjectScanFailed(ILogger logger, FileSystemPath projectRoot, Exception exception);

    private void LogProjectScanFailed(FileSystemPath projectRoot, Exception exception) => LogProjectScanFailed(this.logger, projectRoot, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not analyze script `{Script}`")]
    private static partial void LogScriptAnalysisFailed(ILogger logger, AssetPath script, Exception exception);

    private void LogScriptAnalysisFailed(AssetPath script, Exception exception) => LogScriptAnalysisFailed(this.logger, script, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not bring the facts of {Count} script sources up to date")]
    private static partial void LogScriptCheckFailed(ILogger logger, int count, Exception exception);

    private void LogScriptCheckFailed(int count, Exception exception) => LogScriptCheckFailed(this.logger, count, exception);
}
