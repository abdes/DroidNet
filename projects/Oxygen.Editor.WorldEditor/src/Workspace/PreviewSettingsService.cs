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
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Owns the active project's embedded-engine preferences and their user-local persistence.</summary>
/// <param name="engine">The shared embedded runtime.</param>
/// <param name="settings">A dedicated settings manager, accessed serially by this service.</param>
/// <param name="projects">The current project lifetime.</param>
/// <param name="results">The visible diagnostic publisher.</param>
/// <param name="status">The workspace diagnostic status.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class PreviewSettingsService(
    IEngineService engine,
    IEditorSettingsManager settings,
    IProjectContextService projects,
    IOperationResultPublisher results,
    IStatusReducer status,
    ILoggerFactory? loggerFactory = null) : ObservableObject
{
    /// <summary>The project-scoped preference identity in the editor database.</summary>
    internal static readonly SettingKey<Preferences> Key = new("WorldEditor", "Preview");
    private readonly ILogger logger = loggerFactory?.CreateLogger<PreviewSettingsService>() ?? NullLogger<PreviewSettingsService>.Instance;

    // Serializes database access across project switches and toolbar writes.
    private Task persistence = Task.CompletedTask;
    private Task savePump = Task.CompletedTask;
    private PendingWrite? pendingWrite;
    private bool saveFailed;
    private ProjectContext? activeProject;
    private ProjectContext? requestedProject;
    private long activation;
    private Preferences accepted = new();
    private int maxFrameRateCap = EngineConstants.DefaultFrameRateCap;

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

    /// <summary>Gets or sets the accepted native logging threshold.</summary>
    public int LoggingVerbosity
    {
        get => this.accepted.LoggingVerbosity;
        set => this.Apply(this.accepted with { LoggingVerbosity = value }, nameof(this.LoggingVerbosity));
    }

    /// <summary>Restores project preferences before starting or resuming its preview.</summary>
    /// <param name="project">The workspace's project.</param>
    /// <returns>The restoration task.</returns>
    public Task RestoreAsync(ProjectContext project)
    {
        this.activeProject = null;
        this.requestedProject = project;
        var restoration = this.RestoreAfterAsync(this.persistence, project, ++this.activation);

        // Startup observes restoration failures; the persistence queue must remain usable for the next project.
        this.persistence = restoration.ContinueWith(
            static completed => { _ = completed.Exception; },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
        return restoration;
    }

    /// <summary>Prevents a closing workspace's delayed restoration from changing the shared runtime.</summary>
    /// <param name="project">The workspace being retired.</param>
    public void Deactivate(ProjectContext project)
    {
        if (ReferenceEquals(project, this.requestedProject))
        {
            ++this.activation;
            this.requestedProject = null;
            this.activeProject = null;
        }
    }

    /// <summary>Waits for accepted preference writes before closing the workspace.</summary>
    /// <returns>The pending persistence task.</returns>
    public Task FlushAsync() => this.persistence;

    private static SettingContext Context(ProjectContext project)
        => SettingContext.Project(Path.TrimEndingDirectorySeparator(Path.GetFullPath(project.ProjectRoot)).ToUpperInvariant());

    private static string RejectionCode(string property) => property switch
    {
        nameof(VSyncEnabled) => "VSYNC_REJECTED",
        nameof(AlwaysRenderPanes) => "PANE_RENDERING_REJECTED",
        nameof(LoggingVerbosity) => "LOGGING_VERBOSITY_REJECTED",
        _ => "TARGET_FPS_REJECTED",
    };

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A preference-store failure is reported and uses visible defaults without preventing project authoring.")]
    private async Task RestoreAfterAsync(Task previous, ProjectContext project, long activation)
    {
        await previous.ConfigureAwait(true);
        var maxCap = (int)engine.MaxTargetFps;
        var value = new Preferences { ProjectId = project.ProjectId };
        try
        {
            var stored = await settings.LoadSettingAsync(Key, Context(project)).ConfigureAwait(true);
            if (stored is not null && stored.ProjectId == project.ProjectId)
            {
                if (stored.FrameRateCap < 1 || stored.FrameRateCap > maxCap
                    || stored.LoggingVerbosity is < EngineConstants.MinLoggingVerbosity or > EngineConstants.MaxLoggingVerbosity)
                {
                    throw new InvalidDataException("Saved preview preferences contain an unsupported frame-rate cap or logging value.");
                }

                value = stored;
            }
        }
        catch (Exception failure)
        {
            this.ReportFailure(
                project,
                "LOAD_FAILED",
                "Preview preferences could not be restored",
                "Using vsync, no frame-rate cap and Error logging. See Details for the storage error.",
                failure);
        }

        if (activation != this.activation || !ReferenceEquals(project, projects.ActiveProject))
        {
            return;
        }

        this.MaxFrameRateCap = maxCap;
        this.ApplyToEngine(previous: null, value);
        this.activeProject = project;
        this.saveFailed = false;
        this.SetAccepted(value);
    }

    private void Apply(Preferences requested, string property)
    {
        var project = this.activeProject;
        try
        {
            if (project is null || !ReferenceEquals(project, projects.ActiveProject))
            {
                throw new InvalidOperationException("The project's preview preferences are not active.");
            }

            var next = requested with
            {
                ProjectId = project.ProjectId,
                FrameRateCap = Math.Clamp(requested.FrameRateCap, 1, this.MaxFrameRateCap),
                LoggingVerbosity = Math.Clamp(requested.LoggingVerbosity, EngineConstants.MinLoggingVerbosity, EngineConstants.MaxLoggingVerbosity),
            };
            if (next == this.accepted && !this.saveFailed)
            {
                return;
            }

            this.ApplyToEngine(this.accepted, next);
            this.SetAccepted(next);
            this.pendingWrite = new(project, next);
            if (this.savePump.IsCompleted)
            {
                this.persistence = this.savePump = this.SavePendingAfterAsync(this.persistence);
            }
        }
        catch (Exception failure) when (failure is InvalidOperationException or ArgumentOutOfRangeException)
        {
            this.OnPropertyChanged(property);
            this.ReportFailure(
                project ?? projects.ActiveProject,
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

        if (previous?.LoggingVerbosity != next.LoggingVerbosity)
        {
            engine.EngineLoggingVerbosity = next.LoggingVerbosity;
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Async preference persistence must report storage failures without losing later writes or crashing UI bindings.")]
    private async Task SavePendingAfterAsync(Task previous)
    {
        await previous.ConfigureAwait(true);
        while (this.pendingWrite is { } write)
        {
            this.pendingWrite = null;
            try
            {
                await settings.SaveSettingAsync(Key, write.Value, Context(write.Project)).ConfigureAwait(true);
                this.saveFailed = false;
            }
            catch (Exception failure)
            {
                this.saveFailed = true;
                this.ReportFailure(
                    write.Project,
                    "SAVE_FAILED",
                    "Preview preferences were not saved",
                    "Applied for this session, but could not save. Change the setting again to retry.",
                    failure);
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

        if (previous.LoggingVerbosity != value.LoggingVerbosity)
        {
            this.OnPropertyChanged(nameof(this.LoggingVerbosity));
        }
    }

    private void ReportFailure(ProjectContext? project, string code, string title, string message, Exception exception)
    {
        this.LogPreferenceFailure(exception, code, project?.ProjectRoot);
        RuntimeOperationResults.PublishFailure(
            results,
            status,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            DiagnosticCodes.SettingsPrefix + code,
            title,
            message,
            project is null ? AffectedScope.Empty : new AffectedScope { ProjectId = project.ProjectId, ProjectName = project.Name, ProjectPath = project.ProjectRoot },
            exception: exception);
    }

    /// <summary>The saved user preferences, guarded by project identity.</summary>
    /// <remarks>
    ///     Defaults apply to members a stored value lacks, so preferences saved before vsync, the frame-rate cap and
    ///     pane rendering existed restore with vsync on and no cap, keeping their logging threshold.
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

        /// <summary>Gets the native logging threshold.</summary>
        public int LoggingVerbosity { get; init; } = EngineConstants.DefaultLoggingVerbosity;
    }

    private sealed record PendingWrite(ProjectContext Project, Preferences Value);
}
