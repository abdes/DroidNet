// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
///     Logging methods for the <see cref="HomeView"/> class."/>.
/// </summary>
public partial class HomeView
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "An error occurred while opening project '{ProjectName}'.")]
    private static partial void LogFailedToOpenProject(ILogger logger, string projectName, Exception? ex);

    private void LogFailedToOpenProject(string projectName, Exception? ex = null)
        => LogFailedToOpenProject(this.logger, projectName, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to create a new project from template.")]
    private static partial void LogFailedToCreateProjectFromTemplate(ILogger logger);

    private void LogFailedToCreateProjectFromTemplate()
        => LogFailedToCreateProjectFromTemplate(this.logger);
}
