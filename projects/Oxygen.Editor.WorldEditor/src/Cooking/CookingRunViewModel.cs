// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core.Diagnostics;
using Serilog.Events;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Maintains stable presentation collections for one session cook.</summary>
public sealed partial class CookingRunViewModel : ObservableObject
{
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
    public string Kind => this.snapshot.Request.Import is not null || this.snapshot.Request.IsReimport || (Path.GetExtension(this.snapshot.Request.ScopeUri?.AbsolutePath) ?? string.Empty).ToUpperInvariant() is ".GLTF" or ".GLB" or ".FBX" ? "Source" : this.snapshot.Request.TargetKind switch
    {
        CookTargetKind.Project => "Project",
        CookTargetKind.Folder => "Folder",
        CookTargetKind.CurrentScene => "Scene",
        _ => this.snapshot.Request.ScopeUri?.AbsolutePath.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase) == true ? "Scene"
            : this.snapshot.Request.ScopeUri?.AbsolutePath.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase) == true ? "Geometry" : "Material",
    };

    /// <summary>Gets the secondary list context.</summary>
    public string Context => this.Kind + (this.snapshot.Request.IsAutomatic ? " · Automatic" : string.Empty);

    /// <summary>Gets the compact selected-run title and type.</summary>
    public string Title => $"{this.Name} ({this.Kind})";

    /// <summary>Gets one readable status for the selected run.</summary>
    public string Status => GetStatus(this.snapshot.State);

    /// <summary>Gets the status glyph used consistently in both lists.</summary>
    public string StatusGlyph => GetGlyph(this.snapshot.State);

    /// <summary>Gets a value indicating whether indeterminate progress is appropriate.</summary>
    public bool IsBusy => !this.snapshot.IsCompleted && this.snapshot.State is not CookRunState.NeedsSave and not CookRunState.Queued;

    /// <summary>Gets visibility of live progress, hiding the idle track after completion.</summary>
    public Visibility ProgressVisibility => this.IsBusy ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility for applicable retry actions.</summary>
    public Visibility RetryVisibility => this.snapshot.IsCompleted && this.snapshot.State is CookRunState.Failed or CookRunState.Cancelled ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility for cancellation while work remains owned.</summary>
    public Visibility CancelVisibility => this.snapshot.IsCompleted ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets visibility of published-output inspection after the selected operation has finished.</summary>
    public Visibility InspectVisibility => this.snapshot.IsCompleted && (this.snapshot.Request.TargetKind == CookTargetKind.Project
        || (this.snapshot.Request.ScopeUri is { } uri && string.Equals(uri.Scheme, "asset", StringComparison.OrdinalIgnoreCase))) ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of explicit navigation to the successful run's imported assets.</summary>
    public Visibility ShowImportedAssetsVisibility => this.snapshot.ImportedOutputs.IsEmpty ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets a value indicating whether cancellation can still be requested.</summary>
    public bool CanCancel => !this.snapshot.IsCompleted && this.snapshot.State != CookRunState.Cancelling;

    /// <summary>Gets visibility of the explicit save recovery controls.</summary>
    public Visibility SaveVisibility => this.snapshot.State == CookRunState.NeedsSave ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets a value indicating whether the cook is waiting for explicit saves.</summary>
    public bool NeedsSave => this.snapshot.State == CookRunState.NeedsSave;

    /// <summary>Gets the exact documents requiring explicit save consent.</summary>
    public IReadOnlyList<Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentState> UnsavedDocuments => this.snapshot.UnsavedDocuments;

    /// <summary>Gets this run's transcript, independent of the global console.</summary>
    public ObservableCollection<OutputLogEntry> Output { get; } = [];

    /// <summary>Gets the actual participating assets.</summary>
    public ObservableCollection<CookingAssetViewModel> Assets { get; } = [];

    /// <summary>Gets actionable issues grouped by source asset.</summary>
    public ObservableCollection<CookingIssueGroup> IssueGroups { get; } = [];

    [ObservableProperty]
    public partial bool FollowTail { get; set; } = true;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(OutputVisibility))]
    [NotifyPropertyChangedFor(nameof(OutputChevron))]
    public partial bool IsOutputExpanded { get; set; } = true;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AssetsVisibility))]
    [NotifyPropertyChangedFor(nameof(AssetsChevron))]
    public partial bool IsAssetsExpanded { get; set; }

    /// <summary>Gets visibility of the selected run's output area.</summary>
    public Visibility OutputVisibility => this.IsOutputExpanded ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of the asset list.</summary>
    public Visibility AssetsVisibility => this.IsAssetsExpanded ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the output disclosure icon.</summary>
    public string OutputChevron => this.IsOutputExpanded ? "\uE70D" : "\uE76C";

    /// <summary>Gets the asset disclosure icon.</summary>
    public string AssetsChevron => this.IsAssetsExpanded ? "\uE70D" : "\uE76C";

    /// <summary>Applies ordered state without replacing existing transcript items.</summary>
    /// <param name="next">A snapshot from the owning coordinator.</param>
    public void Apply(CookRunSnapshot next)
    {
        if (next.Revision < this.snapshot.Revision)
        {
            return;
        }

        this.snapshot = next;
        for (var index = this.Output.Count; index < next.Messages.Count; index++)
        {
            var message = next.Messages[index];
            this.Output.Add(new OutputLogEntry
            {
                Timestamp = message.Timestamp,
                Message = message.Text,
                Level = message.Severity switch
                {
                    DiagnosticSeverity.Warning => LogEventLevel.Warning,
                    DiagnosticSeverity.Error => LogEventLevel.Error,
                    DiagnosticSeverity.Fatal => LogEventLevel.Fatal,
                    _ => LogEventLevel.Information,
                },
            });
        }

        foreach (var asset in next.Assets.Values.OrderBy(static asset => asset.AssetUri.AbsoluteUri, StringComparer.Ordinal))
        {
            var existing = this.Assets.FirstOrDefault(item => item.Asset.AssetUri == asset.AssetUri);
            if (existing is null)
            {
                this.Assets.Add(new(asset));
            }
            else
            {
                existing.Apply(asset);
            }
        }

        foreach (var diagnostic in next.Diagnostics)
        {
            var key = diagnostic.AffectedVirtualPath ?? diagnostic.AffectedEntity?.AssetVirtualPath ?? string.Empty;
            var group = this.IssueGroups.FirstOrDefault(item => string.Equals(item.Key, key, StringComparison.Ordinal));
            if (group is null)
            {
                group = new(key, diagnostic.AffectedEntity?.SceneName ?? GetAssetName(key, this.Name));
                this.IssueGroups.Add(group);
            }

            if (!group.Issues.Any(issue => issue.Diagnostic.DiagnosticId == diagnostic.DiagnosticId))
            {
                group.Issues.Add(new(diagnostic));
            }
        }

        this.OnPropertyChanged(string.Empty);
    }

    /// <summary>Extracts a readable descriptor name from its stable source path.</summary>
    /// <param name="path">The asset source path.</param>
    /// <param name="fallback">The display name for a project-wide issue.</param>
    /// <returns>The logical name.</returns>
    internal static string GetAssetName(string path, string fallback)
        => string.IsNullOrWhiteSpace(path) ? fallback : Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(Uri.UnescapeDataString(path)));

    /// <summary>Maps execution state to the shared status icon vocabulary.</summary>
    /// <param name="state">The observed state.</param>
    /// <returns>The Fluent status glyph.</returns>
    internal static string GetGlyph(CookRunState state) => state switch
    {
        CookRunState.Succeeded or CookRunState.UpToDate => "\uE73E",
        CookRunState.SucceededWithWarnings => "\uE7BA",
        CookRunState.Failed => "\uEA39",
        CookRunState.Cancelled => "\uE711",
        CookRunState.NeedsSave => "\uE74E",
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
        CookRunState.NeedsSave => "Needs save",
        CookRunState.Publishing => "Updating preview",
        _ => state.ToString(),
    };
}
