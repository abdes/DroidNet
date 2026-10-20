// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Input;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Turns the asset catalog's changes into automatic cooks of the changed sources, through the shared cook writer and run history.</summary>
/// <remarks>
/// A change to a cookable source under an authoring mount opens a quiet window, which each further change restarts, up
/// to <see cref="QuietWindowLimit"/> after the first change. When it closes, a run waiting for one of the changed
/// sources to be saved resumes. The other sources wait for the next automatic run, which starts once the project
/// writer is idle, so no cook, import or relocation is half done. Sources from every window that closes while it
/// waits join it, and so do those that change while an automatic run cooks: there is one automatic run at a time, and
/// a running one is never interrupted by changes. When it starts, while Cook on change is on, it covers each waiting
/// source whose published output does not match what is on disk; a cook that ran after the change, such as Play's,
/// already covered it. A source that was moved or deleted is a change too: if it has published output, the run
/// publishes the content again without it, and if it was never cooked there is nothing to do.
/// <para>
/// A run fails as a whole when one of its inputs fails, so a source whose family facts (scripts, input sources) report
/// errors is left out and named in the run; its problems are in Problems. It cooks with the change that fixes them,
/// or, when that change came before its facts caught up, once its facts report no errors. A run stopped for a cook the
/// user asked for returns its sources to the next run, which cooks those that cook did not cover.
/// </para>
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
    private readonly IScripts scripts;
    private readonly IInputSources inputs;
    private readonly TimeProvider time;
    private readonly ILogger<AutomaticCookService> logger;
    private readonly Lock sync = new();
    private readonly Dictionary<AssetPath, string> changed = [];
    private readonly HashSet<AssetPath> waiting = [];
    private readonly HashSet<AssetPath> skipped = [];
    private readonly ITimer window;
    private readonly IDisposable projectSubscription;
    private readonly IDisposable settingsSubscription;
    private readonly IDisposable catalogSubscription;
    private ProjectContext? project;
    private CancellationTokenSource projectLifetime = new();
    private long windowOpenedAt;
    private bool windowOpen;
    private bool draining;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AutomaticCookService"/> class.</summary>
    /// <param name="projects">The active project, its settings and their changes.</param>
    /// <param name="projectManager">Saves the project's settings.</param>
    /// <param name="catalog">The asset catalog, whose changes are the changed sources.</param>
    /// <param name="pipeline">The same saved-input pipeline used by explicit cooks.</param>
    /// <param name="statuses">Tells whether a source's published output matches what is on disk.</param>
    /// <param name="runs">The shared run history, holding the runs that wait for a source to be saved.</param>
    /// <param name="scripts">The script facts, whose errors leave a script uncooked.</param>
    /// <param name="inputs">The input source facts, whose errors leave an input source uncooked.</param>
    /// <param name="logger">Reports scheduling failures and sources left uncooked.</param>
    /// <param name="time">The clock of the quiet window; the system clock when omitted.</param>
    public AutomaticCookService(
        IProjectContextService projects,
        IProjectManagerService projectManager,
        IAssetCatalog catalog,
        IContentPipelineService pipeline,
        IAssetCookStatusReader statuses,
        ICookRunService runs,
        IScripts scripts,
        IInputSources inputs,
        ILogger<AutomaticCookService> logger,
        TimeProvider? time = null)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        this.projects = projects;
        this.projectManager = projectManager;
        this.pipeline = pipeline;
        this.statuses = statuses;
        this.runs = runs;
        this.scripts = scripts;
        this.inputs = inputs;
        this.logger = logger;
        this.time = time ?? TimeProvider.System;
        this.window = this.time.CreateTimer(_ => this.CloseWindow(), state: null, Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        this.projectSubscription = projects.ProjectChanged.Subscribe(this.OnProjectChanged);
        this.settingsSubscription = projects.SettingsChanged.Subscribe(this.OnSettingsChanged);
        this.catalogSubscription = catalog.Changes.Subscribe(this.OnCatalogChange);
        scripts.ProblemsChanged += this.OnProblemsChanged;
        inputs.ProblemsChanged += this.OnProblemsChanged;
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
            this.waiting.Clear();
            this.skipped.Clear();
            this.projectLifetime.Cancel();
        }

        this.inputs.ProblemsChanged -= this.OnProblemsChanged;
        this.scripts.ProblemsChanged -= this.OnProblemsChanged;
        this.catalogSubscription.Dispose();
        this.settingsSubscription.Dispose();
        this.projectSubscription.Dispose();
        this.window.Dispose();
        this.projectLifetime.Dispose();
    }

    // A source name the catalog reports for a cooked record resolves to the same source; only a change to the source
    // file itself, an authored descriptor or a script, is cooked.
    private static bool TryResolveSource(ProjectContext project, AssetPath uri, [NotNullWhen(true)] out string? source)
    {
        source = project.AuthoringMounts.Any(mount => !ProjectOutputFolders.Contains(mount) && string.Equals(mount.Name, uri.Mount, StringComparison.Ordinal))
            && project.Find(uri) is { IsMountRoot: false } location
            && (ContentPipelineService.IsCookableDescriptorFile(project, location.File.FullPath) || AssetFileTypes.Is(uri.Name, AssetFileCategory.Script))
                ? location.File.FullPath
                : null;
        return source is not null;
    }

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
            this.waiting.Clear();
            this.skipped.Clear();
            this.windowOpen = false;
            _ = this.window.Change(Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        }

        // The run of the project that closed stops; its writer scope publishes nothing partial.
        ended.Cancel();
        ended.Dispose();
        this.PropertyChanged?.Invoke(this, new(nameof(this.CookOnChange)));
    }

    private void OnSettingsChanged(ProjectSettings settings)
    {
        // A running cook finishes; the sources waiting for the next run are not cooked while the setting is off.
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

            // The source a file left is checked again, to remove its output.
            if (change is { Kind: AssetChangeKind.Relocated, PreviousIdentity: { } previous })
            {
                this.Note(current, previous);
            }

            this.Note(current, change.Identity);
        }
    }

    // A skipped source whose facts no longer report errors is a change: its fixing change may have come before the
    // facts caught up with it.
    private void OnProblemsChanged(object? sender, EventArgs args)
    {
        lock (this.sync)
        {
            if (this.disposed || this.project is not { } current || this.skipped.Count == 0)
            {
                return;
            }

            foreach (var source in this.skipped.Where(source => this.ErrorCount(source) == 0).ToList())
            {
                this.Note(current, source);
            }
        }
    }

    // Called under sync.
    private void Note(ProjectContext current, AssetPath uri)
    {
        _ = this.skipped.Remove(uri);
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

    private void CloseWindow()
    {
        KeyValuePair<AssetPath, string>[] closed;
        ProjectContext current;
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
        }

        var resumed = closed.Where(item => this.ResumeWaitingRuns(current, item.Value)).Select(static item => item.Key).ToHashSet();
        if (resumed.Count != 0)
        {
            this.LogResumedWaitingRuns(resumed.Count);
        }

        lock (this.sync)
        {
            if (this.disposed || !ReferenceEquals(current, this.project))
            {
                return;
            }

            this.waiting.UnionWith(closed.Select(static item => item.Key).Where(asset => !resumed.Contains(asset)));
            if (this.waiting.Count == 0 || this.draining)
            {
                return;
            }

            this.draining = true;
        }

        // The runs go on off the window's timer; the drain owns and observes their failures.
        _ = Task.Run(() => this.DrainAsync(), CancellationToken.None);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A scheduling failure is logged; the next closed window drains the sources still waiting.")]
    private async Task DrainAsync()
    {
        try
        {
            while (await this.NextRunAsync().ConfigureAwait(false) is { } next)
            {
                await this.CookAsync(next.Project, next.Changes, next.Lifetime).ConfigureAwait(false);
            }
        }
        catch (Exception exception)
        {
            this.LogDrainFailure(exception);
            lock (this.sync)
            {
                this.draining = false;
            }
        }
    }

    // The next run: the sources waiting when the writer is idle, less those already cooked or skipped for their errors.
    // Null, and the drain ends, when no source is waiting.
    private async Task<(ProjectContext Project, CookChanges Changes, CancellationToken Lifetime)?> NextRunAsync()
    {
        while (true)
        {
            ProjectContext current;
            CancellationToken lifetime;
            int count;
            lock (this.sync)
            {
                if (this.disposed || this.project is null || this.waiting.Count == 0)
                {
                    this.draining = false;
                    return null;
                }

                current = this.project;
                lifetime = this.projectLifetime.Token;
                count = this.waiting.Count;
            }

            try
            {
                await this.WaitForWriterAsync(count, lifetime).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (lifetime.IsCancellationRequested)
            {
                continue;
            }

            List<AssetPath> sources;
            lock (this.sync)
            {
                if (this.disposed || !ReferenceEquals(current, this.project))
                {
                    continue;
                }

                sources = [.. this.waiting];
                this.waiting.Clear();
            }

            if (await this.SelectAsync(current, sources, lifetime).ConfigureAwait(false) is { } changes)
            {
                return (current, changes, lifetime);
            }
        }
    }

    private async Task WaitForWriterAsync(int count, CancellationToken lifetime)
    {
        var idle = this.runs.WhenWriterIdleAsync(lifetime);
        if (idle.IsCompleted)
        {
            await idle.ConfigureAwait(false);
            return;
        }

        var waitStarted = this.time.GetTimestamp();
        this.LogWaitingForWriter(count);
        await idle.ConfigureAwait(false);
        this.LogWriterIdle(waitStarted);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed status read cooks the sources instead of leaving them stale.")]
    private async Task<CookChanges?> SelectAsync(ProjectContext current, List<AssetPath> sources, CancellationToken lifetime)
    {
        this.LogCheckingChangedSources(sources);
        if (!this.CookOnChange)
        {
            this.LogNotCooked(sources.Count);
            return null;
        }

        var stale = sources;
        try
        {
            var read = await this.statuses.ReadAsync(current, sources, lifetime).ConfigureAwait(false);
            var cooked = read.Where(static status => status.Freshness == AssetCookFreshness.Current).Select(static status => status.AssetUri).ToHashSet();

            // A source that is gone is cooked only to remove the output it published. One that cannot be a cook input,
            // such as a deleted script, would only fail the run; the next cook removes what it published.
            var gone = read.Where(static status => (status.Freshness == AssetCookFreshness.MissingSource && !status.HasPublishedOutput)
                    || status.Diagnostics.Any(static issue => string.Equals(issue.Code, AssetCookStatusReader.InputUnresolvedCode, StringComparison.Ordinal)))
                .Select(static status => status.AssetUri).ToHashSet();
            stale = [.. sources.Where(asset => !cooked.Contains(asset) && !gone.Contains(asset))];
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
            return null;
        }
        catch (Exception exception)
        {
            this.LogStatusUnavailable(exception, sources.Count);
        }

        if (!this.CookOnChange)
        {
            this.LogNotCooked(stale.Count);
            return null;
        }

        return this.LeaveOutSourcesWithErrors(current, stale);
    }

    private CookChanges? LeaveOutSourcesWithErrors(ProjectContext current, List<AssetPath> stale)
    {
        List<AssetPath> cook = [];
        List<SkippedCookSource> skip = [];
        lock (this.sync)
        {
            if (this.disposed || !ReferenceEquals(current, this.project))
            {
                return null;
            }

            foreach (var source in stale)
            {
                var errors = this.ErrorCount(source);
                if (errors > 0)
                {
                    skip.Add(new SkippedCookSource { Source = source, ErrorCount = errors });
                    _ = this.skipped.Add(source);
                }
                else
                {
                    cook.Add(source);
                }
            }
        }

        if (skip.Count != 0)
        {
            this.LogSkippedForErrors(skip);
        }

        return cook.Count == 0 ? null : new CookChanges { Sources = cook, Skipped = skip };
    }

    // The errors the family facts report for a source: script analysis and input source checks.
    private int ErrorCount(AssetPath source)
    {
        var identity = AssetFileTypes.ToIdentity(source);
        return (this.scripts.Find(identity)?.ErrorCount ?? 0)
            + this.inputs.Problems.Count(problem => problem is InputProblem { Severity: >= DiagnosticSeverity.Error } input && input.Container == identity);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Background cook failures are recorded in Cooking and logged; they must not escape onto the catalog.")]
    private async Task CookAsync(ProjectContext current, CookChanges changes, CancellationToken lifetime)
    {
        try
        {
            _ = await this.pipeline.CookChangedSourcesAsync(changes, current, lifetime).ConfigureAwait(false);
        }
        catch (CookPreemptedException)
        {
            // Stopped for a cook the user asked for: the next run, after it, cooks what it did not cover.
            lock (this.sync)
            {
                if (!this.disposed && ReferenceEquals(current, this.project))
                {
                    this.waiting.UnionWith(changes.Sources);
                }
            }
        }
        catch (OperationCanceledException)
        {
            // Cancellation stays visible in the run history; the next change cooks the sources again.
        }
        catch (Exception exception)
        {
            this.LogSchedulingFailure(exception, changes.Sources.Count);
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
}
