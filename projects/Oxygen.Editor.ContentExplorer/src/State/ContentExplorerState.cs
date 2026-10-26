// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>
/// Represents the state of the content explorer, including the selected folders.
/// This is a passive data holder that is updated by ViewModels.
/// </summary>
public sealed partial class ContentExplorerState : INotifyPropertyChanged
{
    private readonly HashSet<AssetPath> selectedFolders = [];

    /// <summary>
    /// Occurs when a property changes.
    /// </summary>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Gets the selected folders; none selected browses the whole project.
    /// </summary>
    public IReadOnlySet<AssetPath> SelectedFolders => this.selectedFolders;

    /// <summary>Gets the search and filter state shared by this browser's layouts.</summary>
    public AssetBrowserQuery Query { get; init; } = new();

    /// <summary>Gets the result order, tile size, view and details-pane state shared by this browser's layouts.</summary>
    public AssetBrowserPresentation Presentation { get; init; } = new();

    /// <summary>Gets the selected assets of the active layout, in selection order; the last is the active asset.</summary>
    public IReadOnlyList<AssetEntry> SelectedAssets { get; private set; } = [];

    /// <summary>Gets the number of results the active layout shows after search and filters.</summary>
    public int ResultCount { get; private set; }

    /// <summary>Gets the number of assets in the current folders before search and filters.</summary>
    public int ScopeCount { get; private set; }

    /// <summary>
    /// Gets the virtual paths Paste copies or moves. The asset list and the sources tree share it, so a folder cut or
    /// copied in either pastes as a folder.
    /// </summary>
    public IReadOnlyList<AssetPath> Clipboard { get; private set; } = [];

    /// <summary>Gets a value indicating whether Paste moves the clipboard's items instead of copying them.</summary>
    public bool IsClipboardCut { get; private set; }

    /// <summary>
    /// Gets why the project's cooked content cannot be shown, when it was published in a format this editor does not
    /// read; cooking the project upgrades it. <see langword="null"/> while the cooked content is readable.
    /// </summary>
    public string? OutdatedPublication { get; private set; }

    /// <summary>Gets or sets the active identity retained when switching asset layouts.</summary>
    internal AssetPath? SelectedIdentity { get; set; }

    /// <summary>Gets or sets every selected identity retained when switching asset layouts.</summary>
    internal IReadOnlyList<AssetPath> SelectedIdentities { get; set; } = [];

    /// <summary>Gets or sets the layout allowed to change the shared asset selection.</summary>
    internal AssetsLayoutViewModel? ActiveAssetLayout { get; set; }

    /// <summary>Gets or sets initial shared discovery, so replacing a layout does not rescan the catalog.</summary>
    internal Task? AssetInitialization { get; set; }

    /// <summary>
    /// Clears all selected folders.
    /// Publishes an empty scope when selection changes.
    /// </summary>
    public void ClearSelection()
    {
        if (this.SelectedFolders.Count > 0)
        {
            this.selectedFolders.Clear();
            this.OnPropertyChanged(nameof(this.SelectedFolders));
        }
    }

    /// <summary>
    /// Sets the selected folders; none selected browses the whole project.
    /// </summary>
    /// <param name="folders">The folders to select.</param>
    public void SetSelectedFolders(IEnumerable<AssetPath> folders)
    {
        ArgumentNullException.ThrowIfNull(folders);
        var replacement = folders.ToHashSet();
        if (this.selectedFolders.SetEquals(replacement))
        {
            return;
        }

        this.selectedFolders.Clear();
        this.selectedFolders.UnionWith(replacement);
        this.OnPropertyChanged(nameof(this.SelectedFolders));
    }

    /// <summary>Puts items on the clipboard, replacing what it held.</summary>
    /// <param name="items">The virtual paths of the assets and folders.</param>
    /// <param name="isCut">Whether Paste moves them instead of copying them.</param>
    public void SetClipboard(IReadOnlyList<AssetPath> items, bool isCut)
    {
        ArgumentNullException.ThrowIfNull(items);
        this.Clipboard = items;
        this.IsClipboardCut = isCut;
        this.OnPropertyChanged(nameof(this.Clipboard));
    }

    /// <summary>Records whether the project's cooked content is in a format this editor does not read.</summary>
    /// <param name="reason">Why it cannot be shown and how to upgrade it; <see langword="null"/> once it is readable.</param>
    public void SetOutdatedPublication(string? reason)
    {
        if (string.Equals(this.OutdatedPublication, reason, StringComparison.Ordinal))
        {
            return;
        }

        this.OutdatedPublication = reason;
        this.OnPropertyChanged(nameof(this.OutdatedPublication));
    }

    /// <summary>Empties the clipboard, as after the cut items were moved.</summary>
    public void ClearClipboard() => this.SetClipboard([], isCut: false);

    /// <summary>Publishes the active layout's selection to the details pane and commands.</summary>
    /// <param name="assets">The selected assets; the last is the active asset.</param>
    internal void PublishSelection(IReadOnlyList<AssetEntry> assets)
    {
        if (this.SelectedAssets.SequenceEqual(assets))
        {
            return;
        }

        this.SelectedAssets = assets;
        this.OnPropertyChanged(nameof(this.SelectedAssets));
    }

    /// <summary>Publishes the active layout's result counts to the footer.</summary>
    /// <param name="resultCount">The results shown after search and filters.</param>
    /// <param name="scopeCount">The assets in the current folders.</param>
    internal void PublishCounts(int resultCount, int scopeCount)
    {
        if (this.ResultCount == resultCount && this.ScopeCount == scopeCount)
        {
            return;
        }

        this.ResultCount = resultCount;
        this.ScopeCount = scopeCount;
        this.OnPropertyChanged(nameof(this.ResultCount));
    }

    private void OnPropertyChanged(string propertyName)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
