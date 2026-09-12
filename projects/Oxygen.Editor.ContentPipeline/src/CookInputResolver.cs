// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Maps source and native identities to the same authored descriptor without changing scene references.</summary>
internal static class CookInputResolver
{
    /// <summary>Resolves the authored descriptor corresponding to a source or native identity.</summary>
    /// <param name="project">The owning project and mount declarations.</param>
    /// <param name="uri">The reference to resolve.</param>
    /// <param name="role">The input's role in the requested closure.</param>
    /// <returns>The canonical authored identity and physical source path.</returns>
    public static ContentCookInput Resolve(ProjectContext project, Uri uri, ContentCookInputRole role)
    {
        var path = Uri.UnescapeDataString(uri.AbsolutePath);
        var extension = Path.GetExtension(path.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? path[..^5] : path).ToUpperInvariant();
        var kind = extension switch
        {
            ".OMAT" => ContentCookAssetKind.Material,
            ".OGEO" => ContentCookAssetKind.Geometry,
            ".OSCENE" => ContentCookAssetKind.Scene,
            _ => throw new InvalidDataException($"Unsupported cook dependency '{uri}'."),
        };
        var nativePath = ContentPipelinePaths.ToNativeDescriptorPath(uri, extension);
        var slash = nativePath.IndexOf('/', 1);
        if (slash <= 1)
        {
            throw new InvalidDataException($"Cook dependency '{uri}' has no authoring mount.");
        }

        var mount = project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, nativePath[1..slash], StringComparison.OrdinalIgnoreCase))
            ?? throw new InvalidDataException($"Cook dependency '{uri}' refers to an unknown authoring mount.");
        var relative = Path.Combine(mount.RelativePath, nativePath[(slash + 1)..] + ".json").Replace('\\', '/');
        var absolute = Path.GetFullPath(Path.Combine(project.ProjectRoot, relative));
        relative = Path.GetRelativePath(project.ProjectRoot, absolute).Replace('\\', '/');
        return Path.IsPathRooted(relative) || relative.StartsWith("../", StringComparison.Ordinal)
            || relative.Split('/')[0].ToUpperInvariant() is ".COOKED" or ".BUILD" or ".PIPELINE" or ".IMPORTED"
            ? throw new InvalidDataException($"Cook dependency '{uri}' does not resolve to a retained authoring file in this project.")
            : new(new Uri($"{AssetUris.Scheme}://{nativePath}.json"), kind, mount.Name, relative, absolute, nativePath, role);
    }
}
