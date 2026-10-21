// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.CookedInspector;

/// <summary>
/// The Cooked Inspector panel: a read-only report of the cooked output of one scope, which each inspection request
/// replaces. A request for the scope it already shows inspects it again.
/// </summary>
public sealed partial class CookedInspectionViewModel : ObservableObject, IDisposable
{
    private readonly IContentPipelineService pipeline;
    private readonly IAssetFeed assetProvider;
    private readonly IProjectContextService projects;
    private readonly Func<AssetPath, Task<bool>> showAsset;
    private Func<Task>? cancelInspection;
    private Task currentWork = Task.CompletedTask;
    private long requestVersion;
    private CookedInspectionScope? scope;
    private bool closed;
    private InspectionAssetRow[] allAssets = [];
    private InspectionFileRow[] allFiles = [];

    /// <summary>Initializes a new instance of the <see cref="CookedInspectionViewModel"/> class.</summary>
    /// <param name="pipeline">The shared read-only pipeline boundary.</param>
    /// <param name="assetProvider">Current shared catalog facts for a specifically opened asset.</param>
    /// <param name="projects">The active project lifetime.</param>
    /// <param name="showAsset">Explicit navigation in the owning workspace.</param>
    public CookedInspectionViewModel(IContentPipelineService pipeline, IAssetFeed assetProvider, IProjectContextService projects, Func<AssetPath, Task<bool>> showAsset)
    {
        this.pipeline = pipeline;
        this.assetProvider = assetProvider;
        this.projects = projects;
        this.showAsset = showAsset;
    }

    /// <summary>Gets the inspected scope's name, or what to do when nothing is inspected yet.</summary>
    public string Scope => this.scope is not { } current
        ? "Nothing inspected"
        : current.Scope is { } scope
            ? current.Project.Name + " · " + scope.VirtualPath
            : current.Project.Name;

    /// <summary>Gets the current read operation for lifecycle and workflow tests.</summary>
    public Task CurrentWork => this.currentWork;

    /// <summary>Gets the finite captured report.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ValidationVisibility))]
    [NotifyCanExecuteChangedFor(nameof(ValidateCommand))]
    public partial CookedOutputReport? Report { get; private set; }

    /// <summary>Gets the current asset facts, including engine-owned assets without a project copy.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AssetInformationVisibility))]
    [NotifyPropertyChangedFor(nameof(OutputAssetsVisibility))]
    [NotifyPropertyChangedFor(nameof(EmptyAssetsVisibility))]
    [NotifyPropertyChangedFor(nameof(ScopedReferenceText))]
    public partial AssetEntry? ScopedAsset { get; private set; }

    /// <summary>Gets or sets the report-local search text.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(EmptyAssetsText))]
    public partial string SearchText { get; set; } = string.Empty;

    /// <summary>Gets the visible asset entries.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(EmptyAssetsVisibility))]
    [NotifyPropertyChangedFor(nameof(AssetInformationVisibility))]
    [NotifyPropertyChangedFor(nameof(OutputAssetsVisibility))]
    public partial IReadOnlyList<InspectionAssetRow> Assets { get; private set; } = [];

    /// <summary>Gets the visible root-file entries.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(EmptyFilesVisibility))]
    public partial IReadOnlyList<InspectionFileRow> Files { get; private set; } = [];

    /// <summary>Gets inspection and validation issues with their root context.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(EmptyIssuesVisibility))]
    public partial IReadOnlyList<InspectionIssueRow> Issues { get; private set; } = [];

    /// <summary>Gets or sets selection within this report, independently of the browser.</summary>
    [ObservableProperty]
    public partial InspectionAssetRow? SelectedAsset { get; set; }

    /// <summary>Gets the current operation summary.</summary>
    [ObservableProperty]
    public partial string StatusText { get; private set; } = "Inspect a folder or an asset in the Content Browser, or a cook run in the Cooking panel, to see its cooked output here.";

    /// <summary>Gets the captured report time and counts.</summary>
    [ObservableProperty]
    public partial string Summary { get; private set; } = string.Empty;

