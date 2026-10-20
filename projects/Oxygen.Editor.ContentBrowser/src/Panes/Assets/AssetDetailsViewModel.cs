// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Globalization;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>
/// The docked details pane: everything known about the selected asset, or a summary of a multi-selection.
/// It reads the current catalog snapshot and file facts only; it never loads, cooks or changes an asset.
/// </summary>
public sealed partial class AssetDetailsViewModel : ObservableObject, IDisposable
{
    // Lets long paths wrap after each separator instead of in the middle of a name.
    private const char PathBreak = '​';

    private readonly ContentBrowserState state;
    private readonly IAssetShell shell;
    private readonly Relocation.IAssetRelocationWorkflow? relocation;
    private readonly IScripts? scripts;
    private readonly IDisposable? scriptChanges;
    private int referrersRequest;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AssetDetailsViewModel"/> class.</summary>
    /// <param name="state">The browser state that publishes the selection.</param>
    /// <param name="shell">The clipboard and File Explorer actions.</param>
    /// <param name="relocation">Finds the content that uses the selected asset; without it no referrers are shown.</param>
    /// <param name="scripts">The scripts, which supply a script's parameters and problems.</param>
    /// <param name="hosting">The UI host, whose dispatcher shows the scripts' changes; without it they are shown on the
    /// thread that raises them.</param>
    public AssetDetailsViewModel(
        ContentBrowserState state,
        IAssetShell shell,
        Relocation.IAssetRelocationWorkflow? relocation = null,
        IScripts? scripts = null,
        HostingContext? hosting = null)
    {
        this.state = state;
        this.shell = shell;
        this.relocation = relocation;
        this.scripts = scripts;
        state.PropertyChanged += this.OnStateChanged;
        this.scriptChanges = scripts is null ? null : Observable.FromEventPattern<ScriptChangedEventArgs>(
                handler => scripts.Changed += handler,
                handler => scripts.Changed -= handler)
            .ObserveOn((IScheduler?)hosting?.DispatcherScheduler ?? ImmediateScheduler.Instance)
            .Subscribe(change => this.OnScriptChanged(change.EventArgs));
        this.Refresh();
    }

    /// <summary>Gets the single selected asset, or null for none or several.</summary>
    [ObservableProperty]
    public partial AssetEntry? Asset { get; private set; }

    /// <summary>Gets the selected asset's row presentation: swatch, status tone and file facts.</summary>
    [ObservableProperty]
    public partial AssetBrowserRow? Row { get; private set; }

    /// <summary>Gets the labelled facts about the selection.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<AssetInformationFact> Facts { get; private set; } = [];

    /// <summary>Gets one line per selected asset when several are selected: name, type and size.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<AssetInformationFact> SelectedItems { get; private set; } = [];

    /// <summary>Gets the pane title: the asset name, or the number of selected assets.</summary>
    [ObservableProperty]
    public partial string Title { get; private set; } = string.Empty;

    /// <summary>Gets the status explanation for a single asset.</summary>
    [ObservableProperty]
    public partial string Description { get; private set; } = string.Empty;

    /// <summary>Gets the authored files that use the selected asset.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<string> Referrers { get; private set; } = [];

    /// <summary>Gets the referrer summary: who uses the asset, or that nothing does.</summary>
    [ObservableProperty]
    public partial string ReferrersSummary { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether referrer information is shown.</summary>
    [ObservableProperty]
    public partial bool HasReferrerInfo { get; private set; }

    /// <summary>Gets a value indicating whether nothing is selected.</summary>
    public bool IsEmpty => this.state.SelectedAssets.Count == 0;

    /// <summary>Gets a value indicating whether anything is selected.</summary>
    public bool HasSelection => !this.IsEmpty;

    /// <summary>Gets a value indicating whether exactly one asset is selected.</summary>
    public bool IsSingle => this.Asset is not null;

    /// <summary>Gets a value indicating whether several assets are selected.</summary>
    public bool IsMultiple => this.state.SelectedAssets.Count > 1;

    /// <summary>Returns a path that wraps after each separator, for display only.</summary>
    /// <param name="path">The path.</param>
    /// <returns>The path with a zero-width break after each separator.</returns>
    public static string WithPathBreaks(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return path.Replace("/", "/" + PathBreak, StringComparison.Ordinal).Replace("\\", "\\" + PathBreak, StringComparison.Ordinal);
    }

    /// <summary>Builds the facts shown for one asset, in reading order.</summary>
    /// <param name="asset">The asset.</param>
    /// <param name="file">The asset's file facts.</param>
    /// <returns>The labelled facts.</returns>
    public static IReadOnlyList<AssetInformationFact> BuildFacts(AssetEntry asset, AssetFileFacts file)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var facts = new List<AssetInformationFact>
        {
            new("Type", asset.TypeDisplayName),
            new("Logical path", WithPathBreaks(asset.Identity.VirtualPath)),
        };
        if (file.Path is { } path)
        {
            facts.Add(new("File", WithPathBreaks(path)));
        }

        if (file.Size is not null)
        {
            facts.Add(new("Size", file.SizeText));
        }

        if (file.Modified is not null)
        {
            facts.Add(new("Modified", file.ModifiedText));
        }

        if (asset.AssetGuid is { Length: > 0 } guid)
        {
            facts.Add(new("Identity", guid));
        }

        // Origin and published output come from the shared information; its source duplicates File.
        facts.AddRange(asset.Information.Facts
            .Where(static fact => !string.Equals(fact.Label, "Source", StringComparison.Ordinal) || fact.Value.Contains("Unavailable", StringComparison.Ordinal))
            .Select(static fact => fact with { Value = fact.Value.Contains('/', StringComparison.Ordinal) ? WithPathBreaks(fact.Value) : fact.Value }));
        if (asset.HasDiagnostics)
        {
            facts.Add(new("Diagnostics", asset.DiagnosticsText));
        }

        return facts;
    }

