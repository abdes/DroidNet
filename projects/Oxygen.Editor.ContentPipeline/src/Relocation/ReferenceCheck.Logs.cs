// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Logging helpers for <see cref="ReferenceCheck"/>.</summary>
/// <remarks>Levels: Information for each check, with what it found and the file it wrote; Warning for a check that could
/// not read or write the project's files.</remarks>
public sealed partial class ReferenceCheck
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Reference check of project '{ProjectRoot}': {Count} unresolved references, listed in '{File}'.")]
    private static partial void LogChecked(ILogger logger, string projectRoot, int count, string file);

    private void LogChecked(FileSystemPath projectRoot, int count, string file)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogChecked(this.logger, projectRoot.FullPath, count, file);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Reference check of project '{ProjectRoot}' failed; the Problems pane keeps the last check's references.")]
    private static partial void LogCheckFailed(ILogger logger, Exception failure, string projectRoot);

    private void LogCheckFailed(Exception failure)
    {
        if (this.logger.IsEnabled(LogLevel.Warning))
        {
            LogCheckFailed(this.logger, failure, this.session.Context.ProjectRoot.FullPath);
        }
    }
}
