// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Storage;

using System.Diagnostics;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Oxygen.Editor.Projects.Config;
using Oxygen.Editor.Projects.Utils;
using Oxygen.Editor.Storage;

/// <summary>
/// A <see cref="IProjectSource">project source</see> that can handle projects stored in the local storage.
/// </summary>
public partial class LocalProjectsSource : IProjectSource
{
    private readonly JsonSerializerOptions jsonOptions;

    private readonly ILogger<LocalProjectsSource> logger;
    private readonly IStorageProvider storage;

    /// <summary>
    /// Initializes a new instance of the <see cref="LocalProjectsSource" /> class.
    /// </summary>
    /// <param name="localStorage">
    /// The <see cref="IStorageProvider" /> that can be used to access locally stored items.
    /// </param>
    /// <param name="logger">
    /// A logger that can be used by this class when creating logs.
    /// </param>
    /// <param name="settings">
    /// The configuration settings for this module.
    /// </param>
    public LocalProjectsSource(
        IStorageProvider localStorage,
        ILogger<LocalProjectsSource> logger,
        IOptions<ProjectsSettings> settings)
    {
        this.logger = logger;
        this.storage = localStorage;
        this.jsonOptions = new JsonSerializerOptions
        {
            AllowTrailingCommas = true,
            Converters = { new CategoryJsonConverter(settings.Value) },
            WriteIndented = true,
        };
    }

    public async Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath)
    {
        // Check if the project location still exists before adding it (maybe the project was deleted)
        try
        {
            var projectFolder = await this.storage.GetFolderFromPathAsync(projectFolderPath).ConfigureAwait(false);
            var projectFile = await projectFolder.GetDocumentAsync(Constants.ProjectFileName)
                .ConfigureAwait(false);
            var json = await projectFile.ReadAllTextAsync().ConfigureAwait(false);
            return JsonSerializer.Deserialize<ProjectInfo>(json, this.jsonOptions);
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProjectInfo(projectFolderPath, ex.Message);
        }

        return null;
    }

    public async Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo)
    {
        Debug.Assert(projectInfo.Location != null, "The project location must be valid!");
        try
        {
            var json = JsonSerializer.Serialize(projectInfo, this.jsonOptions);

            var documentPath = this.storage.NormalizeRelativeTo(
                projectInfo.Location,
                Constants.ProjectFileName);
            var document = await this.storage.GetDocumentFromPathAsync(documentPath).ConfigureAwait(false);

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
