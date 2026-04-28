// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Utils;
using Oxygen.Storage;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Storage-backed project creation service.
/// </summary>
/// <param name="storage">The storage provider.</param>
/// <param name="validation">The project validation service.</param>
public sealed class ProjectCreationService(
    IStorageProvider storage,
    IProjectValidationService validation) : IProjectCreationService
{
    private const string TemplateManifestFileName = "Template.json";

    /// <inheritdoc/>
    public async Task<bool> CanCreateProjectAsync(
        string projectName,
        string parentLocation,
        CancellationToken cancellationToken = default)
    {
        if (!IsValidProjectFolderName(projectName))
        {
            return false;
        }

        var parentRoot = storage.Normalize(parentLocation);
        if (!await storage.FolderExistsAsync(parentRoot).ConfigureAwait(false))
        {
            return false;
        }

        var projectRoot = storage.NormalizeRelativeTo(parentRoot, projectName);
        var projectFolder = await storage.GetFolderFromPathAsync(projectRoot, cancellationToken)
            .ConfigureAwait(false);
        return !await projectFolder.ExistsAsync().ConfigureAwait(false)
            || !await projectFolder.HasItemsAsync().ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task<ProjectCreationResult> CreateFromTemplateAsync(
        ProjectCreationRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);

        var projectRoot = string.Empty;
        var rootCreated = false;
        var createdItems = new List<IStorageItem>();
        try
        {
            var parentRoot = storage.Normalize(request.ParentLocation);
            projectRoot = storage.NormalizeRelativeTo(parentRoot, request.ProjectName);
            if (!await this.CanCreateProjectAsync(request.ProjectName, parentRoot, cancellationToken)
                    .ConfigureAwait(false))
            {
                return ProjectCreationResult.Failure(
                    projectRoot,
                    "Project destination is not available.");
            }

            var templateFolder = await storage.GetFolderFromPathAsync(request.TemplateRoot, cancellationToken)
                .ConfigureAwait(false);
            if (!await templateFolder.ExistsAsync().ConfigureAwait(false))
            {
                return ProjectCreationResult.Failure(projectRoot, "Project template folder does not exist.");
            }

            var projectFolder = await storage.GetFolderFromPathAsync(projectRoot, cancellationToken)
                .ConfigureAwait(false);
            rootCreated = !await projectFolder.ExistsAsync().ConfigureAwait(false);
            await projectFolder.CreateAsync(cancellationToken).ConfigureAwait(false);

            await this.CopyTemplatePayloadAsync(templateFolder, projectFolder, createdItems, cancellationToken)
                .ConfigureAwait(false);
            await this.WriteProjectManifestAsync(projectFolder, request, cancellationToken).ConfigureAwait(false);

            var validationResult = await validation.ValidateAsync(projectRoot, cancellationToken).ConfigureAwait(false);
            if (!validationResult.IsValid || validationResult.ProjectInfo is null)
            {
                await CleanupCreatedItemsAsync(projectFolder, rootCreated, createdItems, cancellationToken)
                    .ConfigureAwait(false);
                return ProjectCreationResult.Failure(
                    projectRoot,
                    validationResult.Message ?? "Created project did not pass validation.",
                    validationResult);
            }

            return ProjectCreationResult.Success(projectRoot, validationResult.ProjectInfo, validationResult);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            if (!string.IsNullOrEmpty(projectRoot))
            {
                var projectFolder = await storage.GetFolderFromPathAsync(projectRoot, CancellationToken.None)
                    .ConfigureAwait(false);
                await CleanupCreatedItemsAsync(projectFolder, rootCreated, createdItems, CancellationToken.None)
                    .ConfigureAwait(false);
            }

            return ProjectCreationResult.Failure(projectRoot, ex.Message, exception: ex);
        }
    }

    private static bool IsValidProjectFolderName(string projectName)
        => !string.IsNullOrWhiteSpace(projectName)
            && projectName.IndexOfAny(Path.GetInvalidFileNameChars()) < 0
            && !projectName.Contains(Path.DirectorySeparatorChar, StringComparison.Ordinal)
            && !projectName.Contains(Path.AltDirectorySeparatorChar, StringComparison.Ordinal);

    private static async Task CleanupCreatedItemsAsync(
        IFolder projectFolder,
        bool rootCreated,
        IReadOnlyList<IStorageItem> createdItems,
        CancellationToken cancellationToken)
    {
        if (rootCreated)
        {
            await projectFolder.DeleteRecursiveAsync(cancellationToken).ConfigureAwait(false);
            return;
        }

        foreach (var item in createdItems.OrderByDescending(item => item.Location.Length))
        {
            switch (item)
            {
                case IDocument document:
                    await document.DeleteAsync(cancellationToken).ConfigureAwait(false);
                    break;
                case IFolder folder:
                    await folder.DeleteRecursiveAsync(cancellationToken).ConfigureAwait(false);
                    break;
            }
        }
    }

    [SuppressMessage(
        "Performance",
        "CA1822:Mark members as static",
        Justification = "Instance helper keeps the recursive template-copy workflow grouped with this service.")]
    private async Task CopyTemplatePayloadAsync(
        IFolder templateFolder,
        IFolder projectFolder,
        ICollection<IStorageItem> createdItems,
        CancellationToken cancellationToken)
    {
        await foreach (var folder in templateFolder.GetFoldersAsync(cancellationToken).ConfigureAwait(false))
        {
            var destination = await projectFolder.GetFolderAsync(folder.Name, cancellationToken).ConfigureAwait(false);
            await destination.CreateAsync(cancellationToken).ConfigureAwait(false);
            createdItems.Add(destination);
            await this.CopyTemplatePayloadAsync(folder, destination, createdItems, cancellationToken)
                .ConfigureAwait(false);
        }

        await foreach (var document in templateFolder.GetDocumentsAsync(cancellationToken).ConfigureAwait(false))
        {
            if (string.Equals(document.Name, TemplateManifestFileName, StringComparison.Ordinal))
            {
                continue;
            }

            var copied = await document.CopyOverwriteAsync(projectFolder, cancellationToken).ConfigureAwait(false);
            createdItems.Add(copied);
        }
    }

    private async Task WriteProjectManifestAsync(
        IFolder projectFolder,
        ProjectCreationRequest request,
        CancellationToken cancellationToken)
    {
        var templateInfo = await this.LoadTemplateProjectInfoAsync(projectFolder, cancellationToken).ConfigureAwait(false);
        var projectInfo = new ProjectInfo(
            Guid.NewGuid(),
            request.ProjectName,
            templateInfo?.Category ?? request.Category,
            projectFolder.Location,
            templateInfo?.Thumbnail ?? request.Thumbnail);

        foreach (var mount in templateInfo?.AuthoringMounts ?? [])
        {
            projectInfo.AuthoringMounts.Add(mount);
        }

        foreach (var mount in templateInfo?.LocalFolderMounts ?? [])
        {
            projectInfo.LocalFolderMounts.Add(mount);
        }

        if (projectInfo.AuthoringMounts.Count == 0)
        {
            projectInfo.AuthoringMounts.Add(new ProjectMountPoint("Content", "Content"));
            var contentFolder = await projectFolder.GetFolderAsync("Content", cancellationToken).ConfigureAwait(false);
            await contentFolder.CreateAsync(cancellationToken).ConfigureAwait(false);
        }

        var manifest = await projectFolder.GetDocumentAsync(Constants.ProjectFileName, cancellationToken)
            .ConfigureAwait(false);
        await manifest.WriteAllTextAsync(ProjectInfo.ToJson(projectInfo), cancellationToken).ConfigureAwait(false);
    }

    [SuppressMessage(
        "Performance",
        "CA1822:Mark members as static",
        Justification = "Instance helper keeps template-manifest loading grouped with this service.")]
    private async Task<TemplateProjectInfo?> LoadTemplateProjectInfoAsync(
        IFolder projectFolder,
        CancellationToken cancellationToken)
    {
        var manifest = await projectFolder.GetDocumentAsync(Constants.ProjectFileName, cancellationToken)
            .ConfigureAwait(false);
        if (!await manifest.ExistsAsync().ConfigureAwait(false))
        {
            return null;
        }

        var json = await manifest.ReadAllTextAsync(cancellationToken).ConfigureAwait(false);
        return TemplateProjectInfo.FromJson(json);
    }

    private sealed record TemplateProjectInfo(
        Category? Category,
        string? Thumbnail,
        IReadOnlyList<ProjectMountPoint> AuthoringMounts,
        IReadOnlyList<LocalFolderMount> LocalFolderMounts)
    {
        private static readonly JsonSerializerOptions JsonOptions = new()
        {
            AllowTrailingCommas = true,
            PropertyNameCaseInsensitive = false,
            Converters = { new CategoryJsonConverter() },
        };

        public static TemplateProjectInfo FromJson(string json)
        {
            var descriptor = JsonSerializer.Deserialize<TemplateProjectInfoDescriptor>(json, JsonOptions)
                ?? new TemplateProjectInfoDescriptor();

            return new TemplateProjectInfo(
                descriptor.Category,
                descriptor.Thumbnail,
                descriptor.AuthoringMounts ?? [],
                descriptor.LocalFolderMounts ?? []);
        }
    }

    private sealed class TemplateProjectInfoDescriptor
    {
        public Category? Category { get; set; }

        public string? Thumbnail { get; set; }

        public List<ProjectMountPoint>? AuthoringMounts { get; set; }

        public List<LocalFolderMount>? LocalFolderMounts { get; set; }
    }
}
