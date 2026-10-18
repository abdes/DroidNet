// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Maps source and native identities to the same authored descriptor without changing scene references.</summary>
internal static class CookInputResolver
{
    /// <summary>The cooked script extension.</summary>

    /// <summary>The script source extensions, in the order a source is looked for.</summary>
    private static readonly IReadOnlyList<string> ScriptSourceExtensions = AssetFileTypes.Suffixes(AssetFileCategory.Script);

    /// <summary>Maps a project-owned physical source to its declared authoring identity.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="source">The absolute source path.</param>
    /// <returns>The source identity, or null outside authoring mounts.</returns>
    public static Uri? FindAuthoringSourceUri(ProjectContext project, string source)
        => FindAuthoringFile(project.FindByFilePath(source))?.AssetUri;

    /// <summary>Tests whether a virtual reference belongs to a declared authoring mount.</summary>
    /// <param name="project">The declared authoring mounts.</param>
    /// <param name="uri">The asset identity to resolve.</param>
    /// <returns>Whether the URI belongs to an authoring mount.</returns>
    public static bool IsAuthoringUri(ProjectContext project, Uri uri)
        => FindAuthoringFile(project.FindByAssetUri(uri)) is not null;

    /// <summary>Tests whether the project holds the source a reference cooks from.</summary>
    /// <param name="project">The declared authoring mounts.</param>
    /// <param name="uri">The asset identity.</param>
    /// <returns>Whether the reference is in an authoring mount and its source file exists.</returns>
    public static bool HasProjectSource(ProjectContext project, Uri uri)
    {
        if (!IsAuthoringUri(project, uri))
        {
            return false;
        }

        // A script cooks from a .lua or .luau beside it; without one, the reference names a cooked script only.
        var path = Uri.UnescapeDataString(uri.AbsolutePath);
        return IsScriptReference(path)
            ? TryResolveScript(project, uri, ContentCookInputRole.Dependency) is not null
            : File.Exists(Resolve(project, uri, ContentCookInputRole.Dependency).SourceAbsolutePath);
    }

    /// <summary>Resolves the authored descriptor corresponding to a source or native identity.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="uri">The reference to resolve.</param>
    /// <param name="role">The input's role in the requested closure.</param>
    /// <returns>The canonical authored identity and physical source path.</returns>
    public static ContentCookInput Resolve(ProjectContext project, Uri uri, ContentCookInputRole role)
    {
        var path = Uri.UnescapeDataString(uri.AbsolutePath);
        if (AssetFileTypes.Is(path, AssetFileCategory.Input))
        {
            return ResolveInputFile(project, uri, path, role);
        }

        if (TryResolveScript(project, uri, role) is { } script)
        {
            return script;
        }

        var extension = Path.GetExtension(path.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? path[..^5] : path).ToUpperInvariant();
        var kind = extension switch
        {
            ".OTEX" => ContentCookAssetKind.Texture,
            ".OMAT" => ContentCookAssetKind.Material,
            ".OGEO" => ContentCookAssetKind.Geometry,
            ".OSCENE" => ContentCookAssetKind.Scene,
            ".GLTF" or ".GLB" or ".FBX" => ContentCookAssetKind.ForeignSource,
            _ => throw new InvalidDataException($"Unsupported cook dependency '{uri}'."),
        };
        var nativePath = kind == ContentCookAssetKind.ForeignSource ? path : ContentPipelinePaths.ToNativeDescriptorPath(uri, extension);
        var slash = nativePath.IndexOf('/', 1);
        if (slash <= 1)
        {
            throw new InvalidDataException($"Cook dependency '{uri}' has no authoring mount.");
        }

        if (!HasAuthoringMount(project, nativePath[1..slash]))
        {
            throw new InvalidDataException($"Cook dependency '{uri}' refers to an unknown authoring mount.");
        }

        var location = FindAuthoringFile(project.FindByVirtualPath(nativePath + (kind == ContentCookAssetKind.ForeignSource ? string.Empty : ".json")));
        var relative = location is null ? null : Path.GetRelativePath(project.ProjectRoot, location.FilePath).Replace('\\', '/');
        var outputPath = kind == ContentCookAssetKind.Scene ? ContentPipelinePaths.NormalizeSceneOutputPath(nativePath) : nativePath;
        return location is null || relative is null || Path.IsPathRooted(relative) || relative.StartsWith("../", StringComparison.Ordinal)
            || relative.Split('/')[0].ToUpperInvariant() is ".COOKED" or ".BUILD" or ".PIPELINE" or ".IMPORTED"
            ? throw new InvalidDataException($"Cook dependency '{uri}' does not resolve to a retained authoring file in this project.")
            : new(kind == ContentCookAssetKind.ForeignSource ? uri : location.AssetUri, kind, location.MountName, relative, location.FilePath, kind == ContentCookAssetKind.ForeignSource ? null : outputPath, role);
    }

