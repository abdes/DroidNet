// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Serilog.Events;

namespace Oxygen.Editor.Cooking;

/// <summary>Maintains stable presentation collections for one session cook.</summary>
public sealed partial class CookingRunViewModel : ObservableObject
{
    /// <summary>The segmented switch's index of the Output view.</summary>
    internal const int OutputView = 0;

    /// <summary>The segmented switch's index of the Issues view.</summary>
    internal const int IssuesView = 1;

    /// <summary>The segmented switch's index of the Assets view.</summary>
    internal const int AssetsView = 2;

    private readonly Dictionary<AssetPath, CookingAssetViewModel> assetRows = [];
    private CookRunSnapshot snapshot;

    /// <summary>Initializes a new instance of the <see cref="CookingRunViewModel"/> class.</summary>
    /// <param name="snapshot">The initial coordinator state.</param>
    public CookingRunViewModel(CookRunSnapshot snapshot)
    {
        this.snapshot = snapshot;
        this.Apply(snapshot);
    }

    /// <summary>Gets the owning coordinator snapshot.</summary>
    public CookRunSnapshot Snapshot => this.snapshot;

    /// <summary>Gets the scope name.</summary>
    public string Name => this.snapshot.DisplayName;

    /// <summary>Gets concise secondary scope information.</summary>
    public string Kind => this.snapshot.Request.Import is not null || this.snapshot.Request.IsReimport || (this.snapshot.Request.ScopeUri is { } source && AssetFileTypes.Is(source.Name, AssetFileCategory.Model)) ? "Source" : this.snapshot.Request.TargetKind switch
    {
        CookTargetKind.Project => "Project",
        CookTargetKind.Folder => "Folder",
        CookTargetKind.CurrentScene => "Scene",
        CookTargetKind.Changes => "Changes",
        _ => (this.snapshot.Request.ScopeUri is { } asset ? AssetKinds.FromFileName(AssetFileTypes.ToIdentity(asset).Name) : AssetKind.Material) switch
        {
            AssetKind.Scene => "Scene",
            AssetKind.Geometry => "Geometry",
            AssetKind.Texture => "Texture",
            AssetKind.Script => "Script",
            AssetKind.InputFile => "Input",
            _ => "Material",
        },
    };

    /// <summary>Gets the secondary list context.</summary>
    public string Context => this.Kind + (this.snapshot.Request.IsAutomatic ? " · Automatic" : string.Empty);

    /// <summary>Gets the compact selected-run title and type.</summary>
    public string Title => $"{this.Name} ({this.Kind})";

    /// <summary>Gets one readable status for the selected run.</summary>
    public string Status => GetStatus(this.snapshot.State);

    /// <summary>Gets the operation identity, origin and outcome for assistive technology.</summary>
    public string AccessibleName => $"{this.Name}, {this.Context}, {this.Status}";

    /// <summary>Gets the status glyph used consistently in both lists.</summary>
    public string StatusGlyph => GetGlyph(this.snapshot.State);

    /// <summary>Gets a value indicating whether indeterminate progress is appropriate.</summary>
    public bool IsBusy => !this.snapshot.IsCompleted && this.snapshot.State is not CookRunState.Queued;

    /// <summary>Gets visibility of live progress, hiding the idle track after completion.</summary>
    public Visibility ProgressVisibility => this.IsBusy ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility for applicable retry actions.</summary>
    public Visibility RetryVisibility => this.snapshot.IsCompleted && this.snapshot.State is CookRunState.Failed or CookRunState.Cancelled ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility for cancellation while work remains owned.</summary>
    public Visibility CancelVisibility => this.snapshot.IsCompleted ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets visibility of published-output inspection after the selected operation has finished.</summary>
    public Visibility InspectVisibility => this.snapshot.IsCompleted && (this.snapshot.Request.TargetKind == CookTargetKind.Project
        || this.snapshot.Request.ScopeUri is not null) ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of explicit navigation to the successful run's imported assets.</summary>
    public Visibility ShowImportedAssetsVisibility => this.snapshot.ImportedOutputs.IsEmpty ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets a value indicating whether cancellation can still be requested.</summary>
    public bool CanCancel => !this.snapshot.IsCompleted && this.snapshot.State != CookRunState.Cancelling;

    /// <summary>Gets this run's transcript, independent of the global console.</summary>
    public OutputEntryCollection Output { get; } = [];

    /// <summary>Gets the assets the run's Assets view shows: those that changed or need attention, plus reused ones on request.</summary>
    public ObservableCollection<CookingAssetViewModel> Assets { get; } = [];

