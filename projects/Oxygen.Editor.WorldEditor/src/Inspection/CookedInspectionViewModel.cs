// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Inspection;

/// <summary>Owns a read-only report and drains superseded inspection work through the ordinary document lifecycle.</summary>
public sealed partial class CookedInspectionViewModel : ObservableObject, IDocumentCloseParticipant, IDisposable
{
    private readonly CookedInspectionDocumentMetadata metadata;
    private readonly IContentPipelineService pipeline;
    private readonly IContentBrowserAssetProvider assetProvider;
    private readonly IProjectContextService projects;
    private readonly Func<Uri, Task<bool>> showAsset;
    private Func<Task>? cancelInspection;
    private Task currentWork = Task.CompletedTask;
    private long requestVersion;
    private bool initialized;
    private bool closing;
    private bool closed;
    private InspectionAssetRow[] allAssets = [];
    private InspectionFileRow[] allFiles = [];

    /// <summary>Initializes a new instance of the <see cref="CookedInspectionViewModel"/> class.</summary>
    /// <param name="metadata">The captured request and document identity.</param>
    /// <param name="pipeline">The shared read-only pipeline boundary.</param>
    /// <param name="assetProvider">Current shared catalog facts for a specifically opened asset.</param>
    /// <param name="projects">The active project lifetime.</param>
    /// <param name="showAsset">Explicit navigation in the owning workspace.</param>
    public CookedInspectionViewModel(CookedInspectionDocumentMetadata metadata, IContentPipelineService pipeline, IContentBrowserAssetProvider assetProvider, IProjectContextService projects, Func<Uri, Task<bool>> showAsset)
    {
        this.metadata = metadata;
        this.pipeline = pipeline;
        this.assetProvider = assetProvider;
        this.projects = projects;
        this.showAsset = showAsset;
        metadata.RefreshRequested += this.OnRefreshRequested;
    }

    /// <summary>Gets the scope displayed in the report.</summary>
    public string Scope => this.metadata.ScopeUri is { } scope
        ? this.metadata.Project.Name + " · " + Oxygen.Managed.Assets.Catalog.AssetUriHelper.GetVirtualPath(scope) : this.metadata.Project.Name;

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
    public partial ContentBrowserAssetItem? ScopedAsset { get; private set; }

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
    public partial string StatusText { get; private set; } = "Inspection pending";

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
    [NotifyPropertyChangedFor(nameof(AssetsSelected))]
    [NotifyPropertyChangedFor(nameof(FilesSelected))]
    [NotifyPropertyChangedFor(nameof(IssuesSelected))]
    [NotifyPropertyChangedFor(nameof(EmptyFilesVisibility))]
    [NotifyPropertyChangedFor(nameof(EmptyIssuesVisibility))]
    public partial int Section { get; set; }

    /// <summary>Gets a value indicating whether Assets is selected.</summary>
    public bool AssetsSelected => this.Section == 0;

    /// <summary>Gets a value indicating whether Root files is selected.</summary>
    public bool FilesSelected => this.Section == 1;

    /// <summary>Gets a value indicating whether Issues is selected.</summary>
    public bool IssuesSelected => this.Section == 2;

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
    public string ScopedReferenceText => this.ScopedAsset?.IdentityUri.ToString() ?? string.Empty;

    /// <summary>Gets validation visibility when there is a published output to check.</summary>
    public Visibility ValidationVisibility => this.Report is { } report && !report.Roots.Any(static root => root.IsPresent) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Starts only the first inspection when the document view is first loaded.</summary>
    /// <returns>The first or currently active inspection.</returns>
    public Task InitializeAsync()
    {
        if (!this.initialized)
        {
            this.initialized = true;
            return this.RequestInspectionAsync(this.metadata.ValidateRequested);
        }

        return this.currentWork;
    }

    /// <inheritdoc />
    public Task PrepareForCloseAsync()
    {
        this.closing = true;
        _ = this.cancelInspection?.Invoke();
        return this.currentWork;
    }

    /// <inheritdoc />
    public Task<bool> SaveForCloseAsync() => Task.FromResult(true);

    /// <inheritdoc />
    public async Task CloseAsync(bool discard)
    {
        this.closed = true;
        this.metadata.RefreshRequested -= this.OnRefreshRequested;
        await this.PrepareForCloseAsync().ConfigureAwait(true);
    }

    /// <inheritdoc />
    public void ResumeEditing()
    {
        this.closing = false;
        this.RefreshCommand.NotifyCanExecuteChanged();
        this.ValidateCommand.NotifyCanExecuteChanged();
    }

    /// <inheritdoc />
    public void Dispose() => _ = this.CloseAsync(discard: false);

    partial void OnSearchTextChanged(string value) => this.Filter();

    private bool CanInspect() => !this.closed && !this.closing && !this.IsBusy;

    private bool CanValidate() => this.CanInspect() && this.ValidationVisibility == Visibility.Visible;

