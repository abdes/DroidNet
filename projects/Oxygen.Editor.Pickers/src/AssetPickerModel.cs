// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Pickers;

/// <summary>
/// The choices of an asset picker: the assets a property accepts, grouped by origin (the engine content pak, user
/// paks, the project's content) in that order, filtered by the search text, with rows that keep their identity across
/// updates so that an open picker does not flicker.
/// </summary>
public sealed partial class AssetPickerModel : ObservableObject
{
    private static readonly AssetOrigin[] OriginOrder = [AssetOrigin.Engine, AssetOrigin.Package, AssetOrigin.Content];

    private readonly Func<AssetEntry, AssetChoice> describe;
    private readonly Dictionary<AssetOrigin, AssetChoiceGroup> groupsByOrigin;
    private readonly Dictionary<AssetPath, AssetChoiceRow> rows = [];
    private IReadOnlyList<(AssetOrigin Origin, AssetChoiceRow Row)> ordered = [];
    private string searchText = string.Empty;

    /// <summary>Initializes a new instance of the <see cref="AssetPickerModel"/> class.</summary>
    /// <param name="describe">Describes an accepted asset as the picker's row: its name, type line and glyph.</param>
    /// <param name="clear">The choice that clears the assignment, shown above the groups; none when null.</param>
    public AssetPickerModel(Func<AssetEntry, AssetChoice> describe, AssetChoice? clear = null)
    {
        this.describe = describe ?? throw new ArgumentNullException(nameof(describe));
        if (clear is { Asset: not null })
        {
            throw new ArgumentException("The clearing choice assigns no asset.", nameof(clear));
        }

        this.Clear = clear;
        this.groupsByOrigin = OriginOrder.ToDictionary(static origin => origin, static origin => new AssetChoiceGroup(origin));
    }

    /// <summary>Gets the choice that clears the assignment, or null when the property cannot be cleared.</summary>
    public AssetChoice? Clear { get; }

    /// <summary>Gets a value indicating whether the picker offers to clear the assignment.</summary>
    public bool HasClear => this.Clear is not null;

    /// <summary>Gets the groups that have matching choices, in origin order.</summary>
    public ObservableCollection<AssetChoiceGroup> Groups { get; } = [];

    /// <summary>Gets a value indicating whether no choice matches the search.</summary>
    public bool IsEmpty => this.Groups.Count == 0;

    /// <summary>Gets or sets the text the choices' names and locations are filtered by, ignoring case.</summary>
    public string SearchText
    {
        get => this.searchText;
        set
        {
            if (this.SetProperty(ref this.searchText, value ?? string.Empty))
            {
                this.ApplyVisibleRows();
            }
        }
    }

    /// <summary>Replaces the choices with the assets the property accepts now.</summary>
    /// <param name="accepted">The accepted assets in display order, such as an <see cref="AssetRule"/>'s result.</param>
    public void Update(IReadOnlyList<AssetEntry> accepted)
    {
        ArgumentNullException.ThrowIfNull(accepted);
        var next = new List<(AssetOrigin Origin, AssetChoiceRow Row)>(accepted.Count);
        var kept = new HashSet<AssetPath>();
        foreach (var entry in accepted)
        {
            if (!kept.Add(entry.Identity))
            {
                continue;
            }

            var choice = this.describe(entry);
            if (choice.Asset != entry.Identity)
            {
                throw new InvalidOperationException("A picker row must assign the asset it describes.");
            }

            if (this.rows.TryGetValue(entry.Identity, out var row))
            {
                row.Update(choice);
            }
            else
            {
                row = new AssetChoiceRow(choice);
                this.rows.Add(entry.Identity, row);
            }

            next.Add((entry.Origin, row));
        }

        foreach (var removed in this.rows.Keys.Where(asset => !kept.Contains(asset)).ToArray())
        {
            _ = this.rows.Remove(removed);
        }

        this.ordered = next;
        this.ApplyVisibleRows();
    }

    /// <summary>Finds the current choice of an asset.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The asset's choice, or null when the picker does not offer it.</returns>
    public AssetChoice? Find(AssetPath asset)
        => this.rows.TryGetValue(asset, out var row) ? row.Choice : null;

    // Makes the target hold exactly the wanted rows, in order, moving rows instead of recreating them.
    private static void SyncRows(ObservableCollection<AssetChoiceRow> target, IReadOnlyList<AssetChoiceRow> wanted)
    {
        var wantedSet = wanted.ToHashSet();
        for (var index = target.Count - 1; index >= 0; index--)
        {
            if (!wantedSet.Contains(target[index]))
            {
                target.RemoveAt(index);
            }
        }

        for (var index = 0; index < wanted.Count; index++)
        {
            var current = target.IndexOf(wanted[index]);
            if (current < 0)
            {
                target.Insert(index, wanted[index]);
            }
            else if (current != index)
            {
                target.Move(current, index);
            }
        }
    }

    private bool Matches(AssetChoice choice)
    {
        var search = this.searchText.Trim();
        return search.Length == 0 || choice.Name.Contains(search, StringComparison.OrdinalIgnoreCase)
            || choice.DisplayPath.Contains(search, StringComparison.OrdinalIgnoreCase);
    }

    private void ApplyVisibleRows()
    {
        var position = 0;
        foreach (var origin in OriginOrder)
        {
            var group = this.groupsByOrigin[origin];
            SyncRows(group.Rows, [.. this.ordered.Where(item => item.Origin == origin && this.Matches(item.Row.Choice)).Select(static item => item.Row)]);
            var shown = this.Groups.IndexOf(group);
            if (group.Rows.Count == 0)
            {
                if (shown >= 0)
                {
                    this.Groups.RemoveAt(shown);
                }

                continue;
            }

            if (shown < 0)
            {
                this.Groups.Insert(position, group);
            }

            position++;
        }

        this.OnPropertyChanged(nameof(this.IsEmpty));
    }
}