    /// <summary>Gets the outcome counts of the run, one sentence the user reads before any list.</summary>
    public string Summary
    {
        get
        {
            var counts = this.snapshot.Assets.Values.GroupBy(static asset => asset.State).ToDictionary(static group => group.Key, static group => group.Count());
            int Of(params CookAssetState[] states) => states.Sum(state => counts.GetValueOrDefault(state));
            var parts = new List<string>
            {
                $"{Of(CookAssetState.Reused)} reused",
                $"{Of(CookAssetState.Updated)} updated",
                $"{Of(CookAssetState.Skipped, CookAssetState.Unresolved)} skipped",
                $"{Of(CookAssetState.Failed)} failed",
            };
            return string.Join(" · ", parts);
        }
    }

    /// <summary>Gets what the warnings amount to, by kind, or nothing when there are none.</summary>
    public string WarningSummary
    {
        get
        {
            var warnings = this.snapshot.Diagnostics.Where(static diagnostic => diagnostic.Severity == DiagnosticSeverity.Warning).ToArray();
            if (warnings.Length == 0)
            {
                return string.Empty;
            }

            var kinds = warnings.Select(static diagnostic => (diagnostic.Code, diagnostic.Message)).Distinct().Count();
            return string.Create(System.Globalization.CultureInfo.CurrentCulture, $"{warnings.Length} warnings in {kinds} {(kinds == 1 ? "kind" : "kinds")}");
        }
    }

    /// <summary>Gets visibility of the warning summary.</summary>
    public Visibility WarningSummaryVisibility => this.WarningSummary.Length == 0 ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets the Assets view's label with the number of assets in the run.</summary>
    public string AssetsLabel => string.Create(System.Globalization.CultureInfo.CurrentCulture, $"Assets · {this.snapshot.Assets.Count}");

    /// <summary>Gets the run's issues, one per kind of problem, most serious first.</summary>
    public CookingIssueList Issues { get; } = [];

    [ObservableProperty]
    public partial bool FollowTail { get; set; } = true;

    /// <summary>Gets or sets the shown view, one of the view indexes. Only the shown one is laid out.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsAssetsView))]
    [NotifyPropertyChangedFor(nameof(OutputVisibility))]
    [NotifyPropertyChangedFor(nameof(IssuesVisibility))]
    [NotifyPropertyChangedFor(nameof(AssetsVisibility))]
    [NotifyPropertyChangedFor(nameof(NoIssuesVisibility))]
    [NotifyPropertyChangedFor(nameof(IssueDetailsVisibility))]
    public partial int ViewIndex { get; set; }

    /// <summary>Gets or sets the issue whose places the details pane shows.</summary>
    [ObservableProperty]
    public partial CookingIssueViewModel? SelectedIssue { get; set; }

    /// <summary>Gets or sets a value indicating whether reused assets are listed as well.</summary>
    [ObservableProperty]
    public partial bool ShowReused { get; set; }

    /// <summary>Gets or sets a value indicating whether the Assets view is shown.</summary>
    public bool IsAssetsView
    {
        get => this.ViewIndex == AssetsView;
        set => this.ViewIndex = value ? AssetsView : OutputView;
    }

    /// <summary>Gets visibility of the Output view.</summary>
    public Visibility OutputVisibility => this.ViewIndex == OutputView ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of the Issues view.</summary>
    public Visibility IssuesVisibility => this.ViewIndex == IssuesView ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of the Assets view.</summary>
    public Visibility AssetsVisibility => this.IsAssetsView ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of the empty-Issues message.</summary>
    public Visibility NoIssuesVisibility => this.ViewIndex == IssuesView && this.Issues.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of the issue list and its details pane: only a run with issues has them.</summary>
    public Visibility IssueDetailsVisibility => this.ViewIndex == IssuesView && this.Issues.Count > 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the Issues view's label with the number of diagnostics in the run.</summary>
    public string IssuesLabel => this.Issues.Total == 0 ? "Issues" : string.Create(System.Globalization.CultureInfo.CurrentCulture, $"Issues · {this.Issues.Total}");

    /// <summary>Gets or sets the selected transcript entries retained across view changes.</summary>
    internal IReadOnlyList<OutputLogEntry> SelectedOutputEntries { get; set; } = [];

