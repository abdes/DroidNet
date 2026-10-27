// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Inspection;
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
/// Entries key on the node's reference, so renaming, regrouping or reordering a node keeps its
/// state, and a node an instance brought in is hidden or locked by its instance root and node path;
/// an entry the instance's source no longer holds is dropped when its structure is read
/// (<see cref="DropUnresolvedNodes"/>). The whole set for a project is one typed
/// setting (<see cref="Key"/>) holding per-scene subrecords: one atomic write survives a restart, and a scene that is no longer in the
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
/// <param name="structures">What the instances of the shown scene contain, which tells the ancestors of their nodes.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class SceneNodeInteraction(
    IProjectSession session,
    IEditorSettingsManager settings,
    IOperationResultPublisher results,
    IStatusReducer status,
    ISceneStructures structures,
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
    private HashSet<NodeReference> hiddenNodes = [];
    private HashSet<NodeReference> lockedNodes = [];
    private Dictionary<SceneId, SceneInteraction> scenes = [];
    private long activationGeneration;

    /// <summary>Raised whenever the hidden or locked set for the active scene changes.</summary>
    public event EventHandler? StateChanged;

    /// <summary>Gets the category filters applied to Scene Explorer rows.</summary>
    public SceneCategories Categories { get; private set; } = SceneCategories.All;

    /// <summary>Gets the scene whose state this service currently presents.</summary>
    public SceneId ActiveSceneId => this.activeSceneId;

    /// <summary>Finds what protects a node from editing, by a lock test and its instance's contents.</summary>
    /// <param name="scene">The scene the node is in.</param>
    /// <param name="node">The node, which an instance may have brought in.</param>
    /// <param name="isLocked">Whether a node itself is locked.</param>
    /// <param name="structureOf">The contents read of a source, or <see langword="null"/> when they are not read.</param>
    /// <returns>
    /// The closest locked node among the node, its ancestors in its instance's contents (each nesting level's included),
    /// its instance root and the root's ancestors in the scene; for a node an instance brings in that its instance's
    /// contents do not hold yet, that instance; otherwise, <see cref="NodeLock.None"/>.
    /// </returns>
    public static NodeLock FindLock(Scene scene, NodeReference node, Func<NodeReference, bool> isLocked, Func<AssetPath, SceneStructure?> structureOf)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(isLocked);
        ArgumentNullException.ThrowIfNull(structureOf);
        if (NodeAncestry.Of(scene, node, structureOf) is not { } ancestors)
        {
            return NodeLock.Unread(node.Node);
        }

        return isLocked(node) ? NodeLock.By(node)
            : ancestors.FirstOrDefault(isLocked) is { } owner ? NodeLock.By(owner)
            : NodeLock.None;
    }

    /// <summary>Moves each entry of a set of nodes that the map names another node for to that node.</summary>
    /// <param name="nodes">The hidden or locked nodes.</param>
    /// <param name="map">The node a reference names now, or <see langword="null"/> when it names the same node.</param>
    /// <returns>The nodes the moved entries name now.</returns>
    public static IReadOnlyList<NodeReference> Retarget(ISet<NodeReference> nodes, Func<NodeReference, NodeReference?> map)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(map);
        var moved = nodes.Select(node => (From: node, To: map(node))).Where(static entry => entry.To is not null && entry.To != entry.From).ToList();
        nodes.ExceptWith(moved.Select(static entry => entry.From));
        NodeReference[] targets = [.. moved.Select(static entry => entry.To!)];
        nodes.UnionWith(targets);
        return targets;
    }

    /// <summary>
    /// Loads the stored interaction state for a scene of the session's project, replacing any
    /// previously active scene.
    /// </summary>
    /// <param name="sceneId">The scene whose state is being presented.</param>
    /// <returns>The restoration task.</returns>
    public Task RestoreAsync(SceneId sceneId)
    {
        this.activeSceneId = sceneId;
        this.hiddenNodes = [];
        this.lockedNodes = [];
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
        this.hiddenNodes = [];
        this.lockedNodes = [];
        this.Categories = SceneCategories.All;
        this.RaiseStateChanged();
    }

    /// <summary>Gets whether <paramref name="node"/> is hidden in the editing viewports.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <returns><see langword="true"/> when the node itself is in the hidden set.</returns>
    /// <remarks>
    /// Descendant closure is <em>not</em> applied here: only the scene graph knows real ancestry, so
    /// the Explorer computes the effective set (see the plan's R1) and this service stays a store of
    /// explicitly stated entries.
    /// </remarks>
    public bool IsHidden(NodeReference node) => this.hiddenNodes.Contains(node);

    /// <summary>Gets whether <paramref name="node"/> is locked against editing.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <returns><see langword="true"/> when the node itself is locked.</returns>
    public bool IsLocked(NodeReference node) => this.lockedNodes.Contains(node);

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
            if (this.IsLocked(NodeReference.To(current.Id)))
            {
                return current;
            }
        }

        return null;
    }

    /// <inheritdoc/>
    public IReadOnlyList<NodeReference>? GetAncestors(Scene scene, NodeReference node)
    {
        ArgumentNullException.ThrowIfNull(scene);
        return NodeAncestry.Of(scene, node, source => structures.Find(session.Context, source));
    }

    /// <inheritdoc/>
    /// <remarks>A scene other than the active one has no lock state here, so nothing protects its nodes.</remarks>
    public NodeLock GetLock(Scene scene, NodeReference node)
    {
        ArgumentNullException.ThrowIfNull(scene);
        return scene.Id == this.activeSceneId
            ? FindLock(scene, node, this.IsLocked, source => structures.Find(session.Context, source))
            : NodeLock.None;
    }

    /// <summary>Gets a snapshot of the explicitly hidden nodes of the active scene.</summary>
    /// <returns>The hidden nodes in no particular order.</returns>
    public IReadOnlyCollection<NodeReference> HiddenNodes() => [.. this.hiddenNodes];

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeReference> LockedNodes() => [.. this.lockedNodes];

    /// <summary>Hides or shows <paramref name="node"/> in the editing viewports.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <param name="isHidden">The requested state.</param>
    public void SetHidden(NodeReference node, bool isHidden)
    {
        ArgumentNullException.ThrowIfNull(node);
        if (!node.Node.IsEmpty)
        {
            this.Mutate(node, isHidden, this.hiddenNodes, "node was hidden in the editor", "node is shown in the editor again");
        }
    }

    /// <summary>Locks or unlocks <paramref name="node"/> against editing.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <param name="isLocked">The requested state.</param>
    public void SetLocked(NodeReference node, bool isLocked)
    {
        ArgumentNullException.ThrowIfNull(node);
        if (!node.Node.IsEmpty)
        {
            this.Mutate(node, isLocked, this.lockedNodes, "node was locked", "node was unlocked");
        }
    }

    /// <inheritdoc/>
    public IReadOnlyList<NodeReference> Retarget(Func<NodeReference, NodeReference?> map)
    {
        ArgumentNullException.ThrowIfNull(map);
        var hidden = Retarget(this.hiddenNodes, map);
        if (hidden.Count + Retarget(this.lockedNodes, map).Count != 0)
        {
            this.RaiseStateChanged();
            this.ScheduleSave();
        }

        return hidden;
    }

    /// <summary>
    /// Drops the hidden and locked entries of an instance's nodes that its source no longer holds, as after a swap, a
    /// reimport or the removal of a kept change. Called whenever the instance's structure is read.
    /// </summary>
    /// <param name="instanceRoot">The instance root of the active scene.</param>
    /// <param name="sourceHolds">Whether the structure the source has now holds a node path, as <c>SceneStructure.Find</c> answers.</param>
    public void DropUnresolvedNodes(NodeId instanceRoot, Func<NodePath, bool> sourceHolds)
    {
        ArgumentNullException.ThrowIfNull(sourceHolds);
        bool Unresolved(NodeReference node) => node.Node == instanceRoot && !node.IsOwned && !sourceHolds(node.Path);
        if (this.lockedNodes.RemoveWhere(Unresolved) + this.hiddenNodes.RemoveWhere(Unresolved) == 0)
        {
            return;
        }

        this.RaiseStateChanged();
        this.ScheduleSave();
    }

    /// <summary>Clears every hidden entry of the active scene, leaving locked entries alone.</summary>
    public void ShowAll()
    {
        if (this.hiddenNodes.Count == 0)
        {
            return;
        }

        this.hiddenNodes = [];
        this.RaiseStateChanged();
        this.ScheduleSave();
    }

    /// <summary>Clears every locked entry of the active scene, leaving hidden entries alone.</summary>
    public void UnlockAll()
    {
        if (this.lockedNodes.Count == 0)
        {
            return;
        }

        this.lockedNodes = [];
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

    private void Mutate<TNode>(TNode node, bool requested, HashSet<TNode> target, string appliedMessage, string reversedMessage)
        where TNode : notnull
    {
        var changed = requested ? target.Add(node) : target.Remove(node);
        if (!changed)
        {
            return;
        }

        this.LogStateChange(node.ToString()!, requested, appliedMessage, reversedMessage);
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
            [.. this.hiddenNodes], [.. this.lockedNodes], this.Categories);
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
                await settings.SaveSettingAsync(Key, write, session.Context.SettingsScope, ct: session.Lifetime).ConfigureAwait(true);
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
            stored = await settings.LoadSettingAsync(Key, session.Context.SettingsScope, ct: session.Lifetime).ConfigureAwait(true);
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
            this.hiddenNodes = [.. scene.HiddenNodeIds];
            this.lockedNodes = [.. scene.LockedNodeIds];
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
    /// <param name="HiddenNodeIds">
    /// Nodes hidden in the editing viewports, by reference. A node of the scene's reference is its ID, so a record
    /// stored before instanced nodes could be hidden reads unchanged.
    /// </param>
    /// <param name="LockedNodeIds">
    /// Nodes locked against editing, by reference. A node of the scene's reference is its ID, so a record stored
    /// before instanced nodes could be locked reads unchanged.
    /// </param>
    /// <param name="Categories">The Scene Explorer category filter.</param>
    internal sealed record SceneInteraction(
        IReadOnlyList<NodeReference> HiddenNodeIds,
        IReadOnlyList<NodeReference> LockedNodeIds,
        SceneCategories Categories);
}
