// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Projects;

using System.Diagnostics;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.ProjectBrowser.Config;
using Oxygen.Editor.ProjectBrowser.Utils;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;
using Windows.Storage;

/// <summary>
/// A projects source that can handle projects stored in the local
/// filesystem.
/// </summary>
public partial class LocalProjectsSource : IProjectSource
{
    private const string ProjectFileName = "Project.oxy";

    private readonly JsonSerializerOptions jsonSerializerOptions;
    private readonly NativeStorageProvider localStorage;

    private readonly ILogger<LocalProjectsSource> logger;

    public LocalProjectsSource(
        NativeStorageProvider localStorage,
        ILogger<LocalProjectsSource> logger,
        IPathFinder pathFinder,
        IOptions<ProjectBrowserSettings> settings)
    {
        this.localStorage = localStorage;
        this.logger = logger;

        this.CommonProjectLocations =
        [
            pathFinder.PersonalProjects, // Typically a Projects folder in the local app data folder
            Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), // The user's My Documents folder
        ];

        this.jsonSerializerOptions = new JsonSerializerOptions
        {
            AllowTrailingCommas = true,
            Converters = { new CategoryJsonConverter(settings.Value) },
            WriteIndented = true,
        };
    }

    public string[] CommonProjectLocations { get; }

    public async Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath)
    {
        // Check if the project location still exists before adding it (maybe the project was deleted)
        try
        {
            var projectFolder = await this.localStorage.GetFolderFromPathAsync(projectFolderPath).ConfigureAwait(false);
            var projectFile = await projectFolder.GetDocumentAsync(ProjectFileName)
                .ConfigureAwait(false);
            var json = await projectFile.ReadAllTextAsync().ConfigureAwait(false);
            return JsonSerializer.Deserialize<ProjectInfo>(json, this.jsonSerializerOptions);
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProjectInfo(projectFolderPath, ex.Message);
        }

        return null;
    }

    public async Task<IFolder?> CreateNewProjectFolderAsync(string projectName, string atLocationPath)
    {
        try
        {
            var containingFolder = await StorageFolder.GetFolderFromPathAsync(Path.GetFullPath(atLocationPath));
            var projectFolder = await containingFolder.CreateFolderAsync(
                projectName,
                CreationCollisionOption.OpenIfExists);
            if ((await projectFolder.GetItemsAsync()).Count != 0)
            {
                Debug.WriteLine($"Project folder exists and is not empty: {projectFolder.Path}");
                return null;
            }

            return await this.localStorage.GetFolderFromPathAsync(projectFolder.Path).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Could not create folder for project `{projectName}` at `{atLocationPath}`: {ex.Message}");
        }

        return null;
    }

    public bool CanCreateProject(string projectName, string atLocationPath)
    {
        // Containing folder must exist
        if (!Directory.Exists(atLocationPath))
        {
            return false;
        }

        // Project folder should not exist, but if it does, it should be empty
        var projectFolderPath = Path.Combine(atLocationPath, projectName);
        return !Directory.Exists(projectFolderPath) || !Directory.EnumerateFileSystemEntries(projectFolderPath)
            .Any();
    }

    public async Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo)
    {
        Debug.Assert(projectInfo.Location != null, "The project location must be valid!");
        try
        {
            var json = JsonSerializer.Serialize(projectInfo, this.jsonSerializerOptions);

            var documentPath = this.localStorage.NormalizeRelativeTo(projectInfo.Location, ProjectFileName);
            var document = await this.localStorage.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);

            await document.WriteAllTextAsync(json).ConfigureAwait(true);
            return true;
        }
        catch (Exception error)
        {
            this.CouldNotSaveProjectInfo(projectInfo.Location, error.Message);
        }

        return false;
    }

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotSaveProjectInfo(string location, string error);

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Could not load project info from `{location}`; {error}")]
    partial void CouldNotLoadProjectInfo(string location, string error);
}
