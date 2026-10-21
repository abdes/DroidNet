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

    private void LogImportToolInvoked(string toolPath, string manifestPath, string workingDirectory)
        => LogImportToolInvoked(this.logger, toolPath, manifestPath, workingDirectory);
}
