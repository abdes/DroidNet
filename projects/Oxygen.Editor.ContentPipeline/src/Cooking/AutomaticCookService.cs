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
/// to <see cref="QuietWindowLimit"/> after the first change. When it closes, the changed sources wait for the next
/// automatic run, which starts once the project writer is idle, so no cook, import or relocation is half done. Sources from every window that closes while it
/// waits join it, and so do those that change while an automatic run cooks: there is one automatic run at a time, and
/// a running one is never interrupted by changes. When it starts, while Cook on change is on, it covers each waiting
/// source whose published output does not match what is on disk; a cook that ran after the change, such as Play's,
/// already covered it. A source that was moved or deleted is a change too: if it has published output, the run
/// publishes the content again without it, and if it was never cooked there is nothing to do.
/// <para>
/// A run fails as a whole when one of its inputs fails, so a source whose family facts (scripts, input sources) report
/// errors, or that uses a source with errors, such as a scene whose script has errors, is left out and named in the
/// run; the problems are in Problems. The facts of the sources a run reads are brought up to date with their files
/// first, so an analysis still behind a change does not let a broken source through. A skipped source cooks once none
/// of the sources it was checked with has errors. A run stopped for a cook the user asked for returns its sources to
/// the next run, which cooks those that cook did not cover.
/// </para>
/// </remarks>
public sealed partial class AutomaticCookService : IAutomaticCookService, IDisposable
{
    /// <summary>How long the sources must stay unchanged before they are cooked.</summary>
    public static readonly TimeSpan QuietWindow = TimeSpan.FromMilliseconds(500);

    /// <summary>How long after its first change a window closes, even while changes keep coming.</summary>
    public static readonly TimeSpan QuietWindowLimit = TimeSpan.FromSeconds(5);

    private readonly IProjectSession session;
    private readonly IProjectManagerService projectManager;
    private readonly IContentPipelineService pipeline;
    private readonly IAssetCookStatusReader statuses;
    private readonly ICookRunService runs;
    private readonly IScripts scripts;
    private readonly IInputSources inputs;
    private readonly TimeProvider time;
    private readonly ILogger<AutomaticCookService> logger;
    private readonly Lock sync = new();
    private readonly HashSet<AssetPath> changed = [];
    private readonly HashSet<AssetPath> waiting = [];
    private readonly Dictionary<AssetPath, AssetPath[]> skipped = [];
    private readonly ITimer window;
    private readonly IDisposable catalogSubscription;
    private ProjectContext? project;
    private CancellationTokenSource projectLifetime = new();
    private long windowOpenedAt;
    private bool windowOpen;
    private bool draining;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AutomaticCookService"/> class.</summary>
    /// <param name="session">The project session: its project as its content mounts are now, its settings and their changes.</param>
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
        IProjectSession session,
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
        this.session = session;
        this.projectManager = projectManager;
        this.pipeline = pipeline;
        this.statuses = statuses;
        this.runs = runs;
        this.scripts = scripts;
        this.inputs = inputs;
        this.logger = logger;
        this.time = time ?? TimeProvider.System;
        this.window = this.time.CreateTimer(_ => this.CloseWindow(), state: null, Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        this.OnContextChanged(this, EventArgs.Empty);
        session.ContextChanged += this.OnContextChanged;
        session.SettingsChanged += this.OnSettingsChanged;
        this.catalogSubscription = catalog.Changes.Subscribe(this.OnCatalogChange);
        scripts.ProblemsChanged += this.OnProblemsChanged;
        inputs.ProblemsChanged += this.OnProblemsChanged;
    }

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc />
    public bool CookOnChange => this.session.Settings.CookOnChange;

    /// <inheritdoc />
    public async Task SetCookOnChangeAsync(bool enabled, CancellationToken cancellationToken = default)
    {
        var current = this.session.Context;
        var settings = this.session.Settings with { CookOnChange = enabled };
        if (settings == this.session.Settings)
        {
            return;
        }

        await this.projectManager.SaveProjectSettingsAsync(current.ProjectRoot.FullPath, settings, cancellationToken).ConfigureAwait(true);
        this.session.UpdateSettings(settings);
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
        this.session.SettingsChanged -= this.OnSettingsChanged;
        this.session.ContextChanged -= this.OnContextChanged;
        this.window.Dispose();
        this.projectLifetime.Dispose();
    }

    // A source name the catalog reports for a cooked record resolves to the same source; only a change to the source
    // file itself, an authored descriptor or a script, is cooked.
    private static bool IsCookableSource(ProjectContext project, AssetPath uri)
        => project.AuthoringMounts.Any(mount => !ProjectOutputFolders.Contains(mount) && string.Equals(mount.Name, uri.Mount, StringComparison.Ordinal))
            && project.Find(uri) is { IsMountRoot: false } location
            && (ContentPipelineService.IsCookableDescriptorFile(project, location.File.FullPath) || AssetFileTypes.Is(uri.Name, AssetFileCategory.Script));

    // The other sources in a source's dependency closure, as the catalog names them.
    private static AssetPath[] UsedSources(ProjectContext project, AssetPath source, AssetCookStatus status)
        => [.. status.SourcePaths.Select(path => project.FindByFilePath(path)?.Path).OfType<AssetPath>().Where(used => used != source).Distinct()];

