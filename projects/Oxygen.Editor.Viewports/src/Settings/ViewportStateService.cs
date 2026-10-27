// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Viewports.Settings;

/// <summary>
/// Keeps each scene's viewport layout, focused pane and pane cameras across sessions, so a scene
/// reopens as the user left it.
/// </summary>
/// <remarks>
/// <para>
/// This is user-local workspace state, never authoring data: it does not dirty a scene, enter
/// history or reach cooked output. A project's state is one typed setting (<see cref="Key"/>)
/// holding per-scene records, guarded by the project identity and a payload version.
/// </para>
/// <para>
/// The service belongs to a project session and serves its one project, which it reads from the
/// session when it loads, saves or reports. It loads the project's state once and then serves it
/// from memory, which is always at least as new as the stored state: a load that completes after
/// an update never replaces the updated scene. Writes are serialized, so a fast sequence of updates
/// cannot interleave.
/// </para>
/// </remarks>
/// <param name="session">The project session whose scenes the state belongs to.</param>
/// <param name="settings">The editor settings manager, accessed serially by this service.</param>
/// <param name="results">The visible diagnostic publisher.</param>
/// <param name="status">The workspace diagnostic status.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class ViewportStateService(
    IProjectSession session,
    IEditorSettingsManager settings,
    IOperationResultPublisher results,
    IStatusReducer status,
    ILoggerFactory? loggerFactory = null)
{
    /// <summary>The payload version this editor reads and writes.</summary>
    internal const int CurrentVersion = 1;

    /// <summary>The project-scoped viewport state identity in the editor database.</summary>
    internal static readonly SettingKey<ProjectViewports> Key = new("WorldEditor", "SceneViewports");

    private readonly ILogger logger = loggerFactory?.CreateLogger<ViewportStateService>()
        ?? NullLogger<ViewportStateService>.Instance;

    private readonly Dictionary<SceneId, SceneViewportState> scenes = [];

    // Loads and writes run one at a time, in request order.
    private Task persistence = Task.CompletedTask;
    private bool loadStarted;
    private bool writePending;

    private ProjectContext Project => session.Context;

    /// <summary>Gets the state kept for a scene, loading the project's state on first use.</summary>
    /// <param name="sceneId">The scene identity.</param>
    /// <returns>The kept state, or <see langword="null"/> when the scene has none.</returns>
    public async Task<SceneViewportState?> RestoreAsync(SceneId sceneId)
    {
        this.EnsureLoadStarted();
        await this.persistence.ConfigureAwait(true);
        return this.scenes.GetValueOrDefault(sceneId);
    }

    /// <summary>Keeps a scene's state and schedules a write of the project's state.</summary>
    /// <param name="sceneId">The scene identity.</param>
    /// <param name="state">The scene's current viewport state.</param>
    public void Update(SceneId sceneId, SceneViewportState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        this.EnsureLoadStarted();
        this.scenes[sceneId] = state;

        // The write runs after the project's load, so it carries every stored scene too.
        this.writePending = true;
        this.persistence = this.WritePendingAfterAsync(this.persistence);
    }

    /// <summary>Waits until every update made so far is written.</summary>
    /// <returns>A task that completes when the writes are done.</returns>
    public Task FlushAsync() => this.persistence;

    private static bool IsValid(SceneViewportState? scene)
        => scene is { Panes: { } panes }
            && Enum.IsDefined(scene.Layout)
            && scene.FocusedPane >= 0
            && panes.All(static pane => pane is not null
                && Enum.IsDefined(pane.CameraType)
                && Enum.IsDefined(pane.ViewMode)
                && pane.EditorCamera?.IsFinite() != false);

    private void EnsureLoadStarted()
    {
        if (this.loadStarted)
        {
            return;
        }

        this.loadStarted = true;
        this.persistence = this.LoadAfterAsync(this.persistence);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Unreadable viewport state is discarded with a warning; it must never block opening a scene.")]
    private async Task LoadAfterAsync(Task previous)
    {
        await previous.ConfigureAwait(true);
        ProjectViewports? stored;
        try
        {
            stored = await settings.LoadSettingAsync(Key, this.Project.SettingsScope).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.ReportDiscarded("VIEWPORT_STATE_UNREADABLE", "The saved viewport state could not be read.", failure);
            return;
        }

        // A record written for another project is ignored outright: the settings scope is reused by
        // path, and a copied project must not inherit a stranger's viewports.
        if (stored is null || stored.ProjectId != this.Project.ProjectId)
        {
            return;
        }

        if (stored.Version != CurrentVersion || stored.Scenes is null || !stored.Scenes.Values.All(IsValid))
        {
            this.ReportDiscarded(
                "VIEWPORT_STATE_UNSUPPORTED",
                $"The saved viewport state (version {stored.Version}) is not supported.",
                new InvalidDataException("Viewport state payload is unsupported or invalid."));
            return;
        }

        this.LogRestored(stored.Scenes.Count);
        foreach (var (sceneId, scene) in stored.Scenes)
        {
            // State updated while the load was in flight is newer than the stored one.
            _ = this.scenes.TryAdd(sceneId, scene);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Viewport state persistence must report storage failures without losing later writes or disturbing the editor.")]
    private async Task WritePendingAfterAsync(Task previous)
    {
        await previous.ConfigureAwait(true);
        while (this.writePending)
        {
            this.writePending = false;
            var project = this.Project;
            var value = new ProjectViewports(project.ProjectId, CurrentVersion, new Dictionary<SceneId, SceneViewportState>(this.scenes));
            try
            {
                await settings.SaveSettingAsync(Key, value, project.SettingsScope).ConfigureAwait(true);
            }
            catch (Exception failure)
            {
                this.LogWriteFailed(failure);
                RuntimeOperationResults.PublishWarning(
                    results,
                    status,
                    RuntimeOperationKinds.SettingsApply,
                    FailureDomain.Settings,
                    DiagnosticCodes.SettingsPrefix + "VIEWPORT_STATE_SAVE_FAILED",
                    "Viewport layout was not saved",
                    "The scene's viewports keep working, but reopen with their previous layout.",
                    this.AffectedProject(),
                    exception: failure);
            }
        }
    }

    private void ReportDiscarded(string code, string reason, Exception exception)
    {
        this.LogDiscarded(exception, code);
        RuntimeOperationResults.PublishWarning(
            results,
            status,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            DiagnosticCodes.SettingsPrefix + code,
            "Viewport layout was not restored",
            reason + " Scenes open with the default viewports.",
            this.AffectedProject(),
            exception: exception);
    }

    private AffectedScope AffectedProject()
    {
        var project = this.Project;
        return new AffectedScope { ProjectId = project.ProjectId, ProjectName = project.Name, ProjectPath = project.ProjectRoot.FullPath };
    }

    /// <summary>The saved viewport state of a project, guarded by project identity and version.</summary>
    /// <param name="ProjectId">The project owning this state.</param>
    /// <param name="Version">The payload version.</param>
    /// <param name="Scenes">The viewport state keyed by stable scene identity.</param>
    internal sealed record ProjectViewports(
        Guid ProjectId,
        int Version,
        IReadOnlyDictionary<SceneId, SceneViewportState> Scenes);
}

/// <summary>The kept viewport state of one scene.</summary>
/// <param name="Layout">The pane layout.</param>
/// <param name="FocusedPane">The layout index of the focused pane.</param>
/// <param name="Panes">The panes' state, by layout index.</param>
public sealed record SceneViewportState(
    SceneViewLayout Layout,
    int FocusedPane,
    IReadOnlyList<ViewportPaneState> Panes);
