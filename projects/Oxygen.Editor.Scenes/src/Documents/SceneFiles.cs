// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>The scenes of a project: the scene files the asset catalog lists, in any folder of its authoring mounts.</summary>
/// <remarks>A scene's name is its file name; its identity is the id inside the file.</remarks>
public static class SceneFiles
{
    /// <summary>Lists the project's scene files.</summary>
    /// <param name="catalog">The project's asset catalog.</param>
    /// <param name="project">The project.</param>
    /// <param name="cancellationToken">Cancels the query.</param>
    /// <returns>Where each scene file is, ordered by virtual path.</returns>
    public static async Task<IReadOnlyList<ProjectAssetLocation>> ListAsync(IAssetCatalog catalog, ProjectContext project, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        ArgumentNullException.ThrowIfNull(project);
        var records = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), cancellationToken).ConfigureAwait(true);
        return [.. records
            .Where(static record => AssetFileTypes.Find(record.Identity.Name)?.Suffix == AssetFileTypes.SceneDescriptor)
            .Select(record => project.Find(record.Identity))
            .OfType<ProjectAssetLocation>()
            .Where(static location => location.MountKind == ProjectMountKind.Authoring)
            .DistinctBy(static location => location.File)
            .OrderBy(static location => location.Path)];
    }

    /// <summary>Finds the scene file that holds a scene identity.</summary>
    /// <param name="catalog">The project's asset catalog.</param>
    /// <param name="project">The project.</param>
    /// <param name="sceneId">The scene's id.</param>
    /// <param name="cancellationToken">Cancels reading the files.</param>
    /// <returns>Where the scene is, or <see langword="null"/> when no scene file holds the id.</returns>
    public static async Task<ProjectAssetLocation?> FindByIdAsync(IAssetCatalog catalog, ProjectContext project, SceneId sceneId, CancellationToken cancellationToken = default)
    {
        foreach (var location in await ListAsync(catalog, project, cancellationToken).ConfigureAwait(true))
        {
            if (await ReadIdAsync(location.File, cancellationToken).ConfigureAwait(true) == sceneId)
            {
                return location;
            }
        }

        return null;
    }

    /// <summary>Finds a scene by name, as the editor remembered the last scene a project opened before scenes could live
    /// in any folder.</summary>
    /// <param name="catalog">The project's asset catalog.</param>
    /// <param name="project">The project.</param>
    /// <param name="name">The scene's name.</param>
    /// <param name="cancellationToken">Cancels the query.</param>
    /// <returns>The first scene file with that name, or <see langword="null"/>.</returns>
    public static async Task<ProjectAssetLocation?> FindByNameAsync(IAssetCatalog catalog, ProjectContext project, string name, CancellationToken cancellationToken = default)
        => (await ListAsync(catalog, project, cancellationToken).ConfigureAwait(true))
            .FirstOrDefault(location => string.Equals(SceneContent.NameOf(location.File.Name), name, StringComparison.OrdinalIgnoreCase));

    /// <summary>Reads the identity a scene file holds.</summary>
    /// <param name="path">The scene file.</param>
    /// <param name="cancellationToken">Cancels reading the file.</param>
    /// <returns>The scene's id, or <see langword="null"/> when the file is not a readable scene.</returns>
    public static async Task<SceneId?> ReadIdAsync(FileSystemPath path, CancellationToken cancellationToken = default)
    {
        try
        {
            var stream = File.OpenRead(path.FullPath);
            await using var lifetime = stream.ConfigureAwait(true);
            using var document = await JsonDocument.ParseAsync(stream, cancellationToken: cancellationToken).ConfigureAwait(true);
            return document.RootElement.ValueKind == JsonValueKind.Object
                && document.RootElement.TryGetProperty("Id", out var id) && id.TryGetGuid(out var value)
                    ? SceneId.FromValue(value)
                    : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }
}