    /// <summary>Applies ordered state without replacing existing transcript items.</summary>
    /// <param name="next">A snapshot from the owning coordinator.</param>
    public void Apply(CookRunSnapshot next)
    {
        if (next.Revision < this.snapshot.Revision)
        {
            return;
        }

        this.snapshot = next;
        if (next.Messages.Count > this.Output.Count)
        {
            this.Output.AddRange([.. next.Messages.Skip(this.Output.Count).Select(static message => new OutputLogEntry
            {
                Timestamp = message.Timestamp,
                Message = message.Text,
                Properties = new Dictionary<string, object?>(StringComparer.Ordinal) { ["Detail"] = string.IsNullOrWhiteSpace(message.Detail) ? null : message.Detail },
                Level = message.Severity switch
                {
                    DiagnosticSeverity.Warning => LogEventLevel.Warning,
                    DiagnosticSeverity.Error => LogEventLevel.Error,
                    DiagnosticSeverity.Fatal => LogEventLevel.Fatal,
                    _ => LogEventLevel.Information,
                },
            })]);
        }

        this.UpdateAssets(next.Assets);

        foreach (var diagnostic in next.Diagnostics)
        {
            var key = diagnostic.AffectedVirtualPath ?? diagnostic.AffectedEntity?.AssetVirtualPath ?? string.Empty;
            this.Issues.Report(diagnostic, diagnostic.AffectedEntity?.SceneName ?? GetAssetName(key, this.Name));
        }

        this.Issues.Reorder();
        if (this.SelectedIssue is null || !this.Issues.Contains(this.SelectedIssue))
        {
            this.SelectedIssue = this.Issues.FirstOrDefault();
        }

        this.OnPropertyChanged(string.Empty);
    }

    /// <summary>Extracts a readable descriptor name from its stable source path.</summary>
    /// <param name="path">The asset source path.</param>
    /// <param name="fallback">The display name for a project-wide issue.</param>
    /// <returns>The logical name.</returns>
    internal static string GetAssetName(string path, string fallback)
        => string.IsNullOrWhiteSpace(path) ? fallback : Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(path));

    /// <summary>Maps execution state to the shared status icon vocabulary.</summary>
    /// <param name="state">The observed state.</param>
    /// <returns>The Fluent status glyph.</returns>
    internal static string GetGlyph(CookRunState state) => state switch
    {
        CookRunState.Succeeded or CookRunState.UpToDate => "\uE73E",
        CookRunState.SucceededWithWarnings => "\uE7BA",
        CookRunState.Failed => "\uEA39",
        CookRunState.Cancelled => "\uE711",
        CookRunState.Queued => "\uE823",
        _ => "\uE895",
    };

    /// <summary>Maps execution state to its compact visible label.</summary>
    /// <param name="state">The observed state.</param>
    /// <returns>The user-facing status.</returns>
    internal static string GetStatus(CookRunState state) => state switch
    {
        CookRunState.Succeeded => "Complete",
        CookRunState.SucceededWithWarnings => "Warnings",
        CookRunState.UpToDate => "Up to date",
        CookRunState.Failed => "Failed",
        CookRunState.Publishing => "Updating preview",
        _ => state.ToString(),
    };

    partial void OnShowReusedChanged(bool value) => this.UpdateAssets(this.snapshot.Assets);

    // The switch reports -1 while its items change; a view is always shown.
    partial void OnViewIndexChanged(int value)
    {
        if (value is < OutputView or > AssetsView)
        {
            this.ViewIndex = OutputView;
        }
    }

    // The list clears its selection when it takes another run's issues; the details pane always shows an issue.
    partial void OnSelectedIssueChanged(CookingIssueViewModel? value)
    {
        if (value is null && this.Issues.Count > 0)
        {
            this.SelectedIssue = this.Issues[0];
        }
    }

    // Rows exist for every asset, so a reused one that becomes listed keeps its identity; the list holds only those worth reading.
    private void UpdateAssets(ImmutableDictionary<AssetPath, CookRunAsset> assets)
    {
        foreach (var asset in assets.Values)
        {
            if (!this.assetRows.TryGetValue(asset.AssetUri, out var existing))
            {
                this.assetRows.Add(asset.AssetUri, new CookingAssetViewModel(asset));
            }
            else
            {
                existing.Apply(asset);
            }
        }

        foreach (var removed in this.assetRows.Keys.Where(uri => !assets.ContainsKey(uri)).ToArray())
        {
            _ = this.assetRows.Remove(removed);
        }

        var listed = this.assetRows.Values.Where(row => this.ShowReused || !row.IsReused).OrderBy(static row => row.Asset.AssetUri).ToList();
        var wanted = listed.ToHashSet();
        for (var at = this.Assets.Count - 1; at >= 0; at--)
        {
            if (!wanted.Contains(this.Assets[at]))
            {
                this.Assets.RemoveAt(at);
            }
        }

        // Rows keep their order, so what remains is a prefix-ordered subsequence of the wanted rows.
        for (var index = 0; index < listed.Count; index++)
        {
            if (index >= this.Assets.Count || !ReferenceEquals(this.Assets[index], listed[index]))
            {
                this.Assets.Insert(index, listed[index]);
            }
        }
    }
}
