// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Logging helpers for <see cref="ImportToolContentPipelineApi"/>.</summary>
public sealed partial class ImportToolContentPipelineApi
{
    [LoggerMessage(
        EventId = 1001,
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Invoking ImportTool '{ToolPath}' with manifest '{ManifestPath}' in '{WorkingDirectory}'.")]
    private static partial void LogImportToolInvoked(ILogger logger, string toolPath, string manifestPath, string workingDirectory);

    [LoggerMessage(
        EventId = 1002,
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "ImportTool '{ToolPath}' cooks with importers {Importers}.")]
    private static partial void LogImporterRevisions(ILogger logger, string toolPath, string importers);

    private void LogImportToolInvoked(string toolPath, string manifestPath, string workingDirectory)
        => LogImportToolInvoked(this.logger, toolPath, manifestPath, workingDirectory);

    private void LogImporterRevisions(string toolPath, Incremental.ImporterRevisions revisions)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogImporterRevisions(this.logger, toolPath, string.Join(", ", revisions.Revisions.OrderBy(static entry => entry.Key, StringComparer.Ordinal).Select(static entry => $"{entry.Key} {entry.Value}")));
        }
    }
}
