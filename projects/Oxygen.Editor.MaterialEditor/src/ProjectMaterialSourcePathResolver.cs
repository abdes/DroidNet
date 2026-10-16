// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Resolves material source URIs against the active project's mounts.
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
        var location = project.FindByAssetUri(materialUri) is { IsMountRoot: false } found
            ? found
            : throw new InvalidOperationException($"Material URI '{materialUri}' does not name a file in one of the project's mounts.");

        // An authoring mount's file is named from the project root; a local folder mount's file from its mount.
        var relativePath = location.MountKind == ProjectMountKind.Authoring
            ? Path.GetRelativePath(project.ProjectRoot, location.FilePath).Replace('\\', '/')
            : location.MountRelativePath;
        return new MaterialSourceLocation(
            MaterialUri: materialUri,
            ProjectRoot: project.ProjectRoot,
            MountName: location.MountName,
            SourcePath: location.FilePath,
            SourceRelativePath: relativePath);
    }
}
