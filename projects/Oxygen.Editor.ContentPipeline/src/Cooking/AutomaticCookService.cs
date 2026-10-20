// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Turns the asset catalog's changes into automatic cooks of the changed sources, through the shared cook writer and run history.</summary>
/// <remarks>
/// A change to a cookable source under an authoring mount opens a quiet window, which each further change restarts, up
/// to <see cref="QuietWindowLimit"/> after the first change. When it closes, a run waiting for one of the changed
/// sources to be saved resumes. Otherwise, while Cook on change is on, the service waits until the project writer is
/// idle, so no cook, import or relocation is half done, and cooks each changed source whose published output does not
/// match what is on disk; a cook that ran after the change, such as Play's, already covered it. A source has at most
/// one cook in flight, and a change while it runs cooks the source once more when it ends. A source that was moved or
/// deleted is a change too: if it has published output, its cook publishes the content again without it, and if it
/// was never cooked there is nothing to do. A cook stopped for a cook the user asked for goes through the same check
/// again, so it is cooked after that cook only if that cook did not cover it.
/// </remarks>
public sealed partial class AutomaticCookService : IAutomaticCookService, IDisposable
{
    /// <summary>How long the sources must stay unchanged before they are cooked.</summary>
    public static readonly TimeSpan QuietWindow = TimeSpan.FromMilliseconds(500);

    /// <summary>How long after its first change a window closes, even while changes keep coming.</summary>
    public static readonly TimeSpan QuietWindowLimit = TimeSpan.FromSeconds(5);

    private readonly IProjectContextService projects;
    private readonly IProjectManagerService projectManager;
    private readonly IContentPipelineService pipeline;
    private readonly IAssetCookStatusReader statuses;
    private readonly ICookRunService runs;
    private readonly TimeProvider time;
    private readonly ILogger<AutomaticCookService> logger;
    private readonly Lock sync = new();
    private readonly Dictionary<AssetPath, string> changed = [];
    private readonly Dictionary<AssetPath, SourceCook> cooks = [];
    private readonly ITimer window;
    private readonly IDisposable projectSubscription;
    private readonly IDisposable settingsSubscription;
    private readonly IDisposable catalogSubscription;
    private ProjectContext? project;
    private CancellationTokenSource projectLifetime = new();
    private long windowOpenedAt;
    private bool windowOpen;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AutomaticCookService"/> class.</summary>
    /// <param name="projects">The active project, its settings and their changes.</param>
    /// <param name="projectManager">Saves the project's settings.</param>
    /// <param name="catalog">The asset catalog, whose changes are the changed sources.</param>
    /// <param name="pipeline">The same saved-input pipeline used by explicit cooks.</param>
    /// <param name="statuses">Tells whether a source's published output matches what is on disk.</param>
    /// <param name="runs">The shared run history, holding the runs that wait for a source to be saved.</param>
    /// <param name="logger">Reports scheduling failures and sources left uncooked.</param>
    /// <param name="time">The clock of the quiet window; the system clock when omitted.</param>
    public AutomaticCookService(
        IProjectContextService projects,
        IProjectManagerService projectManager,
        IAssetCatalog catalog,
        IContentPipelineService pipeline,
        IAssetCookStatusReader statuses,
        ICookRunService runs,
        ILogger<AutomaticCookService> logger,
        TimeProvider? time = null)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        this.projects = projects;
        this.projectManager = projectManager;
        this.pipeline = pipeline;
        this.statuses = statuses;
        this.runs = runs;
        this.logger = logger;
        this.time = time ?? TimeProvider.System;
        this.window = this.time.CreateTimer(_ => _ = this.CloseWindowAsync(), state: null, Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        this.projectSubscription = projects.ProjectChanged.Subscribe(this.OnProjectChanged);
        this.settingsSubscription = projects.SettingsChanged.Subscribe(this.OnSettingsChanged);
        this.catalogSubscription = catalog.Changes.Subscribe(this.OnCatalogChange);
    }

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc />
    public bool CookOnChange => this.projects.Settings.CookOnChange;

    /// <inheritdoc />
    public async Task SetCookOnChangeAsync(bool enabled, CancellationToken cancellationToken = default)
    {
        var current = this.projects.ActiveProject ?? throw new InvalidOperationException("Cook on change is a setting of the active project.");
        var settings = this.projects.Settings with { CookOnChange = enabled };
        if (settings == this.projects.Settings)
        {
            return;
        }

        await this.projectManager.SaveProjectSettingsAsync(current.ProjectRoot.FullPath, settings, cancellationToken).ConfigureAwait(true);
        if (ReferenceEquals(current, this.projects.ActiveProject))
        {
            this.projects.UpdateSettings(settings);
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        lock (this.sync)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.changed.Clear();
            this.RetireAll();
            this.projectLifetime.Cancel();
        }

        this.catalogSubscription.Dispose();
        this.settingsSubscription.Dispose();
        this.projectSubscription.Dispose();
        this.window.Dispose();
        this.projectLifetime.Dispose();
    }

