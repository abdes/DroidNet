// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>Puts explorer entries back at places captured from one layout, as the revert of a layout move.</summary>
/// <remarks>The places all refer to the same layout, so the entries return exactly where they were.</remarks>
public sealed record PlaceLayoutEntries : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="PlaceLayoutEntries"/> class.</summary>
    /// <param name="places">The entries and their places.</param>
    public PlaceLayoutEntries(IReadOnlyList<LayoutEntryPosition> places)
    {
        ArgumentNullException.ThrowIfNull(places);
        this.Places = [.. places];
    }

    /// <summary>Gets the entries and their places.</summary>
    public IReadOnlyList<LayoutEntryPosition> Places { get; }

    /// <inheritdoc/>
    public string Label => "Move in Explorer";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = scope.Scene.ExplorerLayout ?? [];
        var moved = this.Places.Select(static place => place.Entry).ToHashSet();
        foreach (var place in this.Places)
        {
            if (SceneLayout.Find(layout, place.Entry) is null)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "An entry to place is no longer in the layout."));
            }

            var owner = place.At.Folder is { } folder ? LayoutEntryId.Of(folder) : place.At.Node is { } node ? LayoutEntryId.Of(node) : (LayoutEntryId?)null;
            if (owner is { } id && (moved.Contains(id) || SceneLayout.Find(layout, id) is null))
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.FolderMissing, "An entry's place is no longer in the layout."));
            }
        }

        return SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var layout = SceneLayout.Of(scope.Scene);
        var before = this.Places.Select(place => place with { At = SceneLayout.PositionOf(layout, place.Entry)! }).ToArray();
        SceneLayout.Place(layout, [.. this.Places.Select(place => (SceneLayout.Find(layout, place.Entry)!.Value.Entry, place.At))]);

        SceneLayout.Prune(layout);
        return new SceneEditApplication.Applied(new PlaceLayoutEntries(before), [new SceneChange.LayoutChanged()]);
    }
}
