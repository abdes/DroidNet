// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>Owns the browser session's search and filters, independently of catalog scans and cooking.</summary>
public sealed partial class AssetBrowserQuery : ObservableObject
{
    private string searchText = string.Empty;
    private bool clearing;

    /// <summary>Initializes a new instance of the <see cref="AssetBrowserQuery"/> class.</summary>
    public AssetBrowserQuery()
    {
        foreach (var option in this.TypeOptions.Concat(this.StatusOptions))
        {
            option.PropertyChanged += this.OnOptionChanged;
        }
    }

    /// <summary>Occurs once after the effective query changes.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the asset-type choices; no selection includes every type.</summary>
    public IReadOnlyList<AssetFilterOption> TypeOptions { get; } =
    [
        new("Materials", static asset => asset.Kind == AssetKind.Material),
        new("Geometry", static asset => asset.Kind == AssetKind.Geometry),
        new("Scenes", static asset => asset.Kind == AssetKind.Scene),
        new("Textures and images", static asset => asset.Kind is AssetKind.Texture or AssetKind.Image),
        new("Source models", static asset => asset.Kind == AssetKind.ForeignSource),
        new("Other", static asset => asset.Kind is AssetKind.Unknown or AssetKind.ImportSettings or AssetKind.CookedData or AssetKind.CookedTable or AssetKind.Folder),
    ];

    /// <summary>Gets the status choices; no selection includes every status.</summary>
    public IReadOnlyList<AssetFilterOption> StatusOptions { get; } =
    [
        new("Needs cooking", static asset => !asset.IsBuiltin && asset.CookStatus?.Freshness == AssetCookFreshness.NeedsCooking),
        new("Out of date", static asset => !asset.IsBuiltin && asset.CookStatus?.Freshness == AssetCookFreshness.OutOfDate),
        new("Unsaved changes", static asset => asset.CookStatus?.HasUnsavedChanges == true || asset.CookActivity?.State == CookRunState.NeedsSave),
        new("In progress", static asset => asset.CookActivity?.State is CookRunState.Queued or CookRunState.Preparing or CookRunState.Cooking or CookRunState.Validating or CookRunState.Publishing or CookRunState.Cancelling
            || asset.RuntimeAvailability == AssetRuntimeAvailability.Updating),
        new("Cooked", static asset => !asset.IsBuiltin && (asset.CookStatus is { Freshness: AssetCookFreshness.Current, HasVerifiedOutput: true }
            || (asset.CookStatus is null && asset.PrimaryState == AssetState.Cooked))),
        new("Built-in", static asset => asset.IsBuiltin),
        new("Problems", HasProblems),
    ];

    /// <summary>Gets or sets text matched against logical names and visible paths.</summary>
    public string SearchText
    {
        get => this.searchText;
        set
        {
            if (this.SetProperty(ref this.searchText, value ?? string.Empty))
            {
                this.NotifyQueryChanged();
            }
        }
    }

    /// <summary>Gets the number of selected type/status filters.</summary>
    public int FilterCount => this.TypeOptions.Count(static option => option.IsSelected) + this.StatusOptions.Count(static option => option.IsSelected);

    /// <summary>Gets a value indicating whether search or filters restrict the scope.</summary>
    public bool IsActive => this.FilterCount != 0 || !string.IsNullOrWhiteSpace(this.SearchText);

    /// <summary>Gets the compact toolbar label, including an active-filter count.</summary>
    public string FilterLabel => this.FilterCount == 0 ? "Filter" : string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Filter ({this.FilterCount})");

    /// <summary>Gets the reset action label appropriate to the current query.</summary>
    public string ClearQueryLabel => string.IsNullOrWhiteSpace(this.SearchText) ? "Clear filters" : "Clear search and filters";

    /// <summary>Tests an asset against search, type and status without starting work.</summary>
    /// <param name="asset">The latest shared identity and status.</param>
    /// <returns>Whether the asset matches the browser query.</returns>
    public bool Matches(ContentBrowserAssetItem asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var search = this.SearchText.Trim();
        return (search.Length == 0 || asset.DisplayName.Contains(search, StringComparison.OrdinalIgnoreCase)
            || asset.DisplayPath.Contains(search, StringComparison.OrdinalIgnoreCase))
            && MatchesGroup(this.TypeOptions, asset) && MatchesGroup(this.StatusOptions, asset);
    }

    private static bool MatchesGroup(IReadOnlyList<AssetFilterOption> options, ContentBrowserAssetItem asset)
        => !options.Any(static option => option.IsSelected) || options.Any(option => option.IsSelected && option.Matches(asset));

    private static bool HasProblems(ContentBrowserAssetItem asset)
        => asset.HasDiagnostics || asset.PrimaryState is AssetState.Broken or AssetState.Missing
            || asset.CookStatus?.Freshness is AssetCookFreshness.MissingSource or AssetCookFreshness.InvalidSource
            || asset.CookStatus is { HasPublishedOutput: true, HasVerifiedOutput: false }
            || asset.CookActivity?.State == CookRunState.Failed || asset.RuntimeAvailability == AssetRuntimeAvailability.Failed;

    [RelayCommand]
    private void ClearFilters() => this.ClearQuery(includeSearch: false);

    [RelayCommand]
    private void ClearAll() => this.ClearQuery(includeSearch: true);

    private void ClearQuery(bool includeSearch)
    {
        this.clearing = true;
        try
        {
            foreach (var option in this.TypeOptions.Concat(this.StatusOptions))
            {
                option.IsSelected = false;
            }

            if (includeSearch)
            {
                this.SearchText = string.Empty;
            }
        }
        finally
        {
            this.clearing = false;
        }

        this.NotifyQueryChanged();
    }

    private void OnOptionChanged(object? sender, PropertyChangedEventArgs args) => this.NotifyQueryChanged();

    private void NotifyQueryChanged()
    {
        if (!this.clearing)
        {
            this.OnPropertyChanged(nameof(this.FilterCount));
            this.OnPropertyChanged(nameof(this.IsActive));
            this.OnPropertyChanged(nameof(this.FilterLabel));
            this.OnPropertyChanged(nameof(this.ClearQueryLabel));
            this.Changed?.Invoke(this, EventArgs.Empty);
        }
    }
}
