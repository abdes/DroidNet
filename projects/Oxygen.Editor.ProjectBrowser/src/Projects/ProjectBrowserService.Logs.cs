// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.Projects;

/// <summary>
///     Logging methods for the <see cref="ProjectBrowserService"/> class.
/// </summary>
public partial class ProjectBrowserService
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
        Level = LogLevel.Debug,
        Message = "Cannot create a new project named '{ProjectName}' at location '{Location}'.")]
    private static partial void LogCannotCreateNewProject(ILogger logger, string projectName, string location);

    [Conditional("DEBUG")]
    private void LogCannotCreateNewProject(string projectName, string location)
        => LogCannotCreateNewProject(this.logger, projectName, location);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Invalid template location: '{Location}'.")]
    private static partial void LogInvalidTemplateLocation(ILogger logger, string location);

    [Conditional("DEBUG")]
    private void LogInvalidTemplateLocation(string location)
        => LogInvalidTemplateLocation(this.logger, location);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Project creation failed.")]
    private static partial void LogProjectCreationFailed(ILogger logger, Exception ex);

    private void LogProjectCreationFailed(Exception ex)
        => LogProjectCreationFailed(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to patch {ManifestFile} at path '{Path}'.")]
    private static partial void LogFailedToPatchProjectManifest(ILogger logger, string manifestFile, string path, Exception ex);

    private void LogFailedToPatchProjectManifest(string? path, Exception ex)
        => LogFailedToPatchProjectManifest(this.logger, Constants.ProjectFileName, path ?? "<null>", ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to open project.")]
    private static partial void LogOpenProjectFailed(ILogger logger, Exception ex);

    private void LogOpenProjectFailed(Exception ex)
        => LogOpenProjectFailed(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to update the last save location.")]
    private static partial void LogFailedToUpdateLastSaveLocation(ILogger logger, Exception ex);

    private void LogFailedToUpdateLastSaveLocation(Exception ex)
        => LogFailedToUpdateLastSaveLocation(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to update template usage for '{TemplateLocation}'.")]
    private static partial void LogFailedToUpdateTemplateUsage(ILogger logger, string templateLocation, Exception ex);

    private void LogFailedToUpdateTemplateUsage(string templateLocation, Exception ex)
        => LogFailedToUpdateTemplateUsage(this.logger, templateLocation, ex);
}
