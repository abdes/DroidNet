// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ProjectBrowser.Templates;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
///     Logging methods for the <see cref="NewProjectViewModel"/> class.
/// </summary>
public partial class NewProjectViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload templates during ViewModel activation")]
    private static partial void LogPreloadingTemplatesError(ILogger logger, Exception ex);

    private void LogPreloadingTemplatesError(Exception ex)
        => LogPreloadingTemplatesError(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Creating new project from template '{TemplateCategory}/{TemplateName}', with name '{ProjectName}', in location '{Location}'.")]
    private static partial void LogNewProjectFromTemplate(ILogger logger, string templateCategory, string templateName, string projectName, string location);

    private void LogNewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
        => LogNewProjectFromTemplate(this.logger, template.Category.Name, template.Name, projectName, location);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create new project from template.")]
    private static partial void LogNewProjectFailed(ILogger logger, Exception? ex);

    private void LogNewProjectFailed(Exception? ex = null)
        => LogNewProjectFailed(this.logger, ex);
}
