// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Scripting;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>
/// Logging methods for <see cref="ScriptAnalysisService"/>.
/// </summary>
public sealed partial class ScriptAnalysisService
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "Scripts are linted only: the SDK has no API definitions at `{Path}`")]
    private partial void LogDefinitionsMissing(string path);

    [LoggerMessage(Level = LogLevel.Information, Message = "Analyzing {Count} scripts of `{ProjectRoot}`")]
    private partial void LogProjectScan(string projectRoot, int count);

    [LoggerMessage(Level = LogLevel.Error, Message = "Could not analyze the scripts of `{ProjectRoot}`")]
    private partial void LogProjectScanFailed(string projectRoot, Exception exception);

    [LoggerMessage(Level = LogLevel.Error, Message = "Could not analyze script `{Path}`")]
    private partial void LogScriptAnalysisFailed(string path, Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Input files are checked without the schema: the SDK has no `{FileName}`")]
    private partial void LogInputSchemaUnavailable(string fileName, Exception exception);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Could not read script `{Path}`; it is analyzed again on its next change")]
    private static partial void LogScriptUnreadable(ILogger logger, string path, Exception exception);

    [Conditional("DEBUG")]
    private void LogScriptUnreadable(string path, Exception exception)
        => LogScriptUnreadable(this.logger, path, exception);
}
