// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>The Level of detail rows: the LOD policy of the selected nodes' shared geometry, as an override of the asset's.</summary>
/// <remarks>
/// The rows edit the policy as the nodes' override; reset removes it so the asset's policy applies. A value the scene
/// refuses, such as boundaries out of order, is reported with its diagnostic. The rows show the scene and the inventory
/// after each edit.
/// </remarks>
public sealed partial class GeometryViewModel
{
    /// <summary>Gets the Level of detail rows.</summary>
    public LodPolicyEditorModel LevelOfDetail { get; }

    /// <summary>Sets the LOD policy of every selected node, or resets it to the asset's, as one undoable edit.</summary>
    /// <remarks>The rows show the scene afterwards; a refused policy, such as one with boundaries out of order, is reported.</remarks>
    /// <param name="policy">The whole policy, or <see langword="null"/> to reset it.</param>
    /// <param name="label">The history label.</param>
    /// <returns>Completion after the edit and the rows' update.</returns>
    internal async Task SetLodPolicyAsync(LodPolicy? policy, string label)
    {
        this.LevelOfDetail.ClearError();
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
        if (!result.Succeeded && result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError) is { } error)
        {
            this.LevelOfDetail.ShowError(error.Code, error.Message);
        }
    }

    private static string LodSummaryOf(bool mixed, int overriding, int count, int lodCount, LodPolicy asset)
    {
        if (mixed)
        {
            return "Mixed: the selected nodes use different policies";
        }

        if (overriding == 0)
        {
            return string.Create(CultureInfo.CurrentCulture, $"{lodCount} {(lodCount == 1 ? "LOD" : "LODs")}, from the asset");
        }

        var source = $"the asset uses {LodPolicyShapes.NameOf(asset)}";
        return overriding == count ? $"Overridden; {source}" : string.Create(CultureInfo.CurrentCulture, $"Overridden on {overriding} of {count} nodes; {source}");
    }

    private LodPolicyEditorModel CreateLevelOfDetail()
        => new(this.SetLodPolicyAsync)
        {
            Actions = new InspectorOverrideActions(() => this.ApplyActionsAsync([SceneSchema.Geometry.LodPolicy.Id], part: null)),
        };

    // The selected nodes' policy: their shared override or the asset's, as an override of the asset's policy.
    private void ShowLevelOfDetail(GeometrySurfaceMetadata inventory, IReadOnlyList<GeometryOverrides> nodes, bool canEdit)
    {
        var asset = inventory.LodPolicy;
        var lodCount = inventory.LodCount;
        var policies = nodes.Select(overrides => overrides.Values.TryGet(SceneSchema.Geometry.LodPolicy, out var policy) ? policy : asset).ToList();
        var agreed = policies.TrueForAll(policy => policy.Equals(policies[0])) ? policies[0] : null;
        var overriding = nodes.Count(overrides => overrides.Changes.TryGet(SceneSchema.Geometry.LodPolicy, out _));
        var reset = $"Reset to the asset's policy ({LodPolicyShapes.NameOf(asset)})";

        // A single LOD always shows, so its policy changes nothing; an override, such as one kept from a geometry with
        // more LODs, still shows so it can be reset.
        this.LevelOfDetail.Show(new LodPolicyEditorState(agreed, asset, lodCount)
        {
            IsAvailable = lodCount > 1 || overriding > 0,
            CanEdit = canEdit,
            OverrideState = overriding == 0 ? OverrideState.None : overriding == nodes.Count ? OverrideState.Full : OverrideState.Partial,
            ResetToolTip = overriding == nodes.Count ? reset : string.Create(CultureInfo.CurrentCulture, $"{reset}, on the {overriding} of {nodes.Count} nodes that change it"),
            Summary = LodSummaryOf(agreed is null, overriding, nodes.Count, lodCount, asset),
        });
    }
}
