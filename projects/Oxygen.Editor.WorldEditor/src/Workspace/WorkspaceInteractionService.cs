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
using Oxygen.Editor.World;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Owns the editor-only interaction state of a project's scenes: which nodes are hidden in the
/// editing viewports ("Show in Editor") and which rows are locked against editing.
/// </summary>
/// <remarks>
/// <para>
/// This is workspace state, not authoring state. Changing it never writes
/// <c>SceneNodeFlags::kVisible</c>, never dirties a scene document, never records an undo step and
/// never asks for a cook. The ratified boundary between the three states — editor hide, editor lock
/// and authored scene visibility — is in the Explorer plan's "Show in Editor &amp; Lock" contract,
/// and <see cref="IsHidden"/> deliberately answers only the first.
/// </para>
/// <para>
/// Entries key on the stable authored node id, so renaming, regrouping or reordering a node keeps
/// its state. The whole set for a project is one typed setting (<see cref="Key"/>) holding
/// per-scene subrecords: one atomic write survives a restart, and a scene that is no longer in the
/// project is dropped when the project is next restored.
/// </para>
/// </remarks>
/// <param name="settings">The editor settings manager, accessed serially by this service.</param>
/// <param name="results">The visible diagnostic publisher.</param>
/// <param name="status">The workspace diagnostic status.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class WorkspaceInteractionService(
    IEditorSettingsManager settings,
    IOperationResultPublisher results,
    IStatusReducer status,
    ILoggerFactory? loggerFactory = null) : ObservableObject
{
    /// <summary>The project-scoped interaction identity in the editor database.</summary>
    internal static readonly SettingKey<ProjectInteraction> Key = new("WorldEditor", "SceneInteraction");

    private readonly ILogger logger = loggerFactory?.CreateLogger<WorkspaceInteractionService>()
        ?? NullLogger<WorkspaceInteractionService>.Instance;

    // Serializes database access across project switches, exactly like the preview preferences.
    private Task persistence = Task.CompletedTask;
    private PendingWrite? pendingWrite;

    private ProjectContext? activeProject;
    private Guid activeSceneId;
    private HashSet<Guid> hiddenNodeIds = [];
    private HashSet<Guid> lockedNodeIds = [];
    private Dictionary<Guid, SceneInteraction> scenes = [];
    private long activationGeneration;

    /// <summary>Raised whenever the hidden or locked set for the active scene changes.</summary>
    public event EventHandler? StateChanged;

    /// <summary>Gets the category filters applied to Scene Explorer rows.</summary>
    public SceneCategories Categories { get; private set; } = SceneCategories.All;

    /// <summary>Gets the scene whose state this service currently presents.</summary>
    public Guid ActiveSceneId => this.activeSceneId;

    /// <summary>
    /// Loads the stored interaction state for a scene of <paramref name="project"/>, replacing any
    /// previously active scene.
    /// </summary>
    /// <param name="project">The project that owns the state.</param>
    /// <param name="sceneId">The scene whose state is being presented.</param>
    /// <returns>The restoration task.</returns>
    public Task RestoreAsync(ProjectContext project, Guid sceneId)
    {
        ArgumentNullException.ThrowIfNull(project);
        if (this.activeProject?.ProjectId != project.ProjectId
            || !string.Equals(this.activeProject?.ProjectRoot, project.ProjectRoot, StringComparison.OrdinalIgnoreCase))
        {
            this.scenes = [];
        }

        this.activeProject = project;
        this.activeSceneId = sceneId;
        this.hiddenNodeIds = [];
        this.lockedNodeIds = [];
        this.Categories = SceneCategories.All;
        var generation = ++this.activationGeneration;
        this.RaiseStateChanged();

        var restoration = this.RestoreAfterAsync(this.persistence, project, sceneId, generation);
        this.persistence = restoration.ContinueWith(
            static completed => { _ = completed.Exception; },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
        return restoration;
    }

    /// <summary>Forgets the active scene without discarding what was already persisted.</summary>
    public void ClearActiveScene()
    {
        this.activationGeneration++;
        this.activeSceneId = Guid.Empty;
        this.hiddenNodeIds = [];
        this.lockedNodeIds = [];
        this.Categories = SceneCategories.All;
        this.RaiseStateChanged();
    }

    /// <summary>Gets whether <paramref name="nodeId"/> is hidden in the editing viewports.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <returns><see langword="true"/> when the node itself is in the hidden set.</returns>
    /// <remarks>
    /// Descendant closure is <em>not</em> applied here: only the scene graph knows real ancestry, so
    /// the Explorer computes the effective set (see the plan's R1) and this service stays a store of
    /// explicitly stated entries.
    /// </remarks>
    public bool IsHidden(Guid nodeId) => this.hiddenNodeIds.Contains(nodeId);

    /// <summary>Gets whether <paramref name="nodeId"/> is locked against editing.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <returns><see langword="true"/> when the node is locked.</returns>
    public bool IsLocked(Guid nodeId) => this.lockedNodeIds.Contains(nodeId);

    /// <summary>Finds the closest locked node that protects <paramref name="node"/> from editing.</summary>
    /// <param name="node">The selected scene node.</param>
    /// <returns>The node itself or an ancestor that is locked; otherwise, <see langword="null"/>.</returns>
    public SceneNode? GetLockOwner(SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);
        if (node.Scene.Id != this.activeSceneId)
        {
            return null;
        }

        for (var current = (SceneNode?)node; current is not null; current = current.Parent)
        {
            if (this.IsLocked(current.Id))
            {
                return current;
            }
        }

        return null;
    }

    /// <summary>Gets a snapshot of the explicitly hidden node ids of the active scene.</summary>
    /// <returns>The hidden ids in no particular order.</returns>
    public IReadOnlyCollection<Guid> HiddenNodeIds() => [.. this.hiddenNodeIds];

    /// <summary>Hides or shows <paramref name="nodeId"/> in the editing viewports.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <param name="isHidden">The requested state.</param>
    public void SetHidden(Guid nodeId, bool isHidden) => this.Mutate(
        nodeId,
        isHidden,
        this.hiddenNodeIds,
        "node was hidden in the editor",
        "node is shown in the editor again");

    /// <summary>Locks or unlocks <paramref name="nodeId"/> against editing.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <param name="isLocked">The requested state.</param>
    public void SetLocked(Guid nodeId, bool isLocked) => this.Mutate(
        nodeId,
        isLocked,
        this.lockedNodeIds,
        "node was locked",
        "node was unlocked");

    /// <summary>Clears every hidden entry of the active scene, leaving locked entries alone.</summary>
    public void ShowAll()
    {
        if (this.hiddenNodeIds.Count == 0)
        {
            return;
        }

        this.hiddenNodeIds = [];
        this.RaiseStateChanged();
        this.ScheduleSave();
    }

    /// <summary>Clears every locked entry of the active scene, leaving hidden entries alone.</summary>
    public void UnlockAll()
    {
        if (this.lockedNodeIds.Count == 0)
        {
            return;
        }

        this.lockedNodeIds = [];
        this.RaiseStateChanged();
        this.ScheduleSave();
    }

    /// <summary>Applies the Scene Explorer category filters.</summary>
    /// <param name="categories">The category visibility preferences.</param>
    public void SetCategories(SceneCategories categories)
    {
        if (categories == this.Categories)
        {
            return;
        }

        this.Categories = categories;
        this.RaiseStateChanged();
        this.ScheduleSave();
    }

    // The project scope id is the normalized project root, the same identity the preview
    // preferences use; the stored ProjectId inside the value is what rejects a reused path.
    private static SettingContext Context(ProjectContext project)
        => SettingContext.Project(Path.TrimEndingDirectorySeparator(Path.GetFullPath(project.ProjectRoot)).ToUpperInvariant());

    private void Mutate(Guid nodeId, bool requested, HashSet<Guid> target, string appliedMessage, string reversedMessage)
    {
        if (nodeId == Guid.Empty)
        {
            return;
        }

        var changed = requested ? target.Add(nodeId) : target.Remove(nodeId);
        if (!changed)
        {
            return;
        }

        this.LogStateChange(nodeId, requested ? appliedMessage : reversedMessage);
        this.RaiseStateChanged();
        this.ScheduleSave();
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "Scene Explorer workspace state: {NodeId} {Change}.")]
    private partial void LogStateChange(Guid nodeId, string change);

    private void RaiseStateChanged() => this.StateChanged?.Invoke(this, EventArgs.Empty);

    private void ScheduleSave()
    {
        var project = this.activeProject;
        if (project is null)
        {
            return;
        }

        if (this.activeSceneId == Guid.Empty)
        {
            return;
        }

        this.scenes[this.activeSceneId] = new SceneInteraction(
            [.. this.hiddenNodeIds], [.. this.lockedNodeIds], this.Categories);
        this.pendingWrite = new PendingWrite(
            project,
            new ProjectInteraction(project.ProjectId, new Dictionary<Guid, SceneInteraction>(this.scenes)));

        // Chain onto the serialized queue so a fast sequence of toggles cannot interleave writes.
        this.persistence = this.SavePendingAfterAsync(this.persistence);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Workspace state persistence must report storage failures without losing later writes or crashing the Explorer bindings.")]
    private async Task SavePendingAfterAsync(Task previous)
    {
        await previous.ConfigureAwait(true);
        while (this.pendingWrite is { } write)
        {
            this.pendingWrite = null;
            try
            {
                await settings.SaveSettingAsync(Key, write.Value, Context(write.Project)).ConfigureAwait(true);
            }
            catch (Exception failure)
            {
                this.ReportFailure(
                    write.Project,
                    "SAVE_FAILED",
                    "Editor hide and lock state was not saved",
                    "Applied for this session, but could not save. Toggle again to retry.",
                    failure);
            }
        }
    }

    private async Task RestoreAfterAsync(Task previous, ProjectContext project, Guid sceneId, long generation)
    {
        await previous.ConfigureAwait(true);
        if (generation != this.activationGeneration || !ReferenceEquals(this.activeProject, project) || this.activeSceneId != sceneId)
        {
            // A newer scene took over while this load was in flight; its state must not be replaced.
            return;
        }

        ProjectInteraction? stored;
        try
        {
            stored = await settings.LoadSettingAsync(Key, Context(project)).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.ReportFailure(project, "LOAD_FAILED", "Editor hide and lock state was not restored", "The scene starts with nothing hidden or locked.", failure);
            return;
        }

        if (generation != this.activationGeneration || !ReferenceEquals(this.activeProject, project) || this.activeSceneId != sceneId)
        {
            return;
        }

        // A record written for another project is ignored outright: the settings scope is reused by
        // path, and a copied project must not inherit a stranger's hidden set.
        if (stored is null || stored.ProjectId != project.ProjectId)
        {
            return;
        }

        if (stored.Scenes is null
            || stored.Scenes.Any(static entry => entry.Value is null
                || entry.Value.HiddenNodeIds is null || entry.Value.LockedNodeIds is null || entry.Value.Categories is null))
        {
            this.ReportFailure(
                project,
                "INVALID_STATE",
                "Editor interaction state was not restored",
                "The saved workspace state is invalid. Scene visibility, locks and Explorer category filters use their defaults.",
                new InvalidDataException("Workspace interaction state contains an invalid scene record."));
            return;
        }

        this.scenes = new Dictionary<Guid, SceneInteraction>(stored.Scenes);
        if (this.scenes.TryGetValue(sceneId, out var scene))
        {
            this.hiddenNodeIds = [.. scene.HiddenNodeIds];
            this.lockedNodeIds = [.. scene.LockedNodeIds];
            this.Categories = scene.Categories;
            this.RaiseStateChanged();
        }
    }

    private void ReportFailure(ProjectContext project, string code, string title, string message, Exception exception)
    {
        this.LogInteractionFailure(exception, code, project.ProjectRoot);
        RuntimeOperationResults.PublishFailure(
            results,
            status,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            DiagnosticCodes.SettingsPrefix + code,
            title,
            message,
            new AffectedScope { ProjectId = project.ProjectId, ProjectName = project.Name, ProjectPath = project.ProjectRoot },
            exception: exception);
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Workspace interaction state failed ({Code}) for project {ProjectRoot}.")]
    private partial void LogInteractionFailure(Exception exception, string code, string projectRoot);

    private readonly record struct PendingWrite(ProjectContext Project, ProjectInteraction Value);

    /// <summary>The saved interaction state, guarded by project identity.</summary>
    /// <param name="ProjectId">The project owning this state.</param>
    /// <param name="Scenes">The interaction state keyed by stable scene identity.</param>
    internal sealed record ProjectInteraction(
        Guid ProjectId,
        IReadOnlyDictionary<Guid, SceneInteraction> Scenes);

    /// <summary>The saved interaction state for one scene.</summary>
    /// <param name="HiddenNodeIds">Nodes hidden in the editing viewports.</param>
    /// <param name="LockedNodeIds">Nodes locked against editing.</param>
    /// <param name="Categories">The Scene Explorer category filter.</param>
    internal sealed record SceneInteraction(
        IReadOnlyList<Guid> HiddenNodeIds,
        IReadOnlyList<Guid> LockedNodeIds,
        SceneCategories Categories);
}

/// <summary>The Scene Explorer node categories to display.</summary>
/// <param name="ShowMeshes">Whether mesh-category rows are displayed.</param>
/// <param name="ShowLights">Whether light-category rows are displayed.</param>
/// <param name="ShowCameras">Whether camera-category rows are displayed.</param>
public sealed record SceneCategories(bool ShowMeshes, bool ShowLights, bool ShowCameras)
{
    /// <summary>Gets the filter that includes every category.</summary>
    public static SceneCategories All { get; } = new(true, true, true);
}
