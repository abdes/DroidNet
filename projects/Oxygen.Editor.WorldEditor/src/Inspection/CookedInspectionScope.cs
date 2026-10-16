// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.Inspection;

/// <summary>
///     What the Cooked Inspector inspects: a project's cooked output, a folder of it, or one asset's.
/// </summary>
/// <param name="Project">The project whose cooked output is inspected.</param>
/// <param name="ScopeUri">The folder or asset whose cooked output is inspected, or <see langword="null"/> for the whole project.</param>
/// <param name="Validate">Whether the inspection also validates the cooked packages.</param>
public sealed record CookedInspectionScope(ProjectContext Project, Uri? ScopeUri, bool Validate)
{
    /// <summary>Gets the asset whose cooked output is inspected, when one asset is.</summary>
    public Uri? AssetUri { get; init; }

    /// <summary>Gets the cooked source of the asset, when one cooked copy of it is inspected.</summary>
    public CookedAssetMetadata? CookedSource { get; init; }

    /// <summary>
    ///     Tells whether another scope names the same cooked output, whatever it asks to validate.
    /// </summary>
    /// <param name="other">The other scope.</param>
    /// <returns>Whether both scopes name the same project, folder or asset, and cooked root.</returns>
    public bool IsSameOutput(CookedInspectionScope? other)
        => other is not null && other.Project.ProjectId == this.Project.ProjectId
            && string.Equals(other.Project.ProjectRoot, this.Project.ProjectRoot, StringComparison.OrdinalIgnoreCase)
            && other.ScopeUri == this.ScopeUri && other.AssetUri == this.AssetUri
            && string.Equals(other.CookedSource?.RootFolderPath, this.CookedSource?.RootFolderPath, StringComparison.OrdinalIgnoreCase);
}
