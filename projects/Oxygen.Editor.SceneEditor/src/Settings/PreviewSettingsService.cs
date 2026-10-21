// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <summary>
///     Owns the session's project's embedded-engine preferences, the user's ground grid preferences and their
///     user-local persistence.
/// </summary>
/// <remarks>
///     The scene editor restores the preferences when it opens. The engine takes them only while it is ready or running:
///     the service applies them then, and again whenever the engine enters either state, so a start after a failed one
///     shows them too. A change while the engine is down is kept and saved, and reaches the engine when it starts. Once
///     the session's lifetime ends, a restoration still in flight no longer changes the shared engine.
/// </remarks>
/// <param name="engine">The shared embedded runtime.</param>
/// <param name="settings">A dedicated settings manager, accessed serially by this service.</param>
/// <param name="session">The project session the preferences belong to.</param>
/// <param name="results">The visible diagnostic publisher.</param>
/// <param name="status">The workspace diagnostic status.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class PreviewSettingsService(
    IEngineService engine,
    IEditorSettingsManager settings,
    IProjectSession session,
    IOperationResultPublisher results,
    IStatusReducer status,
    ILoggerFactory? loggerFactory = null) : ObservableObject, IDisposable
{
    /// <summary>The project-scoped preference identity in the editor database.</summary>
    internal static readonly SettingKey<Preferences> Key = new("WorldEditor", "Preview");

    /// <summary>The user's ground grid preference identity, shared by every project.</summary>
    internal static readonly SettingKey<GroundGridSettings> GroundGridKey = new("WorldEditor", "GroundGrid");
    private readonly ILogger logger = loggerFactory?.CreateLogger<PreviewSettingsService>() ?? NullLogger<PreviewSettingsService>.Instance;

    // Serializes database access across project switches and toolbar writes.
    private Task persistence = Task.CompletedTask;
    private Task savePump = Task.CompletedTask;
    private Preferences? pendingWrite;
    private GroundGridSettings? pendingGroundGridWrite;
    private bool saveFailed;
    private bool groundGridSaveFailed;
    private GroundGridPreferences? groundGrid;
    private Task? restoration;
    private bool restored;
    private Preferences accepted = new();
    private int maxFrameRateCap = EngineConstants.DefaultFrameRateCap;
    private SynchronizationContext? owner;
    private bool watchingEngine;

    /// <summary>Gets or sets a value indicating whether presents wait for the display's vertical blank.</summary>
    public bool VSyncEnabled
    {
        get => this.accepted.VSyncEnabled;
        set => this.Apply(this.accepted with { VSyncEnabled = value }, nameof(this.VSyncEnabled));
    }

    /// <summary>Gets or sets a value indicating whether the engine frame rate is capped at <see cref="FrameRateCap"/>.</summary>
    public bool FrameRateCapEnabled
    {
        get => this.accepted.FrameRateCapEnabled;
        set => this.Apply(this.accepted with { FrameRateCapEnabled = value }, nameof(this.FrameRateCapEnabled));
    }

    /// <summary>Gets or sets the frame-rate cap, kept while the cap is off.</summary>
    public int FrameRateCap
    {
        get => this.accepted.FrameRateCap;
        set => this.Apply(this.accepted with { FrameRateCap = value }, nameof(this.FrameRateCap));
    }

    /// <summary>Gets the highest frame-rate cap the engine supports.</summary>
    public int MaxFrameRateCap
    {
        get => this.maxFrameRateCap;
        private set => this.SetProperty(ref this.maxFrameRateCap, value);
    }

    /// <summary>Gets or sets a value indicating whether every visible viewport pane renders each frame.</summary>
    public bool AlwaysRenderPanes
    {
        get => this.accepted.AlwaysRenderPanes;
        set => this.Apply(this.accepted with { AlwaysRenderPanes = value }, nameof(this.AlwaysRenderPanes));
    }

    /// <summary>Gets the user's ground grid preferences, applied to every viewport pane.</summary>
    public GroundGridPreferences GroundGrid => this.groundGrid ??= new(this);

    private ProjectContext Project => session.Context;

    /// <summary>Restores the project's preferences once; the scene editor calls it when it opens.</summary>
    /// <returns>The restoration task.</returns>
    public Task EnsureRestoredAsync()
    {
        if (this.restoration is { } started)
        {
            return started;
        }

        this.WatchEngine();
        var restoration = this.RestoreAfterAsync(this.persistence);
        this.restoration = restoration;

        // Startup observes restoration failures; the persistence queue must remain usable for later writes.
        this.persistence = restoration.ContinueWith(
            static completed => { _ = completed.Exception; },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
        return restoration;
    }

    /// <summary>Waits for accepted preference writes before closing the workspace.</summary>
    /// <returns>The pending persistence task.</returns>
    public Task FlushAsync() => this.persistence;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.watchingEngine)
        {
            engine.StateChanged -= this.OnEngineStateChanged;
            this.watchingEngine = false;
        }
    }

    /// <summary>Applies an edited ground grid to every pane and saves it for the user.</summary>
    /// <param name="requested">The edited grid; values outside the engine's ranges are clamped.</param>
    internal void ApplyGroundGrid(GroundGridSettings requested)
    {
        var next = GroundGridRanges.Clamp(requested, this.GroundGrid.Value);
        if (next == this.GroundGrid.Value && !this.groundGridSaveFailed)
        {
            if (next != requested)
            {
                this.GroundGrid.Refresh();
            }

            return;
        }

        try
        {
            if (IsLive(engine.State))
            {
                engine.SetGroundGrid(next);
            }
        }
        catch (InvalidOperationException failure)
        {
            this.GroundGrid.Refresh();
            this.ReportFailure(
                "GROUND_GRID_REJECTED",
                "Ground grid setting was not applied",
                "The runtime rejected the ground grid setting.",
                failure);
            return;
        }

        this.GroundGrid.Accept(next);
        this.pendingGroundGridWrite = next;
        if (this.savePump.IsCompleted)
        {
            this.persistence = this.savePump = this.SavePendingAfterAsync(this.persistence);
        }
    }

    private static bool IsLive(EngineServiceState state) => state is EngineServiceState.Ready or EngineServiceState.Running;

    private static SettingContext Context(ProjectContext project)
        => SettingContext.Project(project.ProjectRoot.FullPath.ToUpperInvariant());

    private static string RejectionCode(string property) => property switch
    {
        nameof(VSyncEnabled) => "VSYNC_REJECTED",
        nameof(AlwaysRenderPanes) => "PANE_RENDERING_REJECTED",
        _ => "TARGET_FPS_REJECTED",
    };

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A preference-store failure is reported and uses visible defaults without preventing project authoring.")]
    private async Task RestoreAfterAsync(Task previous)
    {
        await previous.ConfigureAwait(true);
        var project = this.Project;
        var maxCap = IsLive(engine.State) ? (int)engine.MaxTargetFps : int.MaxValue;
        var value = new Preferences { ProjectId = project.ProjectId };
        try
        {
            var stored = await settings.LoadSettingAsync(Key, Context(project)).ConfigureAwait(true);
            if (stored is not null && stored.ProjectId == project.ProjectId)
            {
                if (stored.FrameRateCap < 1 || stored.FrameRateCap > maxCap)
                {
                    throw new InvalidDataException("Saved preview preferences contain an unsupported frame-rate cap.");
                }

                value = stored;
            }
        }
        catch (Exception failure)
        {
            this.ReportFailure(
                "LOAD_FAILED",
                "Preview preferences could not be restored",
                "Using vsync and no frame-rate cap. See Details for the storage error.",
                failure);
        }

        var grid = await this.LoadGroundGridAsync().ConfigureAwait(true);
        if (session.Lifetime.IsCancellationRequested)
        {
            return;
        }

        if (IsLive(engine.State))
        {
            this.MaxFrameRateCap = maxCap;
            try
            {
                this.ApplyToEngine(previous: null, value);
                engine.SetGroundGrid(grid);
            }
            catch (InvalidOperationException failure)
            {
                // The preferences stay accepted; the engine takes them again when it next starts running.
                this.ReportFailure("APPLY_FAILED", "Preview settings were not applied", "The runtime rejected the restored preview settings.", failure);
            }
        }

        this.restored = true;
        this.saveFailed = false;
        this.groundGridSaveFailed = false;
        this.SetAccepted(value);
        this.GroundGrid.Accept(grid);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A preference-store failure is reported and uses the default grid without preventing project authoring.")]
    private async Task<GroundGridSettings> LoadGroundGridAsync()
    {
        try
        {
            var stored = await settings.LoadSettingAsync(GroundGridKey, SettingContext.Application()).ConfigureAwait(true);
            return stored is null ? new GroundGridSettings() : GroundGridRanges.Restore(stored);
        }
        catch (Exception failure)
        {
            this.ReportFailure(
                "GROUND_GRID_LOAD_FAILED",
                "Ground grid preferences could not be restored",
                "Using the default ground grid. See Details for the storage error.",
                failure);
            return new GroundGridSettings();
        }
    }

    private void Apply(Preferences requested, string property)
    {
        try
        {
            if (!this.restored || session.Lifetime.IsCancellationRequested)
            {
                throw new InvalidOperationException("The project's preview preferences are not active.");
            }

            var next = requested with
            {
                ProjectId = this.Project.ProjectId,
                FrameRateCap = Math.Clamp(requested.FrameRateCap, 1, this.MaxFrameRateCap),
            };
            if (next == this.accepted && !this.saveFailed)
            {
                return;
            }

            if (IsLive(engine.State))
            {
                this.ApplyToEngine(this.accepted, next);
            }

            this.SetAccepted(next);
            this.pendingWrite = next;
            if (this.savePump.IsCompleted)
            {
                this.persistence = this.savePump = this.SavePendingAfterAsync(this.persistence);
            }
        }
        catch (Exception failure) when (failure is InvalidOperationException or ArgumentOutOfRangeException)
        {
            this.OnPropertyChanged(property);
            this.ReportFailure(
                RejectionCode(property),
                "Preview setting was not applied",
                "The runtime rejected the preview setting.",
                failure);
        }
    }

    /// <summary>Pushes the settings that differ from <paramref name="previous"/>, or all of them when none were applied.</summary>
    private void ApplyToEngine(Preferences? previous, Preferences next)
    {
        if (previous?.VSyncEnabled != next.VSyncEnabled)
        {
            engine.SetVSyncEnabled(next.VSyncEnabled);
        }

        if (previous is null || previous.FrameRateCapEnabled != next.FrameRateCapEnabled || previous.FrameRateCap != next.FrameRateCap)
        {
            engine.TargetFps = next.FrameRateCapEnabled ? (uint)next.FrameRateCap : 0U;
        }

        if (previous?.AlwaysRenderPanes != next.AlwaysRenderPanes)
        {
            engine.SetAlwaysRenderPanes(next.AlwaysRenderPanes);
        }
    }

    private void WatchEngine()
    {
        if (this.watchingEngine)
        {
            return;
        }

        this.owner = SynchronizationContext.Current;
        engine.StateChanged += this.OnEngineStateChanged;
        this.watchingEngine = true;
    }

    // An engine that becomes ready or starts running takes the accepted preferences again.
    private void OnEngineStateChanged(object? sender, EngineStateChangedEventArgs args)
    {
        if (!IsLive(args.State))
        {
            return;
        }

        if (this.owner is { } context && SynchronizationContext.Current != context)
        {
            context.Post(_ => this.ReapplyToEngine(), state: null);
            return;
        }

        this.ReapplyToEngine();
    }

    private void ReapplyToEngine()
    {
        if (!this.restored || session.Lifetime.IsCancellationRequested || !IsLive(engine.State))
        {
            return;
        }

        try
        {
            this.MaxFrameRateCap = (int)engine.MaxTargetFps;
            var applied = this.accepted with { FrameRateCap = Math.Clamp(this.accepted.FrameRateCap, 1, this.MaxFrameRateCap) };
            this.ApplyToEngine(previous: null, applied);
            this.SetAccepted(applied);
            engine.SetGroundGrid(this.GroundGrid.Value);
        }
        catch (InvalidOperationException failure)
        {
            this.ReportFailure("APPLY_FAILED", "Preview settings were not applied", "The runtime rejected the preview settings when it started.", failure);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Async preference persistence must report storage failures without losing later writes or crashing UI bindings.")]
    private async Task SavePendingAfterAsync(Task previous)
    {
        await previous.ConfigureAwait(true);
        while (this.pendingWrite is not null || this.pendingGroundGridWrite is not null)
        {
            if (this.pendingWrite is { } write)
            {
                this.pendingWrite = null;
                try
                {
                    await settings.SaveSettingAsync(Key, write, Context(this.Project)).ConfigureAwait(true);
                    this.saveFailed = false;
                }
                catch (Exception failure)
                {
                    this.saveFailed = true;
                    this.ReportFailure(
                        "SAVE_FAILED",
                        "Preview preferences were not saved",
                        "Applied for this session, but could not save. Change the setting again to retry.",
                        failure);
                }
            }

            if (this.pendingGroundGridWrite is { } grid)
            {
                this.pendingGroundGridWrite = null;
                try
                {
                    await settings.SaveSettingAsync(GroundGridKey, grid, SettingContext.Application()).ConfigureAwait(true);
                    this.groundGridSaveFailed = false;
                }
                catch (Exception failure)
                {
                    this.groundGridSaveFailed = true;
                    this.ReportFailure(
                        "GROUND_GRID_SAVE_FAILED",
                        "Ground grid preferences were not saved",
                        "Applied for this session, but could not save. Change the setting again to retry.",
                        failure);
                }
            }
        }
    }

    private void SetAccepted(Preferences value)
    {
        var previous = this.accepted;
        this.accepted = value;
        if (previous.VSyncEnabled != value.VSyncEnabled)
        {
            this.OnPropertyChanged(nameof(this.VSyncEnabled));
        }

        if (previous.FrameRateCapEnabled != value.FrameRateCapEnabled)
        {
            this.OnPropertyChanged(nameof(this.FrameRateCapEnabled));
        }

        if (previous.FrameRateCap != value.FrameRateCap)
        {
            this.OnPropertyChanged(nameof(this.FrameRateCap));
        }

        if (previous.AlwaysRenderPanes != value.AlwaysRenderPanes)
        {
            this.OnPropertyChanged(nameof(this.AlwaysRenderPanes));
        }
    }

    private void ReportFailure(string code, string title, string message, Exception exception)
    {
        var project = this.Project;
        this.LogPreferenceFailure(exception, code, project);
        RuntimeOperationResults.PublishFailure(
            results,
            status,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            DiagnosticCodes.SettingsPrefix + code,
            title,
            message,
            new AffectedScope { ProjectId = project.ProjectId, ProjectName = project.Name, ProjectPath = project.ProjectRoot.FullPath },
            exception: exception);
    }

    /// <summary>The saved user preferences, guarded by project identity.</summary>
    /// <remarks>
    ///     Defaults apply to members a stored value lacks, so preferences saved before vsync, the frame-rate cap and
    ///     pane rendering existed restore with vsync on and no cap. Engine logging verbosity is an engine startup
    ///     setting, not a preview preference.
    /// </remarks>
    internal sealed record Preferences
    {
        /// <summary>Gets the project owning these preferences.</summary>
        public Guid ProjectId { get; init; }

        /// <summary>Gets a value indicating whether presents wait for the display's vertical blank.</summary>
        public bool VSyncEnabled { get; init; } = true;

        /// <summary>Gets a value indicating whether the frame rate is capped.</summary>
        public bool FrameRateCapEnabled { get; init; }

        /// <summary>Gets the frame-rate cap, kept while the cap is off.</summary>
        public int FrameRateCap { get; init; } = EngineConstants.DefaultFrameRateCap;

        /// <summary>Gets a value indicating whether every visible viewport pane renders each frame.</summary>
        public bool AlwaysRenderPanes { get; init; }
    }
}