    // The cook follows the session's project in its current content mounts; the runs of the earlier mounts stop.
    private void OnContextChanged(object? sender, EventArgs args)
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
            this.project = this.session.Context;
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

    private void OnSettingsChanged(object? sender, EventArgs args)
    {
        var settings = this.session.Settings;

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

    // A skipped source is a change once no source it was checked with has errors: the fixing change may have been to a
    // script it uses, or may have come before the facts caught up with it.
    private void OnProblemsChanged(object? sender, EventArgs args)
    {
        lock (this.sync)
        {
            if (this.disposed || this.project is not { } current || this.skipped.Count == 0)
            {
                return;
            }

            foreach (var source in this.skipped.Where(item => item.Value.All(checkedSource => this.ErrorCount(checkedSource) == 0)).Select(static item => item.Key).ToList())
            {
                this.Note(current, source);
            }
        }
    }

    // Called under sync.
    private void Note(ProjectContext current, AssetPath uri)
    {
        _ = this.skipped.Remove(uri);
        if (!IsCookableSource(current, uri))
        {
            this.LogChangeIgnored(uri);
            return;
        }

        _ = this.changed.Add(uri);
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
        lock (this.sync)
        {
            if (this.disposed || this.project is null || !this.windowOpen)
            {
                return;
            }

            this.windowOpen = false;
            this.waiting.UnionWith(this.changed);
            this.changed.Clear();
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

    private async Task<CookChanges?> SelectAsync(ProjectContext current, List<AssetPath> sources, CancellationToken lifetime)
    {
        this.LogCheckingChangedSources(sources);
        if (!this.CookOnChange)
        {
            this.LogNotCooked(sources.Count);
            return null;
        }

        if (await this.ReadStaleAsync(current, sources, lifetime).ConfigureAwait(false) is not { } stale)
        {
            return null;
        }

        if (!this.CookOnChange)
        {
            this.LogNotCooked(stale.Count);
            return null;
        }

        // The facts of a file the run reads may still be behind its change: they are brought up to date first.
        AssetPath[] read = [.. stale.SelectMany(static item => item.Uses.Prepend(item.Source)).Distinct()];
        try
        {
            this.inputs.Check(read);
            await this.scripts.CheckAsync(read, lifetime).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested)
        {
            return null;
        }

        return this.LeaveOutSourcesWithErrors(current, stale);
    }

    // The changed sources whose published output does not match the files, each with the other sources it uses; null
    // when the project closed.
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed status read cooks the sources instead of leaving them stale.")]
    private async Task<List<(AssetPath Source, AssetPath[] Uses)>?> ReadStaleAsync(ProjectContext current, List<AssetPath> sources, CancellationToken lifetime)
    {
        try
        {
            var read = await this.statuses.ReadAsync(current, sources, lifetime).ConfigureAwait(false);
            var cooked = read.Where(static status => status.Freshness == AssetCookFreshness.Current).Select(static status => status.AssetUri).ToHashSet();

            // A source that is gone is cooked only to remove the output it published. One that cannot be a cook input,
            // such as a deleted script, would only fail the run; the next cook removes what it published.
            var gone = read.Where(static status => (status.Freshness == AssetCookFreshness.MissingSource && !status.HasPublishedOutput)
                    || status.Diagnostics.Any(static issue => string.Equals(issue.Code, AssetCookStatusReader.InputUnresolvedCode, StringComparison.Ordinal)))
                .Select(static status => status.AssetUri).ToHashSet();
            if (cooked.Count > 0)
            {
                this.LogAlreadyCooked(cooked.Count);
            }

            if (gone.Count > 0)
            {
                this.LogSourcesGone(gone.Count);
            }

            var statuses = read.DistinctBy(static status => status.AssetUri).ToDictionary(static status => status.AssetUri);
            return [.. sources.Where(asset => !cooked.Contains(asset) && !gone.Contains(asset))
                .Select(asset => (asset, statuses.TryGetValue(asset, out var status) ? UsedSources(current, asset, status) : []))];
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested)
        {
            return null;
        }
        catch (Exception exception)
        {
            this.LogStatusUnavailable(exception, sources.Count);
            return [.. sources.Select(static asset => (asset, Array.Empty<AssetPath>()))];
        }
    }

    // A source with errors, or one that uses a source with errors, such as a scene whose script has errors, would fail
    // the run; it is left out until the facts of every source it was checked with report none.
    private CookChanges? LeaveOutSourcesWithErrors(ProjectContext current, List<(AssetPath Source, AssetPath[] Uses)> stale)
    {
        List<AssetPath> cook = [];
        List<SkippedCookSource> skip = [];
        lock (this.sync)
        {
            if (this.disposed || !ReferenceEquals(current, this.project))
            {
                return null;
            }

            foreach (var (source, uses) in stale)
            {
                var errors = this.ErrorCount(source);
                AssetPath[] broken = errors > 0 ? [] : [.. uses.Where(used => this.ErrorCount(used) > 0)];
                if (errors == 0 && broken.Length == 0)
                {
                    cook.Add(source);
                    continue;
                }

                skip.Add(new SkippedCookSource { Source = source, ErrorCount = errors > 0 ? errors : broken.Sum(this.ErrorCount), Dependencies = broken });
                this.skipped[source] = [source, .. uses];
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
}
