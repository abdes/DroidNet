// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>How the browser session presents results: order, tile size and the details pane. Never authored data.</summary>
public sealed partial class AssetBrowserPresentation : ObservableObject
{
    /// <summary>The smallest tile edge, in pixels.</summary>
    public const double MinTileSize = 96;

    /// <summary>The largest tile edge, in pixels.</summary>
    public const double MaxTileSize = 256;

    /// <summary>Occurs once after the result order changes.</summary>
    public event EventHandler? SortChanged;

    /// <summary>Gets or sets the field results are ordered by.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SortLabel))]
    public partial AssetSortField SortField { get; set; } = AssetSortField.Name;

    /// <summary>Gets or sets a value indicating whether results run from the largest value to the smallest.</summary>
    [ObservableProperty]
    public partial bool SortDescending { get; set; }

    /// <summary>Gets or sets the tile edge, in pixels.</summary>
    [ObservableProperty]
    public partial double TileSize { get; set; } = 160;

    /// <summary>Gets or sets a value indicating whether the details pane is shown.</summary>
    [ObservableProperty]
    public partial bool IsDetailsPaneOpen { get; set; } = true;

    /// <summary>Gets or sets the results view.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsTilesView), nameof(IsListView), nameof(IsDetailsView))]
    public partial AssetBrowserView View { get; set; }

    /// <summary>Gets a value indicating whether results show as tiles.</summary>
    public bool IsTilesView => this.View == AssetBrowserView.Tiles;

    /// <summary>Gets a value indicating whether results show as a list.</summary>
    public bool IsListView => this.View == AssetBrowserView.List;

    /// <summary>Gets a value indicating whether results show as a table.</summary>
    public bool IsDetailsView => this.View == AssetBrowserView.Details;

    /// <summary>Gets the user-facing name of the sort field.</summary>
    public string SortLabel => GetLabel(this.SortField);

    /// <summary>Returns the user-facing name of a sort field.</summary>
    /// <param name="field">The sort field.</param>
    /// <returns>The field's label.</returns>
    public static string GetLabel(AssetSortField field) => field switch
    {
        AssetSortField.Type => "Type",
        AssetSortField.Status => "Status",
        AssetSortField.Location => "Location",
        AssetSortField.Size => "Size",
        AssetSortField.Modified => "Modified",
        _ => "Name",
    };

    /// <summary>Orders rows by the current field and direction, ties by name and then identity.</summary>
    /// <param name="items">The rows to order.</param>
    /// <returns>The ordered rows.</returns>
    public IReadOnlyList<ContentBrowserAssetItem> Sort(IEnumerable<ContentBrowserAssetItem> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        var keyed = items.Select(item => (Item: item, Facts: this.SortField is AssetSortField.Size or AssetSortField.Modified ? AssetFileFacts.Read(item) : default)).ToList();
        keyed.Sort((left, right) =>
        {
            var order = this.CompareField(left.Item, left.Facts, right.Item, right.Facts);
            if (order == 0)
            {
                order = StringComparer.CurrentCultureIgnoreCase.Compare(left.Item.DisplayName, right.Item.DisplayName);
            }

            if (order == 0)
            {
                order = StringComparer.OrdinalIgnoreCase.Compare(left.Item.IdentityUri.AbsoluteUri, right.Item.IdentityUri.AbsoluteUri);
            }

            return order;
        });
        return [.. keyed.Select(static entry => entry.Item)];
    }

    partial void OnSortFieldChanged(AssetSortField value) => this.SortChanged?.Invoke(this, EventArgs.Empty);

    partial void OnSortDescendingChanged(bool value) => this.SortChanged?.Invoke(this, EventArgs.Empty);

    partial void OnTileSizeChanged(double value)
    {
        var clamped = Math.Clamp(double.IsFinite(value) ? value : MinTileSize, MinTileSize, MaxTileSize);
        if (clamped != value)
        {
            this.TileSize = clamped;
        }
    }

    // Missing values (no file, unknown status) always sort last, whatever the direction.
    private static int CompareMissingLast<T>(T? left, T? right, bool descending)
        where T : struct, IComparable<T>
        => (left, right) switch
        {
            (null, null) => 0,
            (null, _) => 1,
            (_, null) => -1,
            _ => descending ? right!.Value.CompareTo(left!.Value) : left!.Value.CompareTo(right!.Value),
        };

    private static string GetFolder(ContentBrowserAssetItem item)
    {
        var path = item.DisplayPath.Replace('\\', '/');
        var slash = path.LastIndexOf('/');
        return slash <= 0 ? "/" : path[..slash];
    }

    private int CompareField(ContentBrowserAssetItem left, AssetFileFacts leftFacts, ContentBrowserAssetItem right, AssetFileFacts rightFacts)
    {
        var sign = this.SortDescending ? -1 : 1;
        return this.SortField switch
        {
            AssetSortField.Type => sign * StringComparer.CurrentCultureIgnoreCase.Compare(left.TypeDisplayName, right.TypeDisplayName),
            AssetSortField.Status => sign * StringComparer.CurrentCultureIgnoreCase.Compare(left.PrimaryBadge, right.PrimaryBadge),
            AssetSortField.Location => sign * StringComparer.CurrentCultureIgnoreCase.Compare(GetFolder(left), GetFolder(right)),
            AssetSortField.Size => CompareMissingLast(leftFacts.Size, rightFacts.Size, this.SortDescending),
            AssetSortField.Modified => CompareMissingLast(leftFacts.Modified, rightFacts.Modified, this.SortDescending),
            _ => sign * StringComparer.CurrentCultureIgnoreCase.Compare(left.DisplayName, right.DisplayName),
        };
    }

    [RelayCommand]
    private void OrderBy(AssetSortField field) => this.SortField = field;

    [RelayCommand]
    private void OrderAscending() => this.SortDescending = false;

    [RelayCommand]
    private void OrderDescending() => this.SortDescending = true;

    // A column header: a new field sorts ascending, the current field reverses.
    [RelayCommand]
    private void SortBy(AssetSortField field)
    {
        if (this.SortField == field)
        {
            this.SortDescending = !this.SortDescending;
            return;
        }

        this.SortField = field;
        this.SortDescending = false;
    }
}