    /// <summary>Builds the type and status counts shown for a multi-selection.</summary>
    /// <param name="assets">The selected assets.</param>
    /// <returns>The labelled counts.</returns>
    public static IReadOnlyList<AssetInformationFact> BuildSummaryFacts(IReadOnlyList<AssetEntry> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        return
        [
            new("Types", Count(assets.Select(static asset => asset.TypeDisplayName))),
            new("Statuses", Count(assets.Select(static asset => asset.PrimaryBadge))),
        ];

        static string Count(IEnumerable<string> values)
            => string.Join(
                Environment.NewLine,
                values.GroupBy(static value => value, StringComparer.Ordinal)
                    .OrderByDescending(static group => group.Count())
                    .ThenBy(static group => group.Key, StringComparer.CurrentCulture)
                    .Select(static group => string.Create(CultureInfo.CurrentCulture, $"{group.Key}: {group.Count()}")));
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (!this.disposed)
        {
            this.disposed = true;
            this.state.PropertyChanged -= this.OnStateChanged;
            this.scriptChanges?.Dispose();
        }
    }

    private void OnStateChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(ContentBrowserState.SelectedAssets), StringComparison.Ordinal))
        {
            this.Refresh();
        }
    }

    private void Refresh()
    {
        var assets = this.state.SelectedAssets;
        if (assets is [var asset])
        {
            var row = new AssetBrowserRow(asset);
            this.Asset = asset;
            this.Row = row;
            this.Title = asset.DisplayName;
            this.Description = asset.Information.Description;
            this.Facts = [.. BuildFacts(asset, row.FileFacts), .. this.BuildSourceFacts(asset)];
            this.SelectedItems = [];
        }
        else
        {
            this.Asset = null;
            this.Row = null;
            this.Title = assets.Count == 0 ? "No selection" : string.Create(CultureInfo.CurrentCulture, $"{assets.Count} assets selected");
            this.Description = assets.Count == 0 ? "Select an asset to see its details." : string.Empty;
            this.Facts = assets.Count == 0 ? [] : BuildSummaryFacts(assets);
            this.SelectedItems = [.. assets.Select(static asset => new AssetInformationFact(
                asset.DisplayName,
                string.Join(" · ", new[] { asset.TypeDisplayName, AssetFileFacts.Read(asset).SizeText }.Where(static part => part.Length > 0))))];
        }

        _ = this.RefreshReferrersAsync(assets is [var single] ? single.Identity : null);
        this.OnPropertyChanged(nameof(this.IsEmpty));
        this.OnPropertyChanged(nameof(this.HasSelection));
        this.OnPropertyChanged(nameof(this.IsSingle));
        this.OnPropertyChanged(nameof(this.IsMultiple));
        this.CopyPathCommand.NotifyCanExecuteChanged();
        this.LocateCommand.NotifyCanExecuteChanged();
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Referrers are informational; a failed scan only hides them.")]
    private async Task RefreshReferrersAsync(AssetPath? asset)
    {
        var request = ++this.referrersRequest;
        this.Referrers = [];
        this.ReferrersSummary = string.Empty;
        this.HasReferrerInfo = false;
        if (this.relocation is null || asset is null)
        {
            return;
        }

        IReadOnlyList<AssetPath> referrers;
        try
        {
            referrers = await this.relocation.FindReferrersAsync(asset, CancellationToken.None).ConfigureAwait(true);
        }
        catch (Exception)
        {
            return;
        }

        if (request != this.referrersRequest || this.disposed)
        {
            return;
        }

        this.Referrers = [.. referrers.Select(static referrer => WithPathBreaks(referrer.VirtualPath))];
        this.ReferrersSummary = referrers.Count == 0
            ? "No other content uses it."
            : string.Create(CultureInfo.CurrentCulture, $"Used by {referrers.Count} {(referrers.Count == 1 ? "file" : "files")}:");
        this.HasReferrerInfo = true;
    }

    private bool CanCopyPath() => this.state.SelectedAssets.Count > 0;

    [RelayCommand(CanExecute = nameof(CanCopyPath))]
    private void CopyPath() => this.shell.CopyText(AssetsViewModel.GetCopyPathText(this.state.SelectedAssets));

    private bool CanLocate() => this.Row?.FileFacts.Path is not null;

    [RelayCommand(CanExecute = nameof(CanLocate))]
    private void Locate()
    {
        if (this.Row?.FileFacts.Path is { } path)
        {
            _ = this.shell.ShowInFileExplorer(path);
        }
    }
}
