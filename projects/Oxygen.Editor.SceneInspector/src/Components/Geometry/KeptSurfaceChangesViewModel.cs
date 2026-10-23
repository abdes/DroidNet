// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>
/// The changes the selected nodes keep for a surface their geometry no longer has, such as one lost on reimport. The
/// cook fails until they are applied to a current surface or removed.
/// </summary>
/// <remarks>
/// No current surface is preselected: a matching name or material does not make two surfaces the same.
/// </remarks>
public sealed partial class KeptSurfaceChangesViewModel : ObservableObject
{
    private readonly GeometryViewModel owner;

    /// <summary>Initializes a new instance of the <see cref="KeptSurfaceChangesViewModel"/> class.</summary>
    /// <param name="owner">The geometry section that edits the changes.</param>
    /// <param name="part">The surface the changes were made for.</param>
    internal KeptSurfaceChangesViewModel(GeometryViewModel owner, PartId part)
    {
        this.owner = owner;
        this.Part = part;

        // The surface is gone, and its name with it; the start of its ID tells two lost surfaces apart.
        this.Name = $"Surface {part.Id.ToString("N")[..8]}";
    }

    /// <summary>Gets the surface the changes were made for.</summary>
    public PartId Part { get; }

    /// <summary>Gets the lost surface's name.</summary>
    public string Name { get; }

    /// <summary>Gets the kept values, such as "Visible Off, Tint.omat".</summary>
    [ObservableProperty]
    public partial string Summary { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the changes can be edited now.</summary>
    [ObservableProperty]
    public partial bool CanEdit { get; private set; }

    /// <summary>Gets the current surfaces that can take the changes.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<SurfaceCardViewModel> Targets { get; private set; } = [];

    /// <summary>Gets or sets the surface the changes are applied to; choosing one applies them, as one undoable edit.</summary>
    public SurfaceCardViewModel? Target
    {
        get => null;
        set
        {
            if (value is not null)
            {
                _ = this.owner.RetargetKeptChangesAsync(this, value);
            }
        }
    }

    /// <summary>Shows the changes the selected nodes keep.</summary>
    /// <param name="nodes">The overrides of each selected node's geometry.</param>
    /// <param name="targets">The current surfaces.</param>
    /// <param name="materialName">Names a material.</param>
    /// <param name="canEdit">Whether the changes can be edited now.</param>
    internal void Update(IReadOnlyList<SourceOverrides> nodes, IReadOnlyList<SurfaceCardViewModel> targets, Func<AssetPath, string> materialName, bool canEdit)
    {
        this.Targets = targets;
        this.CanEdit = canEdit;
        var kept = nodes.SelectMany(overrides => overrides.All.Where(entry => entry.Part == this.Part)).ToList();
        this.Summary = string.Join(", ", kept.GroupBy(static entry => entry.Property).Select(values => Describe(values.Key, [.. values.Select(static entry => entry.Value).Distinct()], materialName)));
        this.OnPropertyChanged(nameof(this.Target));
    }

    private static string Describe(PropertyId property, List<object> values, Func<AssetPath, string> materialName)
    {
        var name = SurfaceLabels.Of(property);
        return values.Count > 1 ? $"{name} mixed"
            : values[0] switch
            {
                bool flag => $"{name} {(flag ? "On" : "Off")}",
                AssetPath material => materialName(material),
                var value => $"{name} {value}",
            };
    }

    [RelayCommand]
    private Task RemoveAsync() => this.owner.RemoveKeptChangesAsync(this);
}
