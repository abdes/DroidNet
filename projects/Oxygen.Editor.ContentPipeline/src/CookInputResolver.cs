// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Maps source and native identities to the same authored descriptor without changing scene references.</summary>
internal static class CookInputResolver
{
    /// <summary>The cooked script extension.</summary>
    private const string ScriptExtension = ".oscript";

    /// <summary>The script source extensions, in the order a source is looked for.</summary>
    private static readonly string[] ScriptSourceExtensions = [".lua", ".luau"];

    /// <summary>Maps a project-owned physical source to its declared authoring identity.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="source">The absolute source path.</param>
    /// <returns>The source identity, or null outside authoring mounts.</returns>
    public static Uri? FindAuthoringSourceUri(ProjectContext project, string source)
    {
        foreach (var mount in project.AuthoringMounts)
        {
            var root = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath)).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            if (source.StartsWith(root, StringComparison.OrdinalIgnoreCase))
            {
                var relative = Path.GetRelativePath(root, source).Replace('\\', '/');
                return new Uri(AssetUris.Scheme + ":///" + Uri.EscapeDataString(mount.Name) + "/" + string.Join('/', relative.Split('/').Select(Uri.EscapeDataString)));
            }
        }

        return null;
    }

    /// <summary>Tests whether a virtual reference belongs to a declared authoring mount.</summary>
    /// <param name="project">The declared authoring mounts.</param>
    /// <param name="uri">The asset identity to resolve.</param>
    /// <returns>Whether the URI belongs to an authoring mount.</returns>
    public static bool IsAuthoringUri(ProjectContext project, Uri uri)
        => project.AuthoringMounts.Any(mount => uri.AbsolutePath.StartsWith("/" + Uri.EscapeDataString(mount.Name) + "/", StringComparison.OrdinalIgnoreCase));

    /// <summary>Resolves the authored descriptor corresponding to a source or native identity.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="uri">The reference to resolve.</param>
    /// <param name="role">The input's role in the requested closure.</param>
    /// <returns>The canonical authored identity and physical source path.</returns>
    public static ContentCookInput Resolve(ProjectContext project, Uri uri, ContentCookInputRole role)
    {
        var path = Uri.UnescapeDataString(uri.AbsolutePath);
        if (InputFileReferences.IsInputFile(path))
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

        var mount = project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, nativePath[1..slash], StringComparison.OrdinalIgnoreCase))
            ?? throw new InvalidDataException($"Cook dependency '{uri}' refers to an unknown authoring mount.");
        var relative = Path.Combine(mount.RelativePath, nativePath[(slash + 1)..] + (kind == ContentCookAssetKind.ForeignSource ? string.Empty : ".json")).Replace('\\', '/');
        var absolute = Path.GetFullPath(Path.Combine(project.ProjectRoot, relative));
        relative = Path.GetRelativePath(project.ProjectRoot, absolute).Replace('\\', '/');
        var outputPath = kind == ContentCookAssetKind.Scene ? ContentPipelinePaths.NormalizeSceneOutputPath(nativePath) : nativePath;
        return Path.IsPathRooted(relative) || relative.StartsWith("../", StringComparison.Ordinal)
            || relative.Split('/')[0].ToUpperInvariant() is ".COOKED" or ".BUILD" or ".PIPELINE" or ".IMPORTED"
            ? throw new InvalidDataException($"Cook dependency '{uri}' does not resolve to a retained authoring file in this project.")
            : new(kind == ContentCookAssetKind.ForeignSource ? uri : new Uri($"{AssetUris.Scheme}://{nativePath}.json"), kind, mount.Name, relative, absolute, kind == ContentCookAssetKind.ForeignSource ? null : outputPath, role);
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
        var stem = path.EndsWith(ScriptExtension, StringComparison.OrdinalIgnoreCase) ? path[..^ScriptExtension.Length]
            : ScriptSourceExtensions.FirstOrDefault(extension => path.EndsWith(extension, StringComparison.OrdinalIgnoreCase)) is { } source ? path[..^source.Length]
            : null;
        var slash = stem is null || !ContentPipelinePaths.IsCanonicalVirtualPath(path) ? -1 : stem.IndexOf('/', 1);
        var mount = slash <= 1 ? null : project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, stem![1..slash], StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            return null;
        }

        var mountRoot = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
        var absolute = ScriptSourceExtensions
            .Select(extension => Path.GetFullPath(Path.Combine(mountRoot, stem![(slash + 1)..] + extension)))
            .FirstOrDefault(File.Exists);
        if (absolute is null || !absolute.StartsWith(mountRoot.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        var output = stem + ScriptExtension;
        var relative = Path.GetRelativePath(project.ProjectRoot, absolute).Replace('\\', '/');
        return new(new Uri($"{AssetUris.Scheme}://{string.Join('/', output.Split('/').Select(Uri.EscapeDataString))}"), ContentCookAssetKind.Script, mount.Name, relative, absolute, output, role);
    }

    // An input file keeps its own identity; its output path is the file itself, whose folder receives its actions
    // and mapping contexts.
    private static ContentCookInput ResolveInputFile(ProjectContext project, Uri uri, string path, ContentCookInputRole role)
    {
        if (!ContentPipelinePaths.IsCanonicalVirtualPath(path))
        {
            throw new InvalidDataException($"Input file '{uri}' is not a canonical virtual path.");
        }

        var slash = path.IndexOf('/', 1);
        var mount = (slash <= 1 ? null : project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, path[1..slash], StringComparison.OrdinalIgnoreCase)))
            ?? throw new InvalidDataException($"Input file '{uri}' refers to an unknown authoring mount.");
        var absolute = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath, path[(slash + 1)..]));
        var relative = Path.GetRelativePath(project.ProjectRoot, absolute).Replace('\\', '/');
        return Path.IsPathRooted(relative) || relative.StartsWith("../", StringComparison.Ordinal)
            ? throw new InvalidDataException($"Input file '{uri}' does not resolve to a file in this project.")
            : new(uri, ContentCookAssetKind.Input, mount.Name, relative, absolute, path, role);
    }
}
