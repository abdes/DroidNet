// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Projects scoped cook history without moving selection for background updates.</summary>
public sealed partial class CookingPanelViewModel : ObservableObject, IDisposable
{
    private readonly ICookRunService runs;
    private readonly IContentPipelineService pipeline;
    private readonly IProjectContextService projects;
    private readonly IProjectAssetCatalog catalog;
    private readonly ICookingWorkspaceActions workspace;
    private readonly IMessenger messenger;
    private readonly HostingContext hosting;
    private readonly Dictionary<Guid, CookingRunViewModel> items = [];
    private bool reconciling;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="CookingPanelViewModel"/> class.</summary>
    /// <param name="runs">The coordinator's run history and controls.</param>
    /// <param name="pipeline">The shared cook entry points for every scope.</param>
    /// <param name="projects">The active project identity.</param>
    /// <param name="catalog">The published asset catalog.</param>
    /// <param name="workspace">Document/property recovery navigation.</param>
    /// <param name="messenger">The workspace's runtime publication channel.</param>
    /// <param name="hosting">The authoring dispatcher.</param>
    public CookingPanelViewModel(
        ICookRunService runs,
        IContentPipelineService pipeline,
        IProjectContextService projects,
        IProjectAssetCatalog catalog,
        ICookingWorkspaceActions workspace,
        IMessenger messenger,
        HostingContext hosting)
    {
        this.runs = runs;
        this.pipeline = pipeline;
        this.projects = projects;
        this.catalog = catalog;
        this.workspace = workspace;
        this.messenger = messenger;
        this.hosting = hosting;
        this.runs.RunChanged += this.OnRunChanged;
        foreach (var run in this.runs.Runs)
        {
            this.ApplyRun(new(run));
        }
    }

    /// <summary>Occurs only when explicit work requests panel activation.</summary>
    public event EventHandler? RevealRequested;

    /// <summary>Gets the active project's compact run list.</summary>
    public ObservableCollection<CookingRunViewModel> Runs { get; } = [];

    /// <summary>Gets visibility of the selected cook's details.</summary>
    public Microsoft.UI.Xaml.Visibility DetailsVisibility => this.SelectedRun is null ? Microsoft.UI.Xaml.Visibility.Collapsed : Microsoft.UI.Xaml.Visibility.Visible;

    /// <summary>Gets visibility of the empty-session message.</summary>
    public Microsoft.UI.Xaml.Visibility EmptyVisibility => this.SelectedRun is null ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;

    [ObservableProperty]
    public partial CookingRunViewModel? SelectedRun { get; set; }

