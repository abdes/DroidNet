// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Interaction;

/// <summary>
/// Owns the editor-only interaction state of a project's scenes: which nodes are hidden in the
/// editing viewports ("Show in Editor") and which rows are locked against editing.
/// </summary>
/// <remarks>
/// <para>
/// This is editor state, not authoring state. Changing it never writes
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
/// <para>
/// The service belongs to a project session and serves its one project, which it reads from the
/// session when it loads, saves or reports.
/// </para>
/// </remarks>
/// <param name="session">The project session whose scenes the state belongs to.</param>
/// <param name="settings">The editor settings manager, accessed serially by this service.</param>
/// <param name="results">The visible diagnostic publisher.</param>
/// <param name="status">The diagnostic status reducer.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class SceneNodeInteraction(
    IProjectSession session,
    IEditorSettingsManager settings,
    IOperationResultPublisher results,
    IStatusReducer status,
    ILoggerFactory? loggerFactory = null) : ISceneNodeInteraction
{
    /// <summary>The project-scoped interaction identity in the editor database.</summary>
    /// <remarks>The key keeps its original name because projects store it.</remarks>
    internal static readonly SettingKey<ProjectInteraction> Key = new("WorldEditor", "SceneInteraction");

    private readonly ILogger logger = loggerFactory?.CreateLogger<SceneNodeInteraction>()
        ?? NullLogger<SceneNodeInteraction>.Instance;

    // Loads and writes run one at a time, in request order.
    private Task persistence = Task.CompletedTask;
    private ProjectInteraction? pendingWrite;

    private SceneId activeSceneId;
    private HashSet<NodeId> hiddenNodeIds = [];
    private HashSet<NodeId> lockedNodeIds = [];
    private Dictionary<SceneId, SceneInteraction> scenes = [];
    private long activationGeneration;

    /// <summary>Raised whenever the hidden or locked set for the active scene changes.</summary>
    public event EventHandler? StateChanged;

    /// <summary>Gets the category filters applied to Scene Explorer rows.</summary>
    public SceneCategories Categories { get; private set; } = SceneCategories.All;

    /// <summary>Gets the scene whose state this service currently presents.</summary>
    public SceneId ActiveSceneId => this.activeSceneId;

    /// <summary>
    /// Loads the stored interaction state for a scene of the session's project, replacing any
    /// previously active scene.
    /// </summary>
    /// <param name="sceneId">The scene whose state is being presented.</param>
    /// <returns>The restoration task.</returns>
    public Task RestoreAsync(SceneId sceneId)
    {
        this.activeSceneId = sceneId;
        this.hiddenNodeIds = [];
        this.lockedNodeIds = [];
        this.Categories = SceneCategories.All;
        var generation = ++this.activationGeneration;
        this.RaiseStateChanged();

        var restoration = this.RestoreAfterAsync(this.persistence, sceneId, generation);
        this.persistence = restoration.ContinueWith(
            static completed => _ = completed.Exception,
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
        return restoration;
    }

    /// <summary>Forgets the active scene without discarding what was already persisted.</summary>
    public void ClearActiveScene()
    {
        this.activationGeneration++;
        this.activeSceneId = SceneId.Empty;
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
    public bool IsHidden(NodeId nodeId) => this.hiddenNodeIds.Contains(nodeId);

    /// <summary>Gets whether <paramref name="nodeId"/> is locked against editing.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <returns><see langword="true"/> when the node is locked.</returns>
    public bool IsLocked(NodeId nodeId) => this.lockedNodeIds.Contains(nodeId);

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

        for (var current = (SceneNode?)node; current is not null; current = current.Hierarchy.Parent)
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
    public IReadOnlyCollection<NodeId> HiddenNodeIds() => [.. this.hiddenNodeIds];

    /// <summary>Hides or shows <paramref name="nodeId"/> in the editing viewports.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <param name="isHidden">The requested state.</param>
    public void SetHidden(NodeId nodeId, bool isHidden) => this.Mutate(
        nodeId,
        isHidden,
        this.hiddenNodeIds,
        "node was hidden in the editor",
        "node is shown in the editor again");

    /// <summary>Locks or unlocks <paramref name="nodeId"/> against editing.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <param name="isLocked">The requested state.</param>
    public void SetLocked(NodeId nodeId, bool isLocked) => this.Mutate(
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

    private void Mutate(NodeId nodeId, bool requested, HashSet<NodeId> target, string appliedMessage, string reversedMessage)
    {
        if (nodeId.IsEmpty)
        {
            return;
        }

        var changed = requested ? target.Add(nodeId) : target.Remove(nodeId);
        if (!changed)
        {
            return;
        }

        this.LogStateChange(nodeId, requested, appliedMessage, reversedMessage);
        this.RaiseStateChanged();
        this.ScheduleSave();
    }

    private void RaiseStateChanged() => this.StateChanged?.Invoke(this, EventArgs.Empty);

    private bool IsActive(SceneId sceneId, long generation)
        => generation == this.activationGeneration && this.activeSceneId == sceneId;

    private void ScheduleSave()
    {
        if (this.activeSceneId.IsEmpty)
        {
            return;
        }

        this.scenes[this.activeSceneId] = new SceneInteraction(
            [.. this.hiddenNodeIds], [.. this.lockedNodeIds], this.Categories);
        this.pendingWrite = new ProjectInteraction(session.Context.ProjectId, new Dictionary<SceneId, SceneInteraction>(this.scenes));

        // Chain onto the serialized queue so a fast sequence of toggles cannot interleave writes.
        this.persistence = this.SavePendingAfterAsync(this.persistence);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Interaction state persistence must report storage failures without losing later writes or crashing the Explorer bindings.")]
    private async Task SavePendingAfterAsync(Task previous)
    {
        await previous.ConfigureAwait(true);
        while (this.pendingWrite is { } write)
        {
            this.pendingWrite = null;
            try
            {
                await settings.SaveSettingAsync(Key, write, session.Context.SettingsScope).ConfigureAwait(true);
            }
            catch (Exception failure)
            {
                this.ReportFailure(
                    "SAVE_FAILED",
                    "Editor hide and lock state was not saved",
                    "Applied for this session, but could not save. Toggle again to retry.",
                    failure);
            }
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A stored state that cannot be read, whatever the settings store fails with, is reported and the scene starts with nothing hidden or locked instead of failing to open.")]
    private async Task RestoreAfterAsync(Task previous, SceneId sceneId, long generation)
    {
        await previous.ConfigureAwait(true);
        if (!this.IsActive(sceneId, generation))
        {
            // A newer scene took over while this load was in flight; its state must not be replaced.
            return;
        }

        ProjectInteraction? stored;
        try
        {
            stored = await settings.LoadSettingAsync(Key, session.Context.SettingsScope).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.ReportFailure("LOAD_FAILED", "Editor hide and lock state was not restored", "The scene starts with nothing hidden or locked.", failure);
            return;
        }

        if (!this.IsActive(sceneId, generation))
        {
            return;
        }

        // A record written for another project is ignored outright: the settings scope is reused by
        // path, and a copied project must not inherit a stranger's hidden set.
        if (stored is null || stored.ProjectId != session.Context.ProjectId)
        {
            return;
        }

        if (stored.Scenes?.Any(static entry => entry.Value is null
                || entry.Value.HiddenNodeIds is null || entry.Value.LockedNodeIds is null || entry.Value.Categories is null) != false)
        {
            this.ReportFailure(
                "INVALID_STATE",
                "Editor interaction state was not restored",
                "The saved interaction state is invalid. Scene visibility, locks and Explorer category filters use their defaults.",
                new InvalidDataException("Interaction state contains an invalid scene record."));
            return;
        }

        this.scenes = [with(stored.Scenes)];
        if (this.scenes.TryGetValue(sceneId, out var scene))
        {
            this.hiddenNodeIds = [.. scene.HiddenNodeIds];
            this.lockedNodeIds = [.. scene.LockedNodeIds];
            this.Categories = scene.Categories;
            this.RaiseStateChanged();
        }
    }

    private void ReportFailure(string code, string title, string message, Exception exception)
    {
        var project = session.Context;
        this.LogInteractionFailure(exception, code, project.ProjectRoot);
        _ = SceneOperationResults.PublishFailure(
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

    /// <summary>The saved interaction state, guarded by project identity.</summary>
    /// <param name="ProjectId">The project owning this state.</param>
    /// <param name="Scenes">The interaction state keyed by stable scene identity.</param>
    internal sealed record ProjectInteraction(
        Guid ProjectId,
        IReadOnlyDictionary<SceneId, SceneInteraction> Scenes);

    /// <summary>The saved interaction state for one scene.</summary>
    /// <param name="HiddenNodeIds">Nodes hidden in the editing viewports.</param>
    /// <param name="LockedNodeIds">Nodes locked against editing.</param>
    /// <param name="Categories">The Scene Explorer category filter.</param>
    internal sealed record SceneInteraction(
        IReadOnlyList<NodeId> HiddenNodeIds,
        IReadOnlyList<NodeId> LockedNodeIds,
        SceneCategories Categories);
}
