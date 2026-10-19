// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Resolves an asset to the source the cook reads: any path of the asset (its identity or one of its files) names the
/// asset by <see cref="AssetFileTypes.ToIdentity"/>, and the project holds its source where
/// <see cref="ProjectAssetFiles.FindSource"/> finds it.
/// </summary>
internal static class CookInputResolver
{
    /// <summary>Maps a project-owned physical source to its virtual path.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="source">The absolute source path.</param>
    /// <returns>The source's virtual path, or null outside authoring mounts.</returns>
    public static AssetPath? FindAuthoringSourceUri(ProjectContext project, FileSystemPath source)
        => FindAuthoringFile(project.FindByFilePath(source))?.Path;

    /// <summary>Tests whether a path belongs to a declared authoring mount.</summary>
    /// <param name="project">The declared authoring mounts.</param>
    /// <param name="uri">The path to test.</param>
    /// <returns>Whether the path is below an authoring mount.</returns>
    public static bool IsAuthoringUri(ProjectContext project, AssetPath uri)
        => FindAuthoringFile(project.Find(uri)) is not null;

    /// <summary>Tests whether the project holds the source an asset cooks from.</summary>
    /// <param name="project">The declared authoring mounts.</param>
    /// <param name="uri">The asset's identity, or a path of one of its files.</param>
    /// <returns>Whether the asset's source exists in an authoring mount.</returns>
    public static bool HasProjectSource(ProjectContext project, AssetPath uri)
        => FindAuthoringFile(project.FindSource(AssetFileTypes.ToIdentity(uri))) is not null;

    /// <summary>Resolves the source an asset cooks from.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="uri">The asset's identity, or a path of one of its files.</param>
    /// <param name="role">The input's role in the requested closure.</param>
    /// <returns>The cook input: the source's path, kind and file, and the identity it cooks to.</returns>
    /// <exception cref="InvalidDataException">The asset has no source the cook can read in this project.</exception>
    public static ContentCookInput Resolve(ProjectContext project, AssetPath uri, ContentCookInputRole role)
    {
        if (TryResolveScript(project, uri, role) is { } script)
        {
            return script;
        }

        var identity = AssetFileTypes.ToIdentity(uri);
        if (!HasAuthoringMount(project, identity.Mount))
        {
            throw new InvalidDataException($"Cook dependency '{uri}' refers to an unknown authoring mount.");
        }

        // A missing source is still the descriptor the asset would cook from, so the cook reports it missing.
        var location = FindAuthoringFile(project.LocateSource(identity));
        var kind = location is null || AssetFileTypes.Is(location.Path.Name, AssetFileCategory.Cooked) ? ContentCookAssetKind.Unknown : ContentCookAssetKinds.Of(location.Path.Name);
        if (kind == ContentCookAssetKind.Unknown)
        {
            throw new InvalidDataException($"Unsupported cook dependency '{uri}'.");
        }

        var relative = Path.GetRelativePath(project.ProjectRoot.FullPath, location!.File.FullPath).Replace('\\', '/');
        if (Path.IsPathRooted(relative) || relative.StartsWith("../", StringComparison.Ordinal)
            || ProjectOutputFolders.Contains(relative))
        {
            throw new InvalidDataException($"Cook dependency '{uri}' does not resolve to a retained authoring file in this project.");
        }

        // An input file's output is the file itself, whose folder receives its actions and mapping contexts.
        var output = kind switch
        {
            ContentCookAssetKind.ForeignSource => null,
            ContentCookAssetKind.Input => location.Path.VirtualPath,
            _ => ContentPipelinePaths.ToNativeDescriptorPath(identity, Path.GetExtension(identity.Name)),
        };
        return new(location.Path, kind, location.Path.Mount, relative, location.File, output, role);
    }

    /// <summary>Resolves a script to its source: <c>/Content/Player/move.oscript</c> cooks from
    /// <c>Content/Player/move.lua</c> or <c>move.luau</c>.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="uri">The script's identity, or its source.</param>
    /// <param name="role">The input's role in the requested closure.</param>
    /// <returns>The script input, or null when the path is not a script with a source in an authoring mount.</returns>
    public static ContentCookInput? TryResolveScript(ProjectContext project, AssetPath uri, ContentCookInputRole role)
    {
        var identity = AssetFileTypes.ToIdentity(uri);
        if (!identity.Name.EndsWith(AssetFileTypes.Script, StringComparison.Ordinal)
            || !VirtualPath.IsCanonical(identity.VirtualPath)
            || FindAuthoringFile(project.FindSource(identity)) is not { } source
            || !AssetFileTypes.Is(source.Path.Name, AssetFileCategory.Script)
            || FindAuthoringFile(project.Find(identity)) is null)
        {
            return null;
        }

        var relative = Path.GetRelativePath(project.ProjectRoot.FullPath, source.File.FullPath).Replace('\\', '/');
        return new(identity, ContentCookAssetKind.Script, source.Path.Mount, relative, source.File, identity.VirtualPath, role);
    }

    // The cook reads the files of authoring mounts only; a mount's own folder is not a source.
    private static ProjectAssetLocation? FindAuthoringFile(ProjectAssetLocation? location)
        => location is { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } ? location : null;

    private static bool HasAuthoringMount(ProjectContext project, string name)
        => project.AuthoringMounts.Any(mount => string.Equals(mount.Name, name, StringComparison.Ordinal));
}
