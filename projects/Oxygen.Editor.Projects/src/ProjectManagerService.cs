// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Provides methods for managing projects within the Oxygen Editor, including loading and saving project
///     information for projects which storage is provided by the <see cref="IStorageProvider" /> specified by
///     <paramref name="storage" />.
/// </summary>
/// <param name="storage">The <see cref="IStorageProvider" /> that can be used to access locally stored items.</param>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
/// <param name="atomicFiles">An optional atomic store override for this storage provider.</param>
public partial class ProjectManagerService(IStorageProvider storage, ILoggerFactory? loggerFactory = null, IAtomicFileStore? atomicFiles = null)
    : IProjectManagerService
{
    [SuppressMessage(
        "Performance",
        "CA1823:Avoid unused private fields",
        Justification = "used by generated logging methods")]
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectManagerService>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ProjectManagerService>();

    // Writes of one project file run one at a time, each against the version the one before it wrote.
    private readonly DocumentWriteCoordinator manifestWrites = new();

    /// <inheritdoc />
    public IProject? CurrentProject { get; private set; }

    private IAtomicFileStore AtomicFiles => atomicFiles ?? storage.AtomicFiles;

    /// <inheritdoc />
    public IStorageProvider GetCurrentProjectStorageProvider() => storage;

    /// <inheritdoc />
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "all failures are logged and propagated as null return value")]
    public async Task<IProjectInfo?> LoadProjectInfoAsync(string projectFolderPath)
    {
        try
        {
            var projectFolder = await storage.GetFolderFromPathAsync(projectFolderPath).ConfigureAwait(true);
            var projectFile = await projectFolder.GetDocumentAsync(Constants.ProjectFileName).ConfigureAwait(true);
            var json = await projectFile.ReadAllTextAsync().ConfigureAwait(true);
            var projectInfo = ProjectInfo.FromJson(json);
            _ = projectInfo?.Location = projectFolderPath;

            return projectInfo;
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProjectInfo(projectFolderPath, ex.Message);
        }

        return null;
    }

    /// <inheritdoc />
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "all failures are logged and propagated as false return value")]
    public async Task<bool> SaveProjectInfoAsync(IProjectInfo projectInfo)
    {
        Debug.Assert(projectInfo.Location != null, "The project location must be valid!");
        try
        {
            _ = await this.SaveProjectInfoCoreAsync(projectInfo, expected: null, CancellationToken.None).ConfigureAwait(true);
            return true;
        }
        catch (Exception error)
        {
            this.CouldNotSaveProjectInfo(projectInfo.Location, error.Message);
        }

        return false;
    }

    /// <inheritdoc />
    public Task SaveProjectInfoAsync(IProjectInfo projectInfo, IProjectInfo expected, CancellationToken cancellationToken)
        => this.SaveProjectInfoCoreAsync(projectInfo, expected, cancellationToken);

    /// <inheritdoc />
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "all failures are logged and propagated as false return value")]
    public async Task<bool> LoadProjectAsync(IProjectInfo projectInfo)
    {
        var staged = await this.StageProjectLoadAsync(projectInfo).ConfigureAwait(true);
        if (staged is null)
        {
            return false;
        }

        _ = this.AcceptProjectLoad(staged);
        return true;
    }

    /// <inheritdoc/>
    public Task<ProjectLoadSnapshot?> StageProjectLoadAsync(IProjectInfo projectInfo, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(projectInfo);
        cancellationToken.ThrowIfCancellationRequested();
        Debug.Assert(projectInfo.Location is not null, "should not load a project with an invalid project info");

        if (projectInfo.Location == null)
        {
            this.CouldNotLoadProject("__null__", "cannot not load project from `null` location");
            return Task.FromResult<ProjectLoadSnapshot?>(null);
        }

        var previousProject = this.CurrentProject;
        var project = new Project(projectInfo);
        return Task.FromResult<ProjectLoadSnapshot?>(new(this, project, previousProject));
    }

    /// <inheritdoc/>
    public bool IsProjectLoadCurrent(ProjectLoadSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        return ReferenceEquals(snapshot.Owner, this) && !snapshot.Accepted
            && ReferenceEquals(snapshot.PreviousProject, this.CurrentProject);
    }

    /// <inheritdoc/>
    public IProject AcceptProjectLoad(ProjectLoadSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (!this.IsProjectLoadCurrent(snapshot))
        {
            throw new InvalidOperationException("The staged project is not available for activation.");
        }

        snapshot.Accepted = true;
        return this.CurrentProject = snapshot.Project;
    }

}
