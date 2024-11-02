// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;
using Oxygen.Editor.WorldEditor.ContentBrowser.Routing;

/// <summary>
/// The ViewModel for the <see cref="ProjectLayoutView" /> view.
/// </summary>
public partial class ProjectLayoutViewModel : ObservableObject, IRoutingAware
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

        if (projectManager.CurrentProject is null)
        {
            this.LogNoCurrentProject();
            return;
        }

        var currentProjectInfo = projectManager.CurrentProject.ProjectInfo;
        Debug.Assert(
            projectManager.CurrentProject.ProjectInfo is not null,
            "current project must have a valid location");

        Debug.Assert(
            currentProjectInfo.Location is not null,
            "current project must have a valid location");

        var storage = projectManager.GetCurrentProjectStorageProvider();

        this.ProjectRoot.Add(
            new Folder(currentProjectInfo.Name + " (Project)", currentProjectInfo.Location, storage, this.logger));

        this.routerContext.Router.Events.Subscribe(@event => Debug.WriteLine(@event.ToString()));
    }

    public IActiveRoute? ActiveRoute { get; set; }

    internal ObservableCollection<Folder> ProjectRoot { get; } = [];

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Project `{projectName}` does not have the proper layout. It has no folders!")]
    private partial void LogProjectLayoutInvalid(string projectName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "The project manager service does not have a currently loaded project.")]
    private partial void LogNoCurrentProject();
}

/// <summary>
/// Represents a folder item in the <see cref="ProjectLayoutView" /> tree control.
/// </summary>
/// <param name="name">The folder name.</param>
/// <param name="location">The path at which the folder is located.</param>
/// <param name="storage">The <see cref="IStorageProvider" /> which cam be used to access the physical storage folder.</param>
/// <param name="logger">An <see cref="ILogger" /> that should be used by this class.</param>
internal sealed partial class Folder(string name, string location, IStorageProvider? storage, ILogger logger)
    : ObservableObject
{
    private readonly ObservableCollection<Folder> children = [];

    private bool loaded;

    [ObservableProperty]
    private string location = location;

    [ObservableProperty]
    private string name = name;

    public ObservableCollection<Folder> Children
    {
        get
        {
            if (this.loaded)
            {
                return this.children;
            }

            this.LoadProjectFoldersAsync().GetAwaiter().GetResult();
            this.loaded = true;

            return this.children;
        }
    }

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "An error occurred while loading project folders from `{location}`: {error}")]
    private static partial void CouldNotLoadProjectFolders(ILogger logger, string location, string error);

    private async Task LoadProjectFoldersAsync()
    {
        if (storage is null)
        {
            return;
        }

        var storageFolder = await storage.GetFolderFromPathAsync(this.Location).ConfigureAwait(false);
        await foreach (var child in storageFolder.GetFoldersAsync().ConfigureAwait(false))
        {
            try
            {
                this.children.Add(new Folder(child.Name, child.Location, storage, logger));
            }
            catch (Exception ex)
            {
                CouldNotLoadProjectFolders(logger, this.Location, ex.Message);
            }
        }
    }
}
