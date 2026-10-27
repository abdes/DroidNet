// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
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
            .Where(static record => string.Equals(AssetFileTypes.Find(record.Identity.Name)?.Suffix, AssetFileTypes.SceneDescriptor, StringComparison.Ordinal))
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
            if (await ReadIdAsync(project, location, cancellationToken).ConfigureAwait(true) == sceneId)
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

    /// <summary>Finds a scene by the name the editor remembered for it: its name, its file name or its id.</summary>
    /// <param name="catalog">The project's asset catalog, which lists its scene files.</param>
    /// <param name="project">The project whose scene files are searched.</param>
    /// <param name="lastOpenedScene">The remembered name.</param>
    /// <returns>Where the matching scene is, or <see langword="null"/> when no scene matches.</returns>
    public static async Task<ProjectAssetLocation?> FindByNameOrIdAsync(IAssetCatalog catalog, ProjectContext project, string? lastOpenedScene)
    {
        if (string.IsNullOrWhiteSpace(lastOpenedScene))
        {
            return null;
        }

        if (Guid.TryParse(lastOpenedScene, out var id))
        {
            return await FindByIdAsync(catalog, project, SceneId.FromValue(id)).ConfigureAwait(true);
        }

        var sceneName = lastOpenedScene.EndsWith(AssetFileTypes.SceneDescriptor, StringComparison.Ordinal)
            ? lastOpenedScene[..^AssetFileTypes.SceneDescriptor.Length]
            : lastOpenedScene;
        return await FindByNameAsync(catalog, project, sceneName).ConfigureAwait(true);
    }

    /// <summary>Finds the scene a workspace opens first: the one activation asked for, else the project's start scene.</summary>
    /// <param name="catalog">The project's asset catalog, which lists its scene files.</param>
    /// <param name="context">The project.</param>
    /// <returns>The scene's asset URI, or <see langword="null"/> when there is none to open.</returns>
    public static async Task<AssetPath?> FindInitialAsync(IAssetCatalog catalog, ProjectContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        // Fresh activation may carry an explicit scene request (template StarterScene or a workflow open).
        if (context.InitialScene is { } initialScene && context.FindSource(initialScene) is { IsMountRoot: false } requested)
        {
            return requested.Identity;
        }

        // Otherwise the project's configured start scene, resolved by stable ID.
        // A missing or invalid start scene is not silently replaced by last-opened or first-listed.
        return context.DefaultSceneId is { } defaultSceneId
            ? (await FindByIdAsync(catalog, context, defaultSceneId).ConfigureAwait(true))?.Identity
            : null;
    }

    /// <summary>Reads an authored scene of the project, as an instance of it brings its nodes in.</summary>
    /// <param name="project">The project, through whose file system the file is read.</param>
    /// <param name="scene">The scene's asset.</param>
    /// <param name="cancellationToken">Cancels reading the file.</param>
    /// <returns>The scene, in no document; <see langword="null"/> when the asset is not a readable scene of the project.</returns>
    /// <remarks>The scene's identity, such as <c>Watchtower.oscene</c>, is read from its source, <c>Watchtower.oscene.json</c>.</remarks>
    public static async Task<Scene?> ReadAsync(ProjectContext project, AssetPath scene, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(scene);
        if (project.FindSource(scene) is not { IsProjectFile: true } location)
        {
            return null;
        }

        try
        {
            var bytes = await project.FileSystem.File.ReadAllBytesAsync(location.File.FullPath, cancellationToken).ConfigureAwait(true);
            return JsonSerializer.Deserialize(bytes, SceneFileJsonContext.Default.SceneFileData) is { } data ? Scene.CreateAndHydrate(data) : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException or ArgumentException)
        {
            return null;
        }
    }

    /// <summary>Reads the identity a scene file of the project holds.</summary>
    /// <param name="project">The project, through whose file system the file is read.</param>
    /// <param name="location">The scene file.</param>
    /// <param name="cancellationToken">Cancels reading the file.</param>
    /// <returns>The scene's id, or <see langword="null"/> when the file is not a readable scene.</returns>
    public static async Task<SceneId?> ReadIdAsync(ProjectContext project, ProjectAssetLocation location, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(location);
        try
        {
            return SceneSerializer.ReadId(await project.FileSystem.File.ReadAllBytesAsync(location.File.FullPath, cancellationToken).ConfigureAwait(true));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
