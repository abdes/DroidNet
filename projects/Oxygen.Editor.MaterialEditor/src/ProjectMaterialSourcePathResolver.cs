// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Resolves material source URIs against the active project authoring mounts.
/// </summary>
public sealed class ProjectMaterialSourcePathResolver : IMaterialSourcePathResolver
{
    private readonly IProjectContextService projectContextService;

    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectMaterialSourcePathResolver"/> class.
    /// </summary>
    /// <param name="projectContextService">The active project context service.</param>
    public ProjectMaterialSourcePathResolver(IProjectContextService projectContextService)
    {
        this.projectContextService = projectContextService ?? throw new ArgumentNullException(nameof(projectContextService));
    }

    /// <inheritdoc />
    public MaterialSourceLocation Resolve(Uri materialUri)
    {
        ArgumentNullException.ThrowIfNull(materialUri);

        if (!string.Equals(materialUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"Material URI '{materialUri}' does not use the asset scheme.");
        }

        var project = this.projectContextService.ActiveProject
            ?? throw new InvalidOperationException("No active project is available for material source resolution.");

        var path = Uri.UnescapeDataString(materialUri.AbsolutePath).TrimStart('/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            throw new InvalidOperationException($"Material URI '{materialUri}' does not include an authoring mount.");
        }

        var mountName = path[..slash];
        var mountRelativePath = path[(slash + 1)..];
        if (string.IsNullOrWhiteSpace(mountRelativePath)
            || Path.IsPathRooted(mountRelativePath)
            || mountRelativePath.Split('/', '\\').Any(static segment => segment is ".." or "."))
        {
            throw new InvalidOperationException($"Material URI '{materialUri}' contains an invalid material path.");
        }

        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase))
            ?? throw new InvalidOperationException($"Material URI '{materialUri}' targets unknown mount '{mountName}'.");

        var sourceRelativePath = NormalizeRelativePath(Path.Combine(mount.RelativePath, mountRelativePath));
        var mountRoot = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
        var sourcePath = Path.GetFullPath(Path.Combine(project.ProjectRoot, sourceRelativePath));
        if (!IsUnderRoot(sourcePath, mountRoot))
        {
            throw new InvalidOperationException($"Material URI '{materialUri}' resolves outside authoring mount '{mountName}'.");
        }

        return new MaterialSourceLocation(
            MaterialUri: materialUri,
            ProjectRoot: project.ProjectRoot,
            MountName: mount.Name,
            SourcePath: sourcePath,
            SourceRelativePath: sourceRelativePath);
    }

    private static string NormalizeRelativePath(string path)
        => path.Replace('\\', '/').TrimStart('/');

    private static bool IsUnderRoot(string path, string root)
    {
        var normalizedRoot = root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                             + Path.DirectorySeparatorChar;
        var normalizedPath = path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                             + Path.DirectorySeparatorChar;
        return normalizedPath.StartsWith(normalizedRoot, StringComparison.OrdinalIgnoreCase);
    }
}
