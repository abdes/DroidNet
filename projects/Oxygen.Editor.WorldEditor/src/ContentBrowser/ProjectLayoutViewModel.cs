// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.WorldEditor.ContentBrowser.Routing;

public partial class ProjectLayoutViewModel : DynamicTreeViewModel
{
    private readonly ILogger logger;
    private readonly ILocalRouterContext routerContext;

    public ProjectLayoutViewModel(
        IProjectManagerService projectManager,
        ILocalRouterContext routerContext,
        ILoggerFactory? loggerFactory)
    {
        this.routerContext = routerContext;
        this.logger = loggerFactory?.CreateLogger<ProjectLayoutViewModel>() ??
                      NullLoggerFactory.Instance.CreateLogger<ProjectLayoutViewModel>();

        // The following method will do sanity checks on the current project and its info. On successful return, we have
        // guarantee the project info is valid and has a valid location for the project root folder.
        var projectInfo = this.GetCurrentProjectInfo(projectManager) ??
                          throw new InvalidOperationException("Project Layout used with no CurrentProject");

        // Create the root TreeItem for the project root folder.
        var storage = projectManager.GetCurrentProjectStorageProvider();
        var folder = storage.GetFolderFromPathAsync(projectInfo.Location!);
        this.ProjectRoot = new FolderTreeItemAdapter(this.logger, folder, projectInfo.Name)
        {
            IsExpanded = true,
            Depth = 0,
        };
    }

    private FolderTreeItemAdapter ProjectRoot { get; }

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
        // We will expand the entire project tree
        this.ProjectRoot.IsExpanded = true;
        await this.InitializeRootAsync(this.ProjectRoot, skipRoot: false).ConfigureAwait(false);

        // TODO: Then we will check the selected items in the ActiveRoute query params
    }

    private IProjectInfo? GetCurrentProjectInfo(IProjectManagerService projectManager)
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
                projectInfo?.Location is not null,
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
}
