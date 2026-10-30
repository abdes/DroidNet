// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Storage-backed implementation of project validation.
/// </summary>
/// <param name="storage">The storage provider.</param>
/// <param name="fileSystem">The file system the mount table is resolved on.</param>
public sealed class ProjectValidationService(IStorageProvider storage, IFileSystem fileSystem) : IProjectValidationService
{
    /// <inheritdoc/>
    public async Task<ProjectValidationResult> ValidateAsync(
        string projectFolderPath,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectFolderPath);

        var projectRoot = projectFolderPath;
        try
        {
            return await this.ValidateCoreAsync(projectFolderPath, cancellationToken).ConfigureAwait(false);
        }
        catch (UnsupportedProjectSchemaException ex)
        {
            projectRoot = storage.Normalize(projectFolderPath);
            return ProjectValidationResult.Failure(
                ProjectValidationState.UnsupportedVersion,
                projectRoot,
                ex.Message,
                ex);
        }
        catch (JsonException ex)
        {
            projectRoot = storage.Normalize(projectFolderPath);
            return ProjectValidationResult.Failure(
                ProjectValidationState.InvalidManifest,
                projectRoot,
                ex.Message,
                ex);
        }
        catch (Exception ex) when (this.IsMissing(ex))
        {
            return ProjectValidationResult.Failure(
                ProjectValidationState.Missing,
                projectRoot,
                "Project folder does not exist.",
                ex);
        }
        catch (Exception ex) when (this.IsInaccessible(ex))
        {
            return ProjectValidationResult.Failure(
                ProjectValidationState.Inaccessible,
                projectRoot,
                ex.Message,
                ex);
        }
    }

    private async Task<ProjectValidationResult> ValidateCoreAsync(
        string projectFolderPath,
        CancellationToken cancellationToken)
    {
        var projectRoot = storage.Normalize(projectFolderPath);
        if (!await storage.FolderExistsAsync(projectRoot).ConfigureAwait(false))
        {
            return ProjectValidationResult.Failure(
                ProjectValidationState.Missing,
                projectRoot,
                "Project folder does not exist.");
        }

        var projectFilePath = storage.NormalizeRelativeTo(projectRoot, Constants.ProjectFileName);
        if (!await storage.DocumentExistsAsync(projectFilePath).ConfigureAwait(false))
        {
            return ProjectValidationResult.Failure(
                ProjectValidationState.NotAProject,
                projectRoot,
                $"Project manifest '{Constants.ProjectFileName}' was not found.");
        }

        var document = await storage.GetDocumentFromPathAsync(projectFilePath, cancellationToken)
            .ConfigureAwait(false);
        var json = await document.ReadAllTextAsync(cancellationToken).ConfigureAwait(false);
        var projectInfo = ProjectInfo.FromJson(json);
        projectInfo.Location = projectRoot;

        return this.ValidateMounts(projectRoot, projectInfo);
    }

    private ProjectValidationResult ValidateMounts(string projectRoot, IProjectInfo projectInfo)
    {
        try
        {
            _ = CookedContentOrdering.Resolve(projectInfo);
        }
        catch (Exception exception) when (exception is InvalidDataException or ArgumentException)
        {
            return ProjectValidationResult.Failure(ProjectValidationState.InvalidContentRoots, projectRoot, exception.Message);
        }

        if (projectInfo.OutputFolderViews.Any(static view => view.Folder is not (ProjectOutputFolders.Cooked or ProjectOutputFolders.Build))
            || projectInfo.OutputFolderViews.DistinctBy(static view => view.Folder).Count() != projectInfo.OutputFolderViews.Count)
        {
            return ProjectValidationResult.Failure(
                ProjectValidationState.InvalidContentRoots,
                projectRoot,
                $"Project manifest may show only {ProjectOutputFolders.Cooked} and {ProjectOutputFolders.Build}, each once.");
        }

        // Unbound and missing mounts are warnings: the project opens and shows them. Structural errors fail.
        var table = ProjectMounts.Resolve(fileSystem, FileSystemPath.From(projectRoot), projectInfo.Mounts);
        var findings = projectInfo.Mounts.Any(static mount => mount.Kind == MountKind.Source)
            ? table.Findings
            : [.. table.Findings, new MountFinding(MountCodes.SourceRequired, "Project manifest declares no source mount.")];
        return findings.FirstOrDefault(static finding => finding.IsError) is { } error
            ? new ProjectValidationResult
            {
                State = ProjectValidationState.InvalidContentRoots,
                ProjectRoot = projectRoot,
                Message = $"{error.Code}: {error.Message}",
                MountFindings = findings,
            }
            : ProjectValidationResult.Valid(projectRoot, projectInfo, findings);
    }

    [SuppressMessage(
        "Performance",
        "CA1822:Mark members as static",
        Justification = "Instance helpers keep StyleCop member ordering without changing the validation contract.")]
    private bool IsMissing(Exception exception)
        => exception is ItemNotFoundException
            or DirectoryNotFoundException
            or FileNotFoundException
            or InvalidPathException
            || exception.InnerException is DirectoryNotFoundException
                or FileNotFoundException;

    [SuppressMessage(
        "Performance",
        "CA1822:Mark members as static",
        Justification = "Instance helpers keep StyleCop member ordering without changing the validation contract.")]
    private bool IsInaccessible(Exception exception)
        => exception is UnauthorizedAccessException
            or IOException
            or StorageException
            || exception.InnerException is UnauthorizedAccessException
                or IOException;
}
