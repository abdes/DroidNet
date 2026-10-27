// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>The Level of detail rows: the LOD policy of the selected nodes' shared geometry, as an override of the asset's.</summary>
public sealed partial class GeometryViewModel
{
    /// <summary>Gets the Level of detail rows.</summary>
    public LodPolicyViewModel LevelOfDetail { get; }

    /// <summary>Sets the LOD policy of every selected node, or resets it to the asset's, as one undoable edit.</summary>
    /// <remarks>The rows show the scene afterwards; a refused policy, such as one with boundaries out of order, is reported.</remarks>
    /// <param name="policy">The whole policy, or <see langword="null"/> to reset it.</param>
    /// <param name="label">The history label.</param>
    /// <returns>Completion after the edit and the rows' update.</returns>
    internal async Task SetLodPolicyAsync(LodPolicy? policy, string label)
    {
        this.LevelOfDetail.ShowError(error: null);
        if (!this.TryGetSurfaceEditing(out var nodes, out var authoring))
        {
            this.ProjectSurfaces();
            return;
        }

        var write = new OverrideWrite(SceneSchema.Geometry.LodPolicy.Id, Part: null, LayoutRevision: null, policy);
        var result = await authoring.ExecuteAsync(NodeOverrides.Write(nodes.ToDictionary(this.ReferenceOf, _ => (IReadOnlyList<OverrideWrite>)[write]), label)).ConfigureAwait(true);
        if (!this.IsCurrentSelection(nodes))
        {
            return;
        }

        this.ProjectSurfaces();
        if (!result.Succeeded)
        {
            this.LevelOfDetail.ShowError(result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError));
        }
    }
}
