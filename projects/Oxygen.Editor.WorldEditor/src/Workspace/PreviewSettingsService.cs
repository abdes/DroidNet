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

/// <summary>Owns the active project's preview preferences and their user-local persistence.</summary>
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
    private int runAtFps = EngineConstants.DefaultTargetFps;
    private int loggingVerbosity = EngineConstants.DefaultLoggingVerbosity;

    /// <summary>Gets or sets the accepted preview frame-rate limit.</summary>
    public int RunAtFps
    {
        get => this.runAtFps;
        set => this.Apply(value, this.loggingVerbosity, changeFps: true);
    }

    /// <summary>Gets or sets the accepted native logging threshold.</summary>
    public int LoggingVerbosity
    {
        get => this.loggingVerbosity;
        set => this.Apply(this.runAtFps, value, changeFps: false);
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

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A preference-store failure is reported and uses visible defaults without preventing project authoring.")]
    private async Task RestoreAfterAsync(Task previous, ProjectContext project, long activation)
    {
        await previous.ConfigureAwait(true);
        var value = new Preferences(project.ProjectId, EngineConstants.DefaultTargetFps, EngineConstants.DefaultLoggingVerbosity);
        try
        {
            var stored = await settings.LoadSettingAsync(Key, Context(project)).ConfigureAwait(true);
            if (stored is not null && stored.ProjectId == project.ProjectId)
            {
                if (stored.TargetFps < 1 || stored.TargetFps > engine.MaxTargetFps
                    || stored.LoggingVerbosity is < EngineConstants.MinLoggingVerbosity or > EngineConstants.MaxLoggingVerbosity)
                {
                    throw new InvalidDataException("Saved preview preferences contain an unsupported FPS or logging value.");
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
                "Using 60 FPS and Error logging. See Details for the storage error.",
                failure);
        }

        if (activation != this.activation || !ReferenceEquals(project, projects.ActiveProject))
        {
            return;
        }

        engine.TargetFps = (uint)value.TargetFps;
        engine.EngineLoggingVerbosity = value.LoggingVerbosity;
        this.activeProject = project;
        this.saveFailed = false;
        this.SetAcceptedValues(value.TargetFps, value.LoggingVerbosity);
    }

    private void Apply(int fps, int verbosity, bool changeFps)
    {
        var project = this.activeProject;
        try
        {
            if (project is null || !ReferenceEquals(project, projects.ActiveProject))
            {
                throw new InvalidOperationException("The project's preview preferences are not active.");
            }

            fps = Math.Clamp(fps, 1, (int)engine.MaxTargetFps);
            verbosity = Math.Clamp(verbosity, EngineConstants.MinLoggingVerbosity, EngineConstants.MaxLoggingVerbosity);
            if (fps == this.runAtFps && verbosity == this.loggingVerbosity && !this.saveFailed)
            {
                return;
            }

            if (changeFps)
            {
                engine.TargetFps = (uint)fps;
            }
            else
            {
                engine.EngineLoggingVerbosity = verbosity;
            }

            this.SetAcceptedValues(fps, verbosity);
            this.pendingWrite = new(project, new(project.ProjectId, fps, verbosity));
            if (this.savePump.IsCompleted)
            {
                this.persistence = this.savePump = this.SavePendingAfterAsync(this.persistence);
            }
        }
        catch (Exception failure) when (failure is InvalidOperationException or ArgumentOutOfRangeException)
        {
            this.OnPropertyChanged(changeFps ? nameof(this.RunAtFps) : nameof(this.LoggingVerbosity));
            this.ReportFailure(
                project ?? projects.ActiveProject,
                changeFps ? "TARGET_FPS_REJECTED" : "LOGGING_VERBOSITY_REJECTED",
                "Preview setting was not applied",
                "The runtime rejected the preview setting.",
                failure);
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

    private void SetAcceptedValues(int fps, int verbosity)
    {
        _ = this.SetProperty(ref this.runAtFps, fps, nameof(this.RunAtFps));
        _ = this.SetProperty(ref this.loggingVerbosity, verbosity, nameof(this.LoggingVerbosity));
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

    [LoggerMessage(Level = LogLevel.Error, Message = "Preview preferences failed ({Code}) for project {ProjectRoot}.")]
    private partial void LogPreferenceFailure(Exception exception, string code, string? projectRoot);

    /// <summary>The saved user preferences, guarded by project identity.</summary>
    /// <param name="ProjectId">The project owning these preferences.</param>
    /// <param name="TargetFps">The preview frame-rate limit.</param>
    /// <param name="LoggingVerbosity">The native logging threshold.</param>
    internal sealed record Preferences(Guid ProjectId, int TargetFps, int LoggingVerbosity);

    private sealed record PendingWrite(ProjectContext Project, Preferences Value);
}
