// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ProjectExplorer.Projects;

/// <summary>Logging helpers for <see cref="ProjectExplorerService"/>.</summary>
public partial class ProjectExplorerService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Creating a new project from template: {Category}/{Template} with name '{ProjectName}' at location '{Location}'.")]
    private static partial void LogNewProjectFromTemplate(ILogger logger, string category, string template, string projectName, string location);

    [Conditional("DEBUG")]
    private void LogNewProjectFromTemplate(string category, string template, string projectName, string location)
        => LogNewProjectFromTemplate(this.logger, category, template, projectName, location);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to update the last save location.")]
    private static partial void LogFailedToUpdateLastSaveLocation(ILogger logger, Exception ex);

    private void LogFailedToUpdateLastSaveLocation(Exception ex)
        => LogFailedToUpdateLastSaveLocation(this.logger, ex);
}
