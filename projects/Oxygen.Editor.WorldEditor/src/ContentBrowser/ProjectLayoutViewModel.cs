// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

public partial class ProjectLayoutViewModel(IProjectManagerService projectManager, ILoggerFactory? loggerFactory) : DynamicTreeViewModel, IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectLayoutViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<ProjectLayoutViewModel>();

    private FolderTreeItemAdapter? projectRoot;

    /// <inheritdoc/>
    public Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext) => this.PreloadRecentTemplatesAsync();

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    private async Task PreloadRecentTemplatesAsync()
    {
        if (this.projectRoot is not null)
        {
            return;
        }

        try
        {
            // The following method will do sanity checks on the current project and its info. On successful return, we have
            // guarantee the project info is valid and has a valid location for the project root folder.
            var projectInfo = this.GetCurrentProjectInfo() ?? throw new InvalidOperationException("Project Layout used with no CurrentProject");

            // Create the root TreeItem for the project root folder.
            var storage = projectManager.GetCurrentProjectStorageProvider();
            var folder = storage.GetFolderFromPathAsync(projectInfo.Location!);
            this.projectRoot = new FolderTreeItemAdapter(this.logger, folder, projectInfo.Name, isRoot: true)
            {
                IsExpanded = true,
            };

            // Preload the project folders
            await this.LoadProjectAsync().ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            this.LogPreloadingProjectFoldersError(ex);
        }
    }

    /// <summary>
    /// Loads the project asynchronously, starting with the project root folder, and continuing with children that are part of the
    /// initial selection set. The selection set can be provided via the navigation URL as query parameters.
    /// </summary>
    /// <returns>
    /// A <see cref="Task" /> object representing the asynchronous work.
    /// </returns>
    [RelayCommand]
    private async Task LoadProjectAsync()
    {
        Debug.Assert(this.projectRoot is not null, "project root node should be initialized");

        // We will expand the entire project tree
        await this.InitializeRootAsync(this.projectRoot, skipRoot: false).ConfigureAwait(true);

        // TODO: Then we will check the selected items in the ActiveRoute query params
    }

    private IProjectInfo? GetCurrentProjectInfo()
    {
        var projectInfo = projectManager.CurrentProject?.ProjectInfo;

        if (projectInfo is null)
        {
            this.LogNoCurrentProject();
        }
#if DEBUG
        else
        {
            Debug.Assert(
                projectInfo.Location is not null,
                "current project must be set, have a valid ProjectInfo and a valid Location");
        }
#endif

        return projectInfo;
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "The project manager service does not have a currently loaded project.")]
    private partial void LogNoCurrentProject();

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload project folders during ViewModel activation.")]
    private partial void LogPreloadingProjectFoldersError(Exception ex);
}