    /// <summary>Gets an operation or navigation failure.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasMessage))]
    public partial string Message { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the report has an actionable message.</summary>
    public bool HasMessage => this.Message.Length > 0;

    /// <summary>Gets whether inspection is active or waiting for publication.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RefreshCommand))]
    [NotifyCanExecuteChangedFor(nameof(ValidateCommand))]
    [NotifyCanExecuteChangedFor(nameof(CancelCommand))]
    [NotifyPropertyChangedFor(nameof(BusyVisibility))]
    [NotifyPropertyChangedFor(nameof(EmptyAssetsVisibility))]
    [NotifyPropertyChangedFor(nameof(EmptyFilesVisibility))]
    [NotifyPropertyChangedFor(nameof(EmptyIssuesVisibility))]
    public partial bool IsBusy { get; private set; }

    /// <summary>Gets progress and cancellation visibility.</summary>
    public Visibility BusyVisibility => this.IsBusy ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets or sets the visible report section.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AssetsVisibility))]
    [NotifyPropertyChangedFor(nameof(FilesVisibility))]
    [NotifyPropertyChangedFor(nameof(IssuesVisibility))]
    [NotifyPropertyChangedFor(nameof(EmptyFilesVisibility))]
    [NotifyPropertyChangedFor(nameof(EmptyIssuesVisibility))]
    public partial int Section { get; set; }

    /// <summary>Gets the asset section visibility.</summary>
    public Visibility AssetsVisibility => this.Section == 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the file section visibility.</summary>
    public Visibility FilesVisibility => this.Section == 1 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the issue section visibility.</summary>
    public Visibility IssuesVisibility => this.Section == 2 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets a scoped empty-state explanation.</summary>
    public string EmptyAssetsText => this.SearchText.Length > 0 ? "No assets match this search." : "No cooked assets in this scope.";

    /// <summary>Gets the asset empty-state visibility.</summary>
    public Visibility EmptyAssetsVisibility => !this.IsBusy && this.Assets.Count == 0 && this.AssetInformationVisibility != Visibility.Visible ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the root-file empty-state visibility.</summary>
    public Visibility EmptyFilesVisibility => this.Section == 1 && !this.IsBusy && this.Files.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the issue empty-state visibility.</summary>
    public Visibility EmptyIssuesVisibility => this.Section == 2 && !this.IsBusy && this.Issues.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets fallback asset-information visibility when no project output represents the selected asset.</summary>
    public Visibility AssetInformationVisibility => this.ScopedAsset is not null && this.allAssets.Length == 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the ordinary cooked asset list/details visibility.</summary>
    public Visibility OutputAssetsVisibility => this.AssetInformationVisibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets the copyable identity of an asset opened without project output.</summary>
    public string ScopedReferenceText => this.ScopedAsset?.Identity.ToString() ?? string.Empty;

    /// <summary>Gets validation visibility when there is a published output to check.</summary>
    public Visibility ValidationVisibility => this.Report is { } report && !report.Roots.Any(static root => root.IsPresent) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Inspects a scope: replaces the shown scope with it, or inspects the shown scope again.</summary>
    /// <param name="request">The scope to inspect.</param>
    /// <returns>The inspection.</returns>
    public Task InspectAsync(CookedInspectionScope request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (!request.IsSameOutput(this.scope))
        {
            _ = this.cancelInspection?.Invoke();
            this.Report = null;
            this.ScopedAsset = null;
            this.allAssets = [];
            this.allFiles = [];
            this.Assets = [];
            this.Files = [];
            this.Issues = [];
            this.Summary = string.Empty;
        }

        this.scope = request;
        this.OnPropertyChanged(nameof(this.Scope));
        this.RefreshCommand.NotifyCanExecuteChanged();
        this.ValidateCommand.NotifyCanExecuteChanged();
        return this.RequestInspectionAsync(request.Validate);
    }

    /// <summary>Cancels the running inspection; the panel inspects nothing more.</summary>
    public void Dispose()
    {
        this.closed = true;
        _ = this.cancelInspection?.Invoke();
    }

    partial void OnSearchTextChanged(string value) => this.Filter();

    private bool CanInspect() => this.scope is not null && !this.closed && !this.IsBusy;

    private bool CanValidate() => this.CanInspect() && this.ValidationVisibility == Visibility.Visible;

    [RelayCommand(CanExecute = nameof(CanInspect))]
    private Task RefreshAsync() => this.scope is { } current ? this.RequestInspectionAsync(current.Validate) : Task.CompletedTask;

    [RelayCommand(CanExecute = nameof(CanValidate))]
    private Task ValidateAsync()
    {
        if (this.scope is { } current)
        {
            this.scope = current with { Validate = true };
            _ = this.RequestInspectionAsync(validate: true);
        }

        return this.currentWork;
    }

    [RelayCommand(CanExecute = nameof(IsBusy))]
    private Task CancelAsync()
    {
        _ = this.cancelInspection?.Invoke();
        return this.currentWork;
    }

    [RelayCommand]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Navigation failures remain visible in the report instead of escaping a UI command.")]
    private async Task ShowAssetAsync(AssetPath? uri)
    {
        try
        {
            if (uri is not null && !this.closed)
            {
                this.Message = await this.showAsset(uri).ConfigureAwait(true)
                    ? string.Empty : "This source is no longer available in the Content Browser.";
            }
        }
        catch (Exception exception)
        {
            this.Message = exception.Message;
        }
    }

    private Task RequestInspectionAsync(bool validate)
    {
        if (this.closed || this.scope is not { } request)
        {
            return this.currentWork;
        }

        _ = this.cancelInspection?.Invoke();
        var previous = this.currentWork;
        var version = ++this.requestVersion;
        this.IsBusy = true;
        this.Message = string.Empty;
        this.StatusText = validate ? "Validating output…" : "Inspecting output…";
        this.currentWork = this.RunAfterAsync(previous, version, request, validate);
        return this.currentWork;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The report document owns the read operation and presents failures without unobserved tasks.")]
    private async Task RunAfterAsync(Task previous, long version, CookedInspectionScope request, bool validate)
    {
        try
        {
            var (report, asset) = await this.ReadReportAsync(previous, version, request, validate).ConfigureAwait(true);
            if (version == this.requestVersion && !this.closed)
            {
                this.ScopedAsset = asset;
                this.ApplyReport(report);
            }
        }
        catch (OperationCanceledException)
        {
            if (version == this.requestVersion && !this.closed)
            {
                this.StatusText = "Inspection cancelled";
            }
        }
        catch (Exception exception)
        {
            if (version == this.requestVersion && !this.closed)
            {
                this.StatusText = "Inspection failed";
                this.Message = exception.Message;
            }
        }
        finally
        {
            if (version == this.requestVersion)
            {
                this.IsBusy = false;
            }
        }
    }

    private async Task<(CookedOutputReport report, AssetEntry? asset)> ReadReportAsync(Task previous, long version, CookedInspectionScope request, bool validate)
    {
        using var cancellation = new CancellationTokenSource();
        var cancellationWork = Task.CompletedTask;
        this.cancelInspection = () =>
        {
            if (!cancellation.IsCancellationRequested)
            {
                cancellationWork = cancellation.CancelAsync();
            }

            return cancellationWork;
        };
        try
        {
            await previous.ConfigureAwait(true);
            cancellation.Token.ThrowIfCancellationRequested();
            var project = this.GetCurrentProject(request);
            var asset = request.Asset is { } identity ? await this.ResolveInspectedAssetAsync(identity, project, request, cancellation.Token).ConfigureAwait(true) : null;
            // A packaged asset is read from its pak: the project has no cooked output of it to inspect.
            var report = asset is { IsPackaged: true }
                ? new CookedOutputReport(request.Project.ProjectId, request.Scope, DateTimeOffset.UtcNow, [])
                : await this.pipeline.InspectCookedOutputAsync(request.Scope, cancellation.Token, validate, project).ConfigureAwait(true);
            cancellation.Token.ThrowIfCancellationRequested();
            if (!ReferenceEquals(project, this.projects.ActiveProject))
            {
                throw new OperationCanceledException("The project configuration changed during inspection.");
            }

            if (request.CookedSource is not null && !report.Roots.Any(static root => root.Inspection.Assets.Count > 0))
            {
                asset = null;
            }

            return (report, asset);
        }
        finally
        {
            if (version == this.requestVersion)
            {
                this.cancelInspection = null;
            }

            await cancellationWork.ConfigureAwait(true);
        }
    }

    private ProjectContext GetCurrentProject(CookedInspectionScope request)
        => this.projects.ActiveProject is { } project && project.ProjectId == request.Project.ProjectId
            && project.ProjectRoot == request.Project.ProjectRoot
            ? project : throw new OperationCanceledException("The inspection's project was closed or replaced.");

    private async Task<AssetEntry?> ResolveInspectedAssetAsync(AssetPath identity, ProjectContext project, CookedInspectionScope request, CancellationToken cancellationToken)
    {
        var asset = await this.assetProvider.ResolveAsync(identity, cancellationToken).ConfigureAwait(true);
        if (asset is not null && request.CookedSource is { } requested)
        {
            var source = asset.OverriddenCookedSources.Prepend(asset.CookedMetadata).OfType<Oxygen.Managed.Assets.Catalog.CookedAssetMetadata>()
                .FirstOrDefault(candidate => candidate.ContainerPath == requested.ContainerPath && candidate.VirtualPath == requested.VirtualPath);
            return source is null ? null : AssetIdentity.FromCookedSource(asset, source, project);
        }

        return asset;
    }

    private void ApplyReport(CookedOutputReport report)
    {
        this.Report = report;
        this.allAssets = report.Roots.SelectMany(root => root.Inspection.Assets.Select(asset => this.CreateAssetRow(root, asset))).ToArray();
        this.allFiles = report.Roots.SelectMany(root => root.Inspection.Files.Select(file => new InspectionFileRow(root.Name, root.Inspection.CookedRoot, file))).ToArray();
        this.Issues = report.Roots.SelectMany(root => root.Inspection.Diagnostics.Concat(root.Validation?.Diagnostics ?? []).Select(issue => new InspectionIssueRow(root.Name, issue))).ToArray();
        this.StatusText = report.Roots.Any(static root => !root.Inspection.Succeeded || root.Validation?.Succeeded == false)
                || this.Issues.Any(static issue => issue.Diagnostic.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal) ? "Inspection has errors"
            : this.ScopedAsset is { IsPackaged: true } && this.allAssets.Length == 0 ? this.ScopedAsset.PrimaryBadge
            : !report.Roots.Any(static root => root.IsPresent) ? "No cooked output"
            : report.Roots.Any(static root => root.Validation is not null) ? "Output validated" : "Output inspected";
        this.Summary = this.ScopedAsset is { IsPackaged: true } && report.Roots.Count == 0
            ? report.CapturedAt.ToLocalTime().ToString("g", CultureInfo.CurrentCulture)
            : string.Create(CultureInfo.CurrentCulture, $"{this.allAssets.Length:N0} assets · {this.allFiles.Length:N0} root files · {report.CapturedAt.ToLocalTime():g}");
        this.Filter();
        if (this.Issues.Count > 0)
        {
            this.Section = 2;
        }
    }

    private void Filter()
    {
        var selected = this.SelectedAsset?.Key;
        var search = this.SearchText.Trim();
        this.Assets = this.allAssets.Where(asset => asset.Name.Contains(search, StringComparison.OrdinalIgnoreCase) || asset.Asset.VirtualPath.Contains(search, StringComparison.OrdinalIgnoreCase)).ToArray();
        this.Files = this.allFiles.Where(file => file.Name.Contains(search, StringComparison.OrdinalIgnoreCase)).ToArray();
        this.SelectedAsset = this.Assets.FirstOrDefault(asset => string.Equals(asset.Key, selected, StringComparison.Ordinal)) ?? (this.Assets.Count > 0 ? this.Assets[0] : null);
    }

    private InspectionAssetRow CreateAssetRow(CookedRootReport root, CookedAssetEntry asset)
    {
        var scoped = this.ScopedAsset is { CookedMetadata: { } metadata } candidate
            && metadata.ContainerPath == FileSystemPath.From(root.Inspection.CookedRoot)
            && string.Equals((metadata.VirtualPath ?? candidate.Identity).VirtualPath, asset.VirtualPath, StringComparison.Ordinal) ? candidate : null;
        return new(root, asset, root.Provenance.FirstOrDefault(origin => string.Equals(origin.CookedAssetUri.VirtualPath, asset.VirtualPath, StringComparison.Ordinal)))
        {
            ResolutionText = scoped?.IsCookedSourceOverridden == true ? scoped.PrimaryBadgeTooltip : string.Empty,
            OtherSources = scoped?.OverriddenCookedSources.Select(static source => source.ContainerPath).Distinct().Select(static root => root.FullPath).ToArray() ?? [],
        };
    }
}
