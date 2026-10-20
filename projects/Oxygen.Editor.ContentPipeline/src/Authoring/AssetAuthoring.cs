// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Authoring;

/// <summary>Creates new files from the templates of their asset families.</summary>
/// <param name="catalog">The asset catalog, which lists the files a new name must not collide with.</param>
/// <param name="files">Creates the file only where none exists.</param>
/// <param name="logger">Records each file created or not created.</param>
public sealed partial class AssetAuthoring(IAssetCatalog catalog, IAtomicFileStore files, ILogger<AssetAuthoring>? logger = null)
    : IAssetAuthoring
{
    private readonly ILogger logger = (ILogger?)logger ?? NullLogger.Instance;

    /// <inheritdoc/>
    public event EventHandler<AssetFilesChangedEventArgs>? FilesChanged;

    /// <inheritdoc/>
    public async Task<AssetProposal> ProposeAsync(ProjectContext project, AuthoredAssetTemplate fileTemplate, AssetPath? selectedFolder, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(fileTemplate);
        var folder = GetDefaultFolder(project, selectedFolder, fileTemplate.Folder);
        var existing = await this.ReadExistingAsync(cancellationToken).ConfigureAwait(false);
        for (var index = 1; index < 1000; index++)
        {
            var candidate = string.Create(CultureInfo.InvariantCulture, $"{fileTemplate.DefaultName}{index}");
            if (Reject(project, fileTemplate, candidate, folder, existing) is null)
            {
                return new(folder, candidate);
            }
        }

        return new(folder, fileTemplate.DefaultName);
    }

    /// <inheritdoc/>
    public async Task<string?> ValidateAsync(ProjectContext project, AuthoredAssetTemplate fileTemplate, string name, AssetPath folder, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(fileTemplate);
        ArgumentNullException.ThrowIfNull(name);
        ArgumentNullException.ThrowIfNull(folder);
        return Reject(project, fileTemplate, VirtualPath.NormalizeTypedName(name), folder, await this.ReadExistingAsync(cancellationToken).ConfigureAwait(false));
    }

    /// <inheritdoc/>
    public async Task<FileSystemPath> CreateAsync(ProjectContext project, AuthoredAssetTemplate fileTemplate, string name, AssetPath folder, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(name);
        ArgumentNullException.ThrowIfNull(folder);
        ArgumentNullException.ThrowIfNull(fileTemplate);
        name = VirtualPath.NormalizeTypedName(name);

        // Resumes on the caller's context, where FilesChanged is raised.
        if (Reject(project, fileTemplate, name, folder, await this.ReadExistingAsync(cancellationToken).ConfigureAwait(true)) is { } rejected)
        {
            this.LogNotCreated(name, folder, rejected);
            throw new AssetCreationException(rejected);
        }

        var file = name + fileTemplate.Suffixes[0];
        var path = RelocationPaths.ToPhysical(project, folder)!.Combine(file);
        try
        {
            _ = await files.WriteAsync(path.FullPath, Encoding.UTF8.GetBytes(fileTemplate.Content(name)), FileVersion.Missing, cancellationToken).ConfigureAwait(true);
        }
        catch (StorageWriteConflictException conflict)
        {
            var message = $"'{file}' already exists in {folder}. Choose another name.";
            this.LogNotCreated(name, folder, message);
            throw new AssetCreationException(message, conflict);
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException or StorageException)
        {
            this.LogWriteFailed(failure, path);
            throw new AssetCreationException($"Could not write '{path}': {failure.Message}", failure);
        }

        this.LogCreated(path);
        this.FilesChanged?.Invoke(this, AssetFilesChangedEventArgs.ForCreated([path], foldersCreated: false));
        return path;
    }

    /// <summary>Gets the folder a new file goes to: the folder being browsed, or the template's folder at a mount root.</summary>
    /// <param name="project">The project.</param>
    /// <param name="selectedFolder">The folder being browsed, a virtual path, if any.</param>
    /// <param name="templateFolder">The template's folder name, such as <c>Scripts</c>.</param>
    /// <returns>The virtual folder.</returns>
    internal static AssetPath GetDefaultFolder(ProjectContext project, AssetPath? selectedFolder, string templateFolder)
    {
        var selected = selectedFolder is not null && RelocationPaths.ToPhysical(project, selectedFolder) is not null ? selectedFolder : null;
        if (selected is { IsMount: false })
        {
            return selected;
        }

        var mount = selected ?? AssetPath.Create(RelocationPaths.GetWritableMounts(project).FirstOrDefault()?.Name ?? Oxygen.Editor.Projects.Constants.ContentFolderName);
        return mount.Combine(templateFolder);
    }

    private static string? Reject(ProjectContext project, AuthoredAssetTemplate fileTemplate, string name, AssetPath folder, HashSet<AssetPath> existing)
    {
        if (VirtualPath.ValidateSegment(name + fileTemplate.Suffixes[0], VirtualPathRuleSet.Authored) is { } invalid)
        {
            return invalid.ToMessage();
        }

        if (RelocationPaths.ToPhysical(project, folder) is null)
        {
            return "Choose a folder inside a project content folder, such as /Content/Scripts.";
        }

        var taken = fileTemplate.Suffixes.Select(suffix => name + suffix)
            .FirstOrDefault(file => existing.Contains(folder.Combine(file)));
        return taken is null
            ? null
            : $"'{taken}' already exists in {folder}, or differs from an existing file only in case. Choose another name.";
    }

    // The catalog's records are the project's files under their virtual paths, such as /Content/Player/move.lua.
    private async Task<HashSet<AssetPath>> ReadExistingAsync(CancellationToken cancellationToken)
        => (await catalog.QueryAsync(new(AssetQueryScope.All), cancellationToken).ConfigureAwait(false))
            .Select(static record => record.Identity)
            .ToHashSet(AssetPath.CaseFolded);
}
