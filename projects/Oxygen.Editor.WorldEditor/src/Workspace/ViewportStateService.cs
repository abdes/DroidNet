// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

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
/// The service loads a project's state once and then serves it from memory, which is always at
/// least as new as the stored state: a load that completes after an update never replaces the
/// updated scene. Writes are serialized, so a fast sequence of updates cannot interleave.
/// </para>
/// </remarks>
/// <param name="settings">The editor settings manager, accessed serially by this service.</param>
/// <param name="results">The visible diagnostic publisher.</param>
/// <param name="status">The workspace diagnostic status.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class ViewportStateService(
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

    private readonly Dictionary<string, PendingWrite> pendingWrites = new(StringComparer.Ordinal);

    // Loads and writes run one at a time, in request order.
    private Task persistence = Task.CompletedTask;
    private ProjectContext? activeProject;
    private Dictionary<Guid, SceneViewportState> scenes = [];
    private long activation;

    /// <summary>Gets the state kept for a scene, loading the project's state on first use.</summary>
    /// <param name="project">The project that owns the scene.</param>
    /// <param name="sceneId">The scene identity.</param>
    /// <returns>The kept state, or <see langword="null"/> when the scene has none.</returns>
    public async Task<SceneViewportState?> RestoreAsync(ProjectContext project, Guid sceneId)
    {
        ArgumentNullException.ThrowIfNull(project);
        var scenesOfProject = this.Activate(project);
        await this.persistence.ConfigureAwait(true);
        return scenesOfProject.GetValueOrDefault(sceneId);
    }

    /// <summary>Keeps a scene's state and schedules a write of its project's state.</summary>
    /// <param name="project">The project that owns the scene.</param>
    /// <param name="sceneId">The scene identity.</param>
    /// <param name="state">The scene's current viewport state.</param>
    public void Update(ProjectContext project, Guid sceneId, SceneViewportState state)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(state);
        var scenesOfProject = this.Activate(project);
        scenesOfProject[sceneId] = state;

        // The write runs after the project's load, so it carries every stored scene too.
        this.pendingWrites[RootOf(project)] = new PendingWrite(project, scenesOfProject);
        this.persistence = this.WritePendingAfterAsync(this.persistence);
    }

    /// <summary>Waits until every update made so far is written.</summary>
    /// <returns>A task that completes when the writes are done.</returns>
    public Task FlushAsync() => this.persistence;

    // The project scope id is the normalized project root, the same identity the other workspace
    // settings use; the stored ProjectId inside the value is what rejects a reused path.
    private static string RootOf(ProjectContext project)
        => Path.TrimEndingDirectorySeparator(Path.GetFullPath(project.ProjectRoot)).ToUpperInvariant();

    private static bool IsSameProject(ProjectContext? left, ProjectContext right)
        => left is not null && left.ProjectId == right.ProjectId
            && string.Equals(RootOf(left), RootOf(right), StringComparison.Ordinal);

    private static bool IsValid(SceneViewportState? scene)
        => scene is { Panes: { } panes }
            && Enum.IsDefined(scene.Layout)
            && scene.FocusedPane >= 0
            && panes.All(static pane => pane is not null
                && Enum.IsDefined(pane.CameraType)
                && Enum.IsDefined(pane.ControlMode)
                && pane.EditorCamera?.IsFinite() != false);

    private Dictionary<Guid, SceneViewportState> Activate(ProjectContext project)
    {
        if (IsSameProject(this.activeProject, project))
        {
            return this.scenes;
        }

        this.activeProject = project;
        this.scenes = [];
        var generation = ++this.activation;
        this.persistence = this.LoadAfterAsync(this.persistence, project, this.scenes, generation);
        return this.scenes;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Unreadable viewport state is discarded with a warning; it must never block opening a scene.")]
    private async Task LoadAfterAsync(Task previous, ProjectContext project, Dictionary<Guid, SceneViewportState> target, long generation)
    {
        await previous.ConfigureAwait(true);
        ProjectViewports? stored;
        try
        {
            stored = await settings.LoadSettingAsync(Key, SettingContext.Project(RootOf(project))).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.ReportDiscarded(project, "VIEWPORT_STATE_UNREADABLE", "The saved viewport state could not be read.", failure);
            return;
        }

        // A record written for another project is ignored outright: the settings scope is reused by
        // path, and a copied project must not inherit a stranger's viewports.
        if (stored is null || stored.ProjectId != project.ProjectId)
        {
            return;
        }

        if (stored.Version != CurrentVersion || stored.Scenes is null || !stored.Scenes.Values.All(IsValid))
        {
            this.ReportDiscarded(
                project,
                "VIEWPORT_STATE_UNSUPPORTED",
                $"The saved viewport state (version {stored.Version}) is not supported.",
                new InvalidDataException("Viewport state payload is unsupported or invalid."));
            return;
        }

        this.LogRestored(stored.Scenes.Count, project.ProjectRoot, generation);
        foreach (var (sceneId, scene) in stored.Scenes)
        {
            // State updated while the load was in flight is newer than the stored one.
            _ = target.TryAdd(sceneId, scene);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Viewport state persistence must report storage failures without losing later writes or disturbing the editor.")]
    private async Task WritePendingAfterAsync(Task previous)
    {
        await previous.ConfigureAwait(true);
        while (this.pendingWrites.Count > 0)
        {
            var (root, write) = this.pendingWrites.First();
            _ = this.pendingWrites.Remove(root);
            var value = new ProjectViewports(write.Project.ProjectId, CurrentVersion, new Dictionary<Guid, SceneViewportState>(write.Scenes));
            try
            {
                await settings.SaveSettingAsync(Key, value, SettingContext.Project(root)).ConfigureAwait(true);
            }
            catch (Exception failure)
            {
                this.LogWriteFailed(failure, write.Project.ProjectRoot);
                RuntimeOperationResults.PublishWarning(
                    results,
                    status,
                    RuntimeOperationKinds.SettingsApply,
                    FailureDomain.Settings,
                    DiagnosticCodes.SettingsPrefix + "VIEWPORT_STATE_SAVE_FAILED",
                    "Viewport layout was not saved",
                    "The scene's viewports keep working, but reopen with their previous layout.",
                    new AffectedScope { ProjectId = write.Project.ProjectId, ProjectName = write.Project.Name, ProjectPath = write.Project.ProjectRoot },
                    exception: failure);
            }
        }
    }

    private void ReportDiscarded(ProjectContext project, string code, string reason, Exception exception)
    {
        this.LogDiscarded(exception, code, project.ProjectRoot);
        RuntimeOperationResults.PublishWarning(
            results,
            status,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            DiagnosticCodes.SettingsPrefix + code,
            "Viewport layout was not restored",
            reason + " Scenes open with the default viewports.",
            new AffectedScope { ProjectId = project.ProjectId, ProjectName = project.Name, ProjectPath = project.ProjectRoot },
            exception: exception);
    }

    [LoggerMessage(Level = LogLevel.Debug, Message = "Restored viewport state for {SceneCount} scene(s) of project {ProjectRoot} (activation {Activation}).")]
    private partial void LogRestored(int sceneCount, string projectRoot, long activation);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Discarded saved viewport state ({Code}) of project {ProjectRoot}.")]
    private partial void LogDiscarded(Exception exception, string code, string projectRoot);

    [LoggerMessage(Level = LogLevel.Error, Message = "Viewport state was not saved for project {ProjectRoot}.")]
    private partial void LogWriteFailed(Exception exception, string projectRoot);

    /// <summary>The saved viewport state of a project, guarded by project identity and version.</summary>
    /// <param name="ProjectId">The project owning this state.</param>
    /// <param name="Version">The payload version.</param>
    /// <param name="Scenes">The viewport state keyed by stable scene identity.</param>
    internal sealed record ProjectViewports(
        Guid ProjectId,
        int Version,
        IReadOnlyDictionary<Guid, SceneViewportState> Scenes);

    private readonly record struct PendingWrite(ProjectContext Project, Dictionary<Guid, SceneViewportState> Scenes);
}

/// <summary>The kept viewport state of one scene.</summary>
/// <param name="Layout">The pane layout.</param>
/// <param name="FocusedPane">The layout index of the focused pane.</param>
/// <param name="Panes">The panes' state, by layout index.</param>
public sealed record SceneViewportState(
    SceneViewLayout Layout,
    int FocusedPane,
    IReadOnlyList<ViewportPaneState> Panes);

/// <summary>The kept state of one viewport pane.</summary>
/// <param name="CameraType">The view preset.</param>
/// <param name="ControlMode">The editor camera control mode.</param>
/// <param name="EditorCamera">The editor camera, or <see langword="null"/> when the pane never had a view.</param>
/// <param name="SceneCameraId">The stable node ID of the scene camera the pane looks through, if any.</param>
public sealed record ViewportPaneState(
    CameraType CameraType,
    CameraControlMode ControlMode,
    ViewportCameraState? EditorCamera,
    Guid? SceneCameraId);

/// <summary>
/// A pane's editor camera, stored as plain fields: the settings serializer does not write the
/// fields of <see cref="Vector3"/> and <see cref="Quaternion"/>.
/// </summary>
/// <param name="PositionX">The position X.</param>
/// <param name="PositionY">The position Y.</param>
/// <param name="PositionZ">The position Z.</param>
/// <param name="RotationX">The rotation quaternion X.</param>
/// <param name="RotationY">The rotation quaternion Y.</param>
/// <param name="RotationZ">The rotation quaternion Z.</param>
/// <param name="RotationW">The rotation quaternion W.</param>
/// <param name="FocusX">The orbit focus point X.</param>
/// <param name="FocusY">The orbit focus point Y.</param>
/// <param name="FocusZ">The orbit focus point Z.</param>
/// <param name="OrthographicSize">The orthographic half-height.</param>
public sealed record ViewportCameraState(
    float PositionX,
    float PositionY,
    float PositionZ,
    float RotationX,
    float RotationY,
    float RotationZ,
    float RotationW,
    float FocusX,
    float FocusY,
    float FocusZ,
    float OrthographicSize)
{
    /// <summary>Creates the stored form of a runtime editor camera.</summary>
    /// <param name="camera">The runtime camera, or <see langword="null"/>.</param>
    /// <returns>The stored camera, or <see langword="null"/>.</returns>
    public static ViewportCameraState? From(RuntimeEditorCamera? camera)
        => camera is null ? null : new(
            camera.Position.X,
            camera.Position.Y,
            camera.Position.Z,
            camera.Rotation.X,
            camera.Rotation.Y,
            camera.Rotation.Z,
            camera.Rotation.W,
            camera.FocusPoint.X,
            camera.FocusPoint.Y,
            camera.FocusPoint.Z,
            camera.OrthographicSize);

    /// <summary>Creates the runtime editor camera this state describes.</summary>
    /// <returns>The runtime camera.</returns>
    public RuntimeEditorCamera ToRuntime()
        => new(
            new Vector3(this.PositionX, this.PositionY, this.PositionZ),
            new Quaternion(this.RotationX, this.RotationY, this.RotationZ, this.RotationW),
            new Vector3(this.FocusX, this.FocusY, this.FocusZ),
            this.OrthographicSize);

    /// <summary>Gets whether every component is a finite number.</summary>
    /// <returns><see langword="true"/> when the state is usable.</returns>
    public bool IsFinite()
        => new[] { this.PositionX, this.PositionY, this.PositionZ, this.RotationX, this.RotationY, this.RotationZ, this.RotationW, this.FocusX, this.FocusY, this.FocusZ, this.OrthographicSize }
            .All(float.IsFinite);
}