    /// <summary>Resolves a script to its source: <c>/Content/Player/move.oscript</c> cooks from
    /// <c>Content/Player/move.lua</c> or <c>move.luau</c>.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="uri">The script identity, or its source.</param>
    /// <param name="role">The input's role in the requested closure.</param>
    /// <returns>The script input, or null when the URI is not a script with a source in an authoring mount.</returns>
    public static ContentCookInput? TryResolveScript(ProjectContext project, Uri uri, ContentCookInputRole role)
    {
        var path = Uri.UnescapeDataString(uri.AbsolutePath);
        var stem = path.EndsWith(AssetFileTypes.Script, StringComparison.OrdinalIgnoreCase) ? path[..^AssetFileTypes.Script.Length]
            : ScriptSourceExtensions.FirstOrDefault(extension => path.EndsWith(extension, StringComparison.OrdinalIgnoreCase)) is { } source ? path[..^source.Length]
            : null;
        if (stem is null || !VirtualPath.IsCanonical(path))
        {
            return null;
        }

        var sourceFile = ScriptSourceExtensions
            .Select(extension => FindAuthoringFile(project.FindByVirtualPath(stem + extension)))
            .FirstOrDefault(static location => location is not null && File.Exists(location.FilePath));
        var output = stem + AssetFileTypes.Script;
        if (sourceFile is null || FindAuthoringFile(project.FindByVirtualPath(output)) is not { } script)
        {
            return null;
        }

        var relative = Path.GetRelativePath(project.ProjectRoot, sourceFile.FilePath).Replace('\\', '/');
        return new(script.AssetUri, ContentCookAssetKind.Script, sourceFile.MountName, relative, sourceFile.FilePath, output, role);
    }

    private static bool IsScriptReference(string path)
        => path.EndsWith(AssetFileTypes.Script, StringComparison.OrdinalIgnoreCase)
            || ScriptSourceExtensions.Any(extension => path.EndsWith(extension, StringComparison.OrdinalIgnoreCase));

    // An input file keeps its own identity; its output path is the file itself, whose folder receives its actions
    // and mapping contexts.
    private static ContentCookInput ResolveInputFile(ProjectContext project, Uri uri, string path, ContentCookInputRole role)
    {
        if (!VirtualPath.IsCanonical(path))
        {
            throw new InvalidDataException($"Input file '{uri}' is not a canonical virtual path.");
        }

        var slash = path.IndexOf('/', 1);
        if (slash <= 1 || !HasAuthoringMount(project, path[1..slash]))
        {
            throw new InvalidDataException($"Input file '{uri}' refers to an unknown authoring mount.");
        }

        var location = FindAuthoringFile(project.FindByVirtualPath(path));
        var relative = location is null ? null : Path.GetRelativePath(project.ProjectRoot, location.FilePath).Replace('\\', '/');
        return location is null || relative is null || Path.IsPathRooted(relative) || relative.StartsWith("../", StringComparison.Ordinal)
            ? throw new InvalidDataException($"Input file '{uri}' does not resolve to a file in this project.")
            : new(uri, ContentCookAssetKind.Input, location.MountName, relative, location.FilePath, path, role);
    }

    // The cook reads the files of authoring mounts only; a mount's own folder is not a source.
    private static ProjectAssetLocation? FindAuthoringFile(ProjectAssetLocation? location)
        => location is { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } ? location : null;

    private static bool HasAuthoringMount(ProjectContext project, string name)
        => project.AuthoringMounts.Any(mount => string.Equals(mount.Name, name, StringComparison.Ordinal));
}