    [ObservableProperty]
    public partial bool ShowAll { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ActionErrorVisibility))]
    public partial string ActionError { get; set; } = string.Empty;

    /// <summary>Gets visibility of an actionable recovery failure.</summary>
    public Microsoft.UI.Xaml.Visibility ActionErrorVisibility => string.IsNullOrEmpty(this.ActionError) ? Microsoft.UI.Xaml.Visibility.Collapsed : Microsoft.UI.Xaml.Visibility.Visible;

    /// <inheritdoc />
    public void Dispose()
    {
        this.disposed = true;
        this.runs.RunChanged -= this.OnRunChanged;
        GC.SuppressFinalize(this);
    }

    partial void OnShowAllChanged(bool value) => this.ReconcileRuns();

    partial void OnSelectedRunChanged(CookingRunViewModel? value)
    {
        this.ActionError = string.Empty;
        this.OnPropertyChanged(nameof(this.DetailsVisibility));
        this.OnPropertyChanged(nameof(this.EmptyVisibility));
        this.ReconcileRuns();
    }

    private void OnRunChanged(object? sender, CookRunChangedEventArgs args)
    {
        if (this.hosting.Dispatcher.HasThreadAccess)
        {
            this.ApplyRun(args);
        }
        else
        {
            _ = this.hosting.Dispatcher.TryEnqueue(() => this.ApplyRun(args));
        }
    }

    private void ApplyRun(CookRunChangedEventArgs args)
    {
        if (this.disposed || args.Run.ProjectId != this.projects.ActiveProject?.ProjectId)
        {
            return;
        }

        if (!this.items.TryGetValue(args.Run.OperationId, out var item))
        {
            this.items.Add(args.Run.OperationId, item = new(args.Run));
        }
        else
        {
            item.Apply(args.Run);
        }

        if (args.Reveal)
        {
            this.SelectedRun = item;
        }

        this.ReconcileRuns();
        if (args.Reveal)
        {
            this.RevealRequested?.Invoke(this, EventArgs.Empty);
        }
    }

    private void ReconcileRuns()
    {
        if (this.reconciling)
        {
            return;
        }

        this.reconciling = true;
        try
        {
            var desired = this.items.Values.Where(item => this.ShowAll || ReferenceEquals(item, this.SelectedRun)
                    || !item.Snapshot.Request.IsAutomatic || !item.Snapshot.IsCompleted
                    || item.Snapshot.State is CookRunState.Failed or CookRunState.SucceededWithWarnings)
                .OrderBy(static item => item.Snapshot.IsCompleted)
                .ThenByDescending(static item => item.Snapshot.StartedAt)
                .ToArray();
            foreach (var item in this.Runs.Where(item => !desired.Contains(item)).ToArray())
            {
                _ = this.Runs.Remove(item);
            }

            for (var index = 0; index < desired.Length; index++)
            {
                var existing = this.Runs.IndexOf(desired[index]);
                if (existing < 0)
                {
                    this.Runs.Insert(index, desired[index]);
                }
                else if (existing != index)
                {
                    this.Runs.Move(existing, index);
                }
            }

            this.SelectedRun ??= this.Runs.FirstOrDefault();
        }
        finally
        {
            this.reconciling = false;
        }
    }

    [RelayCommand]
    private Task RetryAsync()
        => this.SelectedRun is { RetryVisibility: Microsoft.UI.Xaml.Visibility.Visible } selected
            && selected.Snapshot.ProjectId == this.projects.ActiveProject?.ProjectId
            ? this.ExecuteCookAsync(selected.Snapshot.Request with { IsAutomatic = false }) : Task.CompletedTask;

    [RelayCommand]
    private async Task CancelAsync()
    {
        if (this.SelectedRun is { CanCancel: true } selected)
        {
            await this.runs.CancelAsync(selected.Snapshot.OperationId).ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private async Task SaveListedAndCookAsync()
    {
        if (this.SelectedRun is { Snapshot.State: CookRunState.NeedsSave } selected)
        {
            var displayed = selected.Snapshot;
            if (await this.workspace.SaveListedAsync(displayed).ConfigureAwait(true))
            {
                _ = this.runs.ResumeAfterSave(displayed.OperationId);
            }
            else
            {
                this.ActionError = "Some listed documents could not be saved. Resolve their save conflicts before cooking.";
            }
        }
    }

    [RelayCommand]
    private async Task OpenDocumentAsync(Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentState? document)
    {
        if (document is not null && !await this.workspace.OpenDocumentAsync(document.DocumentId).ConfigureAwait(true))
        {
            this.ActionError = "This document is no longer open. Cooking will recheck its saved source when resumed.";
        }
    }

    [RelayCommand]
    private async Task GoToPropertyAsync(CookingIssueViewModel? issue)
    {
        if (issue is not null && this.SelectedRun is { } selected
            && !await this.workspace.GoToPropertyAsync(selected.Snapshot, issue.Diagnostic).ConfigureAwait(true))
        {
            this.ActionError = "The affected property is no longer available.";
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI command boundary displays recovery failures while the coordinator retains the original cook outcome.")]
    private async Task ExecuteCookAsync(CookRunRequest request)
    {
        this.ActionError = string.Empty;
        try
        {
            var result = request.TargetKind switch
            {
                CookTargetKind.Project => await this.pipeline.CookProjectAsync(CancellationToken.None).ConfigureAwait(true),
                CookTargetKind.Folder => await this.pipeline.CookFolderAsync(request.ScopeUri!, CancellationToken.None).ConfigureAwait(true),
                _ => await this.pipeline.CookAssetAsync(request.ScopeUri!, CancellationToken.None).ConfigureAwait(true),
            };
            if (result.Validation?.Succeeded == true && result.Status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings)
            {
                await this.catalog.RefreshAsync(CancellationToken.None).ConfigureAwait(true);
                var roots = result.Validation.CookedRoot.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries);
                _ = this.messenger.Send(new ValidatedCookedOutputMessage(roots));
            }
        }
        catch (OperationCanceledException)
        {
            // The scoped run already records cancellation after owned work stops.
        }
        catch (Exception ex)
        {
            this.ActionError = ex.Message;
        }
    }
}
