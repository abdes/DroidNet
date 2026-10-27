// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>
/// The Selected mesh group of the Model Editor's import settings (model editor 5.3, M5): the asset defaults the import
/// writes into the selected mesh's geometry, which every placement of the model starts from: its LOD policy, and each
/// surface's visibility and shadow casting.
/// </summary>
/// <remarks>
/// The settings key the defaults by the geometry's anchor. A default not set shows what the last import gave; the view
/// shows the last import until Save, which reimports with the defaults (ME-6).
/// </remarks>
public sealed partial class SelectedMeshGroup : ObservableObject
{
    private readonly ModelContent model;

    /// <summary>Initializes a new instance of the <see cref="SelectedMeshGroup"/> class.</summary>
    /// <param name="model">The model document's content, whose settings the group edits.</param>
    /// <param name="name">The selected node's name.</param>
    /// <param name="anchor">The anchor of the node's geometry, which keys its defaults.</param>
    /// <param name="geometry">The geometry as the last import produced it.</param>
    public SelectedMeshGroup(ModelContent model, string name, string anchor, GeometrySurfaceMetadata geometry)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(geometry);
        this.model = model;
        this.Anchor = anchor;
        this.Geometry = geometry;
        this.Title = $"Selected mesh: {name}";
        this.LevelOfDetail = new LodPolicyEditorModel(this.WriteLodPolicyAsync);
        this.Surfaces = [.. geometry.Surfaces.Select(surface => new SurfaceDefaultsRow(this, surface))];
        this.Refresh();
    }

    /// <summary>Gets the group's title, such as "Selected mesh: Body".</summary>
    public string Title { get; }

    /// <summary>Gets the anchor of the geometry, which keys its defaults.</summary>
    public string Anchor { get; }

    /// <summary>Gets the geometry as the last import produced it.</summary>
    public GeometrySurfaceMetadata Geometry { get; }

    /// <summary>Gets the geometry's default LOD policy.</summary>
    public LodPolicyEditorModel LevelOfDetail { get; }

    /// <summary>Gets each surface's defaults.</summary>
    public IReadOnlyList<SurfaceDefaultsRow> Surfaces { get; }

    /// <summary>Shows the defaults the document's settings hold now.</summary>
    public void Refresh()
    {
        if (this.model.Record is not { } record)
        {
            this.LevelOfDetail.Clear();
            return;
        }

        var set = ModelGeometryDefaults.LodPolicyOf(record.Settings, this.Anchor);
        var imported = this.Geometry.LodPolicy;
        var lodCount = this.Geometry.LodCount;
        this.LevelOfDetail.Show(new LodPolicyEditorState(set ?? imported, imported, lodCount)
        {
            IsAvailable = lodCount > 1 || set is not null,
            OverrideState = set is null ? OverrideState.None : OverrideState.Full,
            ResetToolTip = $"Reset to the policy the import gives ({LodPolicyShapes.NameOf(imported)})",
            Summary = set is null
                ? string.Create(CultureInfo.CurrentCulture, $"{lodCount} {(lodCount == 1 ? "LOD" : "LODs")}, as the import gives them")
                : "Set for every placement of the model",
        });
        foreach (var surface in this.Surfaces)
        {
            surface.Refresh(record.Settings);
        }
    }

    /// <summary>Sets or resets a surface's default, as one step of the document's history.</summary>
    /// <param name="surface">The surface.</param>
    /// <param name="flag">The default.</param>
    /// <param name="value">The value, or <see langword="null"/> to take the import's.</param>
    internal void WriteSurface(GeometrySurface surface, ModelSurfaceFlag flag, bool? value)
    {
        if (this.model.Record is not { } record)
        {
            return;
        }

        var label = flag == ModelSurfaceFlag.Visible ? "visibility" : "shadow casting";
        this.model.EditSettings(
            ModelGeometryDefaults.WithSurfaceFlag(record.Settings, this.Anchor, surface.SurfaceId, flag, value),
            $"Edit the {label} of {surface.DisplayName}");
    }

    private Task WriteLodPolicyAsync(LodPolicy? policy, string label)
    {
        if (this.model.Record is { } record)
        {
            this.model.EditSettings(ModelGeometryDefaults.WithLodPolicy(record.Settings, this.Anchor, policy), label);
        }

        return Task.CompletedTask;
    }
}