    [RelayCommand(CanExecute = nameof(CanInspect))]
    private Task RefreshAsync() => this.RequestInspectionAsync(this.metadata.ValidateRequested);

    [RelayCommand(CanExecute = nameof(CanValidate))]
    private Task ValidateAsync()
    {
        this.metadata.RequestRefresh(validate: true);
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
    private async Task ShowAssetAsync(Uri? uri)
    {
        try
        {
            if (uri is not null && !this.closed && !await this.showAsset(uri).ConfigureAwait(true))
            {
                this.Message = "This source is no longer available in the Content Browser.";
            }
        }
        catch (Exception exception)
        {
            this.Message = exception.Message;
        }
    }

    private void OnRefreshRequested(object? sender, EventArgs args) => _ = this.RequestInspectionAsync(this.metadata.ValidateRequested);

    private Task RequestInspectionAsync(bool validate)
    {
        if (this.closed || this.closing)
        {
            return this.currentWork;
        }

        _ = this.cancelInspection?.Invoke();
        this.initialized = true;
        var previous = this.currentWork;
        var version = ++this.requestVersion;
        this.IsBusy = true;
        this.Message = string.Empty;
        this.StatusText = validate ? "Validating output…" : "Inspecting output…";
        this.currentWork = this.RunAfterAsync(previous, version, validate);
        return this.currentWork;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The report document owns the read operation and presents failures without unobserved tasks.")]
    private async Task RunAfterAsync(Task previous, long version, bool validate)
    {
        try
        {
            var (report, asset) = await this.ReadReportAsync(previous, version, validate).ConfigureAwait(true);
            if (version == this.requestVersion && !this.closing && !this.closed)
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

    private async Task<(CookedOutputReport report, ContentBrowserAssetItem? asset)> ReadReportAsync(Task previous, long version, bool validate)
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
            this.VerifyProject();
            var asset = this.metadata.AssetUri is { } uri ? await this.ResolveInspectedAssetAsync(uri, cancellation.Token).ConfigureAwait(true) : null;
            var isUncopiedBuiltin = asset is { IsBuiltin: true, CookedUri: null, CookedCompanions.Count: 0 };
            var report = isUncopiedBuiltin
                ? new CookedOutputReport(this.metadata.Project.ProjectId, this.metadata.ScopeUri, DateTimeOffset.UtcNow, [])
                : await this.pipeline.InspectCookedOutputAsync(this.metadata.ScopeUri, cancellation.Token, validate, this.metadata.Project).ConfigureAwait(true);
            cancellation.Token.ThrowIfCancellationRequested();
            this.VerifyProject();
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

    private void VerifyProject()
    {
        if (!ReferenceEquals(this.projects.ActiveProject, this.metadata.Project))
        {
            throw new OperationCanceledException("The inspection's project was closed or replaced.");
        }
    }

    private async Task<ContentBrowserAssetItem?> ResolveInspectedAssetAsync(Uri uri, CancellationToken cancellationToken)
    {
        var asset = await this.assetProvider.ResolveAsync(uri, cancellationToken).ConfigureAwait(true);
        if (asset is not { IsBuiltin: true, BuiltinOriginUri: null })
        {
            return asset;
        }

        var items = await this.assetProvider.Items.FirstAsync().ToTask(cancellationToken).ConfigureAwait(true);
        return AssetIdentityGrouping.GroupBuiltins(items).FirstOrDefault(row => row.IdentityUri == asset.IdentityUri) ?? asset;
    }

    private void ApplyReport(CookedOutputReport report)
    {
        this.Report = report;
        this.allAssets = report.Roots.SelectMany(root => root.Inspection.Assets.Select(asset => new InspectionAssetRow(
            root, asset, root.Provenance.FirstOrDefault(origin => string.Equals(origin.CookedAssetUri.AbsolutePath, asset.VirtualPath, StringComparison.Ordinal))))).ToArray();
        this.allFiles = report.Roots.SelectMany(root => root.Inspection.Files.Select(file => new InspectionFileRow(root.Name, root.Inspection.CookedRoot, file))).ToArray();
        this.Issues = report.Roots.SelectMany(root => root.Inspection.Diagnostics.Concat(root.Validation?.Diagnostics ?? []).Select(issue => new InspectionIssueRow(root.Name, issue))).ToArray();
        this.StatusText = report.Roots.Any(static root => !root.Inspection.Succeeded || root.Validation?.Succeeded == false)
                || this.Issues.Any(static issue => issue.Diagnostic.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal) ? "Inspection has errors"
            : this.ScopedAsset is { IsBuiltin: true } && this.allAssets.Length == 0 ? this.ScopedAsset.PrimaryBadge
            : !report.Roots.Any(static root => root.IsPresent) ? "No cooked output"
            : report.Roots.Any(static root => root.Validation is not null) ? "Output validated" : "Output inspected";
        this.Summary = this.ScopedAsset is { IsBuiltin: true } && report.Roots.Count == 0
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
}
