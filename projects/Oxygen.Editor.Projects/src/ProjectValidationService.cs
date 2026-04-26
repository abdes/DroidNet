// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using Oxygen.Storage;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Storage-backed implementation of project validation.
/// </summary>
/// <param name="storage">The storage provider.</param>
public sealed class ProjectValidationService(IStorageProvider storage) : IProjectValidationService
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

        var contentRootFailure = await this.ValidateContentRootsAsync(projectRoot, projectInfo, cancellationToken)
            .ConfigureAwait(false);
        return contentRootFailure ?? ProjectValidationResult.Valid(projectRoot, projectInfo);
    }

    private async Task<ProjectValidationResult?> ValidateContentRootsAsync(
        string projectRoot,
        Oxygen.Editor.World.IProjectInfo projectInfo,
        CancellationToken cancellationToken)
    {
        if (projectInfo.AuthoringMounts.Count == 0)
        {
            return ProjectValidationResult.Failure(
                ProjectValidationState.InvalidContentRoots,
                projectRoot,
                "Project manifest does not declare any authoring mount roots.");
        }

        var authoringRootFailure = await this.ValidateAuthoringMountsAsync(projectRoot, projectInfo, cancellationToken)
            .ConfigureAwait(false);
        return authoringRootFailure
            ?? await this.ValidateLocalFolderMountsAsync(projectRoot, projectInfo, cancellationToken)
                .ConfigureAwait(false);
    }

    private async Task<ProjectValidationResult?> ValidateAuthoringMountsAsync(
        string projectRoot,
        Oxygen.Editor.World.IProjectInfo projectInfo,
        CancellationToken cancellationToken)
    {
        foreach (var mount in projectInfo.AuthoringMounts)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (string.IsNullOrWhiteSpace(mount.Name) || string.IsNullOrWhiteSpace(mount.RelativePath))
            {
                return ProjectValidationResult.Failure(
                    ProjectValidationState.InvalidContentRoots,
                    projectRoot,
                    "Project manifest declares an invalid authoring mount root.");
            }

            if (Path.IsPathRooted(mount.RelativePath))
            {
                return ProjectValidationResult.Failure(
                    ProjectValidationState.InvalidContentRoots,
                    projectRoot,
                    $"Authoring mount '{mount.Name}' must use a project-relative path.");
            }

            var mountRoot = storage.NormalizeRelativeTo(projectRoot, mount.RelativePath);
            if (!await storage.FolderExistsAsync(mountRoot).ConfigureAwait(false))
            {
                return ProjectValidationResult.Failure(
                    ProjectValidationState.InvalidContentRoots,
                    projectRoot,
                    $"Authoring mount '{mount.Name}' does not exist: {mountRoot}.");
            }
        }

        return null;
    }

    private async Task<ProjectValidationResult?> ValidateLocalFolderMountsAsync(
        string projectRoot,
        Oxygen.Editor.World.IProjectInfo projectInfo,
        CancellationToken cancellationToken)
    {
        foreach (var mount in projectInfo.LocalFolderMounts)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (string.IsNullOrWhiteSpace(mount.Name) || string.IsNullOrWhiteSpace(mount.AbsolutePath))
            {
                return ProjectValidationResult.Failure(
                    ProjectValidationState.InvalidContentRoots,
                    projectRoot,
                    "Project manifest declares an invalid local folder mount root.");
            }

            var mountRoot = storage.Normalize(mount.AbsolutePath);
            if (!await storage.FolderExistsAsync(mountRoot).ConfigureAwait(false))
            {
                return ProjectValidationResult.Failure(
                    ProjectValidationState.InvalidContentRoots,
                    projectRoot,
                    $"Local folder mount '{mount.Name}' does not exist: {mountRoot}.");
            }
        }

        return null;
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