    // A source name the catalog reports for a cooked record resolves to the same source; only a change to the source
    // file itself is cooked.
    private static bool TryResolveSource(ProjectContext project, AssetPath uri, [NotNullWhen(true)] out string? source)
    {
        source = project.AuthoringMounts.Any(mount => !ProjectOutputFolders.Contains(mount) && string.Equals(mount.Name, uri.Mount, StringComparison.Ordinal))
            && project.Find(uri) is { IsMountRoot: false } location && ContentPipelineService.IsCookableDescriptorFile(project, location.File.FullPath)
                ? location.File.FullPath
                : null;
        return source is not null;
    }

    private static void Cancel(SourceCook cook) => cook.CancellationCompletion ??= cook.Cancellation.CancelAsync();

    private void OnProjectChanged(ProjectContext? value)
    {
        CancellationTokenSource ended;
        lock (this.sync)
        {
            if (this.disposed)
            {
                return;
            }

            ended = this.projectLifetime;
            this.projectLifetime = new();
            this.project = value;
            this.changed.Clear();
            this.windowOpen = false;
            _ = this.window.Change(Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
            this.RetireAll();
        }

        ended.Cancel();
        ended.Dispose();
        this.PropertyChanged?.Invoke(this, new(nameof(this.CookOnChange)));
    }

    private void OnSettingsChanged(ProjectSettings settings)
    {
        if (!settings.CookOnChange)
        {
            // A running cook finishes; it does not cook its source again for the changes it saw.
            lock (this.sync)
            {
                foreach (var cook in this.cooks.Values)
                {
                    cook.Again = false;
                }
            }
        }

        this.LogCookOnChangeSet(settings.CookOnChange);
        this.PropertyChanged?.Invoke(this, new(nameof(this.CookOnChange)));
    }

    private void OnCatalogChange(AssetChange change)
    {
        lock (this.sync)
        {
            // A listed asset became visible without changing: opening a project cooks nothing.
            if (this.disposed || this.project is not { } current || change.Kind is AssetChangeKind.Listed)
            {
                return;
            }

            // A cook in flight for a source that is gone stops; the source is checked again, to remove its output.
            if (change.Kind is AssetChangeKind.Removed)
            {
                this.Retire(change.Identity);
                this.Note(current, change.Identity);
                return;
            }

            if (change is { Kind: AssetChangeKind.Relocated, PreviousIdentity: { } previous })
            {
                this.Retire(previous);
                this.Note(current, previous);
            }

            this.Note(current, change.Identity);
        }
    }

    // Called under sync.
    private void Note(ProjectContext current, AssetPath uri)
    {
        if (!TryResolveSource(current, uri, out var source))
        {
            this.LogChangeIgnored(uri);
            return;
        }

        this.changed[uri] = source;
        var now = this.time.GetTimestamp();
        if (!this.windowOpen)
        {
            this.windowOpen = true;
            this.windowOpenedAt = now;
        }

        var left = QuietWindowLimit - this.time.GetElapsedTime(this.windowOpenedAt, now);
        _ = this.window.Change(left < QuietWindow ? (left > TimeSpan.Zero ? left : TimeSpan.Zero) : QuietWindow, Timeout.InfiniteTimeSpan);
    }

    private async Task CloseWindowAsync()
    {
        KeyValuePair<AssetPath, string>[] closed;
        ProjectContext current;
        CancellationToken lifetime;
        lock (this.sync)
        {
            if (this.disposed || this.project is null || !this.windowOpen)
            {
                return;
            }

            this.windowOpen = false;
            closed = [.. this.changed];
            this.changed.Clear();
            current = this.project;
            lifetime = this.projectLifetime.Token;
        }

        var resumed = closed.Where(item => this.ResumeWaitingRuns(current, item.Value)).Select(static item => item.Key).ToHashSet();
        if (resumed.Count != 0)
        {
            this.LogResumedWaitingRuns(resumed.Count);
        }

        var changedSources = closed.Select(static item => item.Key).Where(asset => !resumed.Contains(asset)).ToList();
        if (changedSources.Count != 0)
        {
            this.LogCheckingChangedSources(changedSources);
            await this.CookStaleAsync(current, changedSources, lifetime).ConfigureAwait(false);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed status read cooks the sources instead of leaving them stale.")]
    private async Task CookStaleAsync(ProjectContext current, List<AssetPath> changedSources, CancellationToken lifetime)
    {
        if (!this.CookOnChange)
        {
            this.LogNotCooked(changedSources.Count);
            return;
        }

        var stale = changedSources;
        try
        {
            var idle = this.runs.WhenWriterIdleAsync(lifetime);
            if (!idle.IsCompleted)
            {
                var waitStarted = this.time.GetTimestamp();
                this.LogWaitingForWriter(changedSources.Count);
                await idle.ConfigureAwait(false);
                this.LogWriterIdle(waitStarted);
            }

            var read = await this.statuses.ReadAsync(current, changedSources, lifetime).ConfigureAwait(false);
            var cooked = read.Where(static status => status.Freshness == AssetCookFreshness.Current).Select(static status => status.AssetUri).ToHashSet();

            // A source that is gone is cooked only to remove the output it published.
            var gone = read.Where(static status => status.Freshness == AssetCookFreshness.MissingSource && !status.HasPublishedOutput)
                .Select(static status => status.AssetUri).ToHashSet();
            stale = [.. changedSources.Where(asset => !cooked.Contains(asset) && !gone.Contains(asset))];
            if (cooked.Count > 0)
            {
                this.LogAlreadyCooked(cooked.Count);
            }

            if (gone.Count > 0)
            {
                this.LogSourcesGone(gone.Count);
            }
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested)
        {
            return;
        }
        catch (Exception exception)
        {
            this.LogStatusUnavailable(exception, changedSources.Count);
        }

        if (!this.CookOnChange)
        {
            this.LogNotCooked(stale.Count);
            return;
        }

        foreach (var asset in stale)
        {
            this.Queue(current, asset);
        }
    }

    private bool ResumeWaitingRuns(ProjectContext current, string source)
    {
        var resumed = false;
        foreach (var run in this.runs.Runs.Where(run => run.ProjectId == current.ProjectId && run.State == CookRunState.NeedsSave
            && run.UnsavedDocuments.Any(document => document.SourcePath == FileSystemPath.From(source))))
        {
            resumed |= this.runs.ResumeAfterSave(run.OperationId);
        }

        return resumed;
    }

    private void Queue(ProjectContext current, AssetPath asset)
    {
        SourceCook cook;
        lock (this.sync)
        {
            if (this.disposed || !ReferenceEquals(current, this.project))
            {
                return;
            }

            if (this.cooks.TryGetValue(asset, out var running))
            {
                running.Again = true;
                return;
            }

            cook = new(current, asset);
            this.cooks[asset] = cook;
        }

        // The cook runs off the catalog's notification; this task owns and observes its failures.
        _ = Task.Run(() => this.CookAsync(cook), CancellationToken.None);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Background cook failures are recorded in Cooking and logged; they must not escape onto the catalog.")]
    private async Task CookAsync(SourceCook cook)
    {
        CancellationToken? recheck = null;
        try
        {
            while (true)
            {
                lock (this.sync)
                {
                    if (!this.IsCurrent(cook))
                    {
                        return;
                    }

                    cook.Again = false;
                }

                var preempted = false;
                try
                {
                    _ = await this.pipeline.CookSavedAssetAsync(cook.AssetUri, cook.Project, cook.Cancellation.Token).ConfigureAwait(false);
                }
                catch (CookPreemptedException)
                {
                    preempted = true;
                }
                catch (OperationCanceledException)
                {
                    // Cancellation stays visible in the run history; the next change cooks the source again.
                }
                catch (Exception exception)
                {
                    this.LogSchedulingFailure(exception, cook.AssetUri);
                }

                lock (this.sync)
                {
                    var current = this.IsCurrent(cook);
                    if (!current || preempted || !cook.Again)
                    {
                        if (this.cooks.TryGetValue(cook.AssetUri, out var owned) && ReferenceEquals(owned, cook))
                        {
                            _ = this.cooks.Remove(cook.AssetUri);
                        }

                        recheck = current && preempted ? this.projectLifetime.Token : null;
                        break;
                    }
                }
            }
        }
        finally
        {
            await this.ReleaseAsync(cook).ConfigureAwait(false);
        }

        if (recheck is { } lifetime)
        {
            await this.CookStaleAsync(cook.Project, [cook.AssetUri], lifetime).ConfigureAwait(false);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Releasing a finished cook only logs a failed cancellation.")]
    private async Task ReleaseAsync(SourceCook cook)
    {
        Task? cancellation;
        lock (this.sync)
        {
            cancellation = cook.CancellationCompletion;
        }

        try
        {
            if (cancellation is not null)
            {
                await cancellation.ConfigureAwait(false);
            }
        }
        catch (Exception exception)
        {
            this.LogSchedulingFailure(exception, cook.AssetUri);
        }
        finally
        {
            cook.Cancellation.Dispose();
        }
    }

    // Called under sync.
    private void Retire(AssetPath asset)
    {
        if (this.cooks.Remove(asset, out var cook))
        {
            this.LogCookRetired(asset);
            Cancel(cook);
        }
    }

    // Called under sync.
    private void RetireAll()
    {
        foreach (var cook in this.cooks.Values)
        {
            Cancel(cook);
        }

        this.cooks.Clear();
    }

    // Called under sync.
    private bool IsCurrent(SourceCook cook)
        => !this.disposed && ReferenceEquals(cook.Project, this.project)
            && this.cooks.TryGetValue(cook.AssetUri, out var current) && ReferenceEquals(current, cook)
            && !cook.Cancellation.IsCancellationRequested;

    private sealed class SourceCook(ProjectContext project, AssetPath assetUri)
    {
        public ProjectContext Project { get; } = project;

        public AssetPath AssetUri { get; } = assetUri;

        public bool Again { get; set; }

        public CancellationTokenSource Cancellation { get; } = new();

        public Task? CancellationCompletion { get; set; }
    }
}
