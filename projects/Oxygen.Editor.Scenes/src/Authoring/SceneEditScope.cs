// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>What an edit may use while it validates and applies: the scene, its explorer layout and the ports of the scene's authoring.</summary>
public sealed class SceneEditScope
{
    private readonly List<SceneDiagnostic> warnings = [];

    /// <summary>Initializes a new instance of the <see cref="SceneEditScope"/> class.</summary>
    /// <param name="scene">The scene the edit validates against and applies to.</param>
    /// <param name="layout">The scene's explorer layout, which the edit keeps in step with the scene.</param>
    /// <param name="interaction">The nodes hidden and locked in the editor, if the editor keeps them.</param>
    /// <param name="redirects">The session's asset renames and moves, if the editor tracks them.</param>
    /// <param name="references">The saved authored references, if the editor reads them.</param>
    internal SceneEditScope(Scene scene, IList<ExplorerEntryData> layout, ISceneNodeInteraction? interaction, IAssetRedirects? redirects, IAuthoredReferences? references = null)
    {
        this.Scene = scene;
        this.Layout = layout;
        this.Interaction = interaction;
        this.Redirects = redirects;
        this.References = references;
    }

    /// <summary>Gets the scene.</summary>
    public Scene Scene { get; }

    /// <summary>Gets the scene's explorer layout: an entry for every node, and the folders that group them.</summary>
    public IList<ExplorerEntryData> Layout { get; }

    /// <summary>Gets the nodes hidden and locked in the editor, or <see langword="null"/> when nothing keeps them.</summary>
    public ISceneNodeInteraction? Interaction { get; }

    /// <summary>
    /// Gets the session's asset renames and moves, which retarget references an edit captured earlier, or
    /// <see langword="null"/> when nothing tracks them.
    /// </summary>
    public IAssetRedirects? Redirects { get; }

    /// <summary>
    /// Gets the direct references of the project's saved authored assets, which an instance's source must not reach the
    /// scene through, or <see langword="null"/> when nothing reads them.
    /// </summary>
    public IAuthoredReferences? References { get; }

    /// <summary>
    /// Gets what an instance of a source contains (scene instances 8.2), as last read, or <see langword="null"/> when
    /// nothing reads it; the lookup answers <see langword="null"/> for a source not read.
    /// </summary>
    public Func<AssetPath, SceneStructure?>? StructureOf { get; init; }

    /// <summary>Gets the scene's own asset, which no instance in it may reach, or <see langword="null"/> when unknown.</summary>
    public AssetPath? Asset { get; internal set; }

    /// <summary>Gets or sets a value indicating whether the scope runs a history replay, which checks no lock.</summary>
    internal bool Replaying { get; set; }

    /// <summary>Finds a node of the scene.</summary>
    /// <param name="id">The node's id.</param>
    /// <returns>The node, or <see langword="null"/> when the scene has none with that id.</returns>
    public SceneNode? FindNode(NodeId id) => this.Scene.AllNodes.FirstOrDefault(node => node.Id == id);

    /// <summary>Resolves a reference captured before an asset moved to the asset's current identity.</summary>
    /// <param name="reference">The captured reference.</param>
    /// <returns>The current reference; the input when the asset did not move or nothing tracks moves.</returns>
    [return: System.Diagnostics.CodeAnalysis.NotNullIfNotNull(nameof(reference))]
    public AssetPath? Resolve(AssetPath? reference) => reference is null || this.Redirects is null ? reference : this.Redirects.Resolve(reference);

    /// <summary>Resolves a captured reference, in the form the retargeting helpers take.</summary>
    /// <param name="reference">The captured reference.</param>
    /// <returns>The current reference, or <see langword="null"/> when it did not move.</returns>
    public AssetPath? Moved(AssetPath reference)
    {
        var resolved = this.Resolve(reference);
        return resolved is null || resolved == reference ? null : resolved;
    }

    /// <summary>Warns about references the edit restores to assets this session deleted; they show as missing.</summary>
    /// <param name="restored">The references the edit wrote.</param>
    /// <param name="node">The node that holds them, or <see langword="null"/> for the scene's own references.</param>
    public void WarnIfDeleted(IEnumerable<AssetPath?> restored, NodeId? node = null)
    {
        if (this.Redirects is not { } redirects)
        {
            return;
        }

        foreach (var uri in restored.OfType<AssetPath>().Where(redirects.WasDeleted).Distinct())
        {
            this.warnings.Add(SceneDiagnostic.Warning(
                SceneEditCodes.AssetReferenceDeleted,
                $"{Path.GetFileName(uri.VirtualPath)} was deleted, so the restored reference shows as missing until you choose another asset.",
                node));
        }
    }

    /// <summary>Reports what the user should know about the edit, such as a reference to a deleted asset.</summary>
    /// <param name="diagnostics">The warnings.</param>
    public void Warn(IEnumerable<SceneDiagnostic> diagnostics) => this.warnings.AddRange(diagnostics);

    /// <summary>Checks that each of an edit's targets exists, and that neither it nor one of its ancestors is locked.</summary>
    /// <param name="edit">The edit.</param>
    /// <returns>The validation; refused when a target is missing or locked.</returns>
    public SceneEditValidation CheckTargets(ISceneEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        List<SceneDiagnostic>? errors = null;
        foreach (var target in edit.Targets)
        {
            if (this.CheckTarget(target) is { } error)
            {
                (errors ??= []).Add(error);
            }
        }

        foreach (var target in edit.InstancedTargets)
        {
            if (this.CheckTarget(target) is { } error)
            {
                (errors ??= []).Add(error);
            }
        }

        return errors is null ? SceneEditValidation.Valid : SceneEditValidation.Refused(errors);
    }

    /// <summary>Checks the node whose children a layout place groups, as the frame checks an edit's targets.</summary>
    /// <remarks>Layout edits call it: their targets depend on where the entries are, which only the scene tells.</remarks>
    /// <param name="node">The scope's node, or <see langword="null"/> for the layout's root.</param>
    /// <returns>The validation; refused when the node is missing or locked.</returns>
    public SceneEditValidation CheckScope(NodeId? node)
        => node is { } id && this.CheckTarget(id) is { } error ? SceneEditValidation.Refused(error) : SceneEditValidation.Valid;

    /// <summary>Checks that no node under the given roots is locked, for edits that remove or move whole subtrees.</summary>
    /// <remarks>
    /// The frame checks the targets and their ancestors; an edit that changes a subtree also changes the nodes under its
    /// root, the nodes the subtree's instances bring in included. A replay skips the check, as the frame skips its lock
    /// check.
    /// </remarks>
    /// <param name="roots">The subtrees' roots; missing ids are skipped.</param>
    /// <returns>The validation; refused when a descendant is locked, naming it.</returns>
    public SceneEditValidation CheckSubtrees(IEnumerable<NodeId> roots)
    {
        ArgumentNullException.ThrowIfNull(roots);
        if (this.Interaction is not { } interaction || this.Replaying)
        {
            return SceneEditValidation.Valid;
        }

        List<SceneDiagnostic>? errors = null;
        var lockedInstanced = interaction.LockedNodes().Where(static node => !node.IsOwned).ToList();
        foreach (var root in roots.Select(this.FindNode).OfType<SceneNode>())
        {
            foreach (var locked in root.Descendants().Where(node => interaction.IsLocked(NodeReference.To(node.Id))))
            {
                (errors ??= []).Add(SceneDiagnostic.Error(SceneEditCodes.TargetLocked, $"'{locked.Metadata.Name}' is locked.", locked.Id));
            }

            foreach (var instance in (IEnumerable<SceneNode>)[root, .. root.Descendants()])
            {
                if (lockedInstanced.Any(node => node.Node == instance.Id))
                {
                    (errors ??= []).Add(SceneDiagnostic.Error(SceneEditCodes.TargetLocked, $"A node of '{instance.Metadata.Name}' is locked.", instance.Id));
                }
            }
        }

        return errors is null ? SceneEditValidation.Valid : SceneEditValidation.Refused(errors);
    }

    /// <summary>
    /// Checks an instance's source against the scene: a source that is the scene, or references it through the saved
    /// authored references, would make a cycle (<see cref="InstanceRules.WouldCycle"/>).
    /// </summary>
    /// <param name="source">The instance's source.</param>
    /// <param name="node">The instance root, if it is in the scene.</param>
    /// <returns>The refusal, or <see langword="null"/> when the source may be instanced here or the scene's asset is unknown.</returns>
    public SceneDiagnostic? CheckInstanceSource(AssetPath source, NodeId? node = null)
    {
        ArgumentNullException.ThrowIfNull(source);
        return this.Asset is { } scene && InstanceRules.WouldCycle(scene, source, asset => this.References?.Of(asset) ?? [])
            ? SceneDiagnostic.Error(SceneEditCodes.InstanceCycle, $"'{source.Name}' is or instances this scene, which cannot instance itself.", node)
            : null;
    }

    /// <summary>Creates a scope over another scene, with the same ports, such as a scratch copy to validate a batch on.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <returns>The scope.</returns>
    internal SceneEditScope On(Scene scene, IList<ExplorerEntryData> layout)
        => new(scene, layout, this.Interaction is { } interaction ? new ScratchNodeInteraction(interaction) : null, this.Redirects, this.References)
        {
            Asset = this.Asset,
            Replaying = this.Replaying,
            StructureOf = this.StructureOf,
        };

    /// <summary>Takes the warnings reported since the last call.</summary>
    /// <returns>The warnings.</returns>
    internal IReadOnlyList<SceneDiagnostic> TakeWarnings()
    {
        SceneDiagnostic[] taken = [.. this.warnings];
        this.warnings.Clear();
        return taken;
    }

    private SceneDiagnostic? CheckTarget(NodeId target)
    {
        if (this.FindNode(target) is not { } node)
        {
            return SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The node no longer exists.", target);
        }

        // A replay returns the scene to a state the user already made, so locks do not hold it back.
        if (this.Interaction is not { } interaction || this.Replaying)
        {
            return null;
        }

        for (var current = node; current is not null; current = current.Hierarchy.Parent)
        {
            if (interaction.IsLocked(NodeReference.To(current.Id)))
            {
                return SceneDiagnostic.Error(
                    SceneEditCodes.TargetLocked,
                    current == node ? $"'{node.Metadata.Name}' is locked." : $"'{node.Metadata.Name}' is locked by '{current.Metadata.Name}'.",
                    target);
            }
        }

        return null;
    }

    // A node an instance brings in: its instance root must be in the scene, and the closest lock that protects it, in its
    // instance's contents or above its root, refuses the edit, as do contents not read yet.
    private SceneDiagnostic? CheckTarget(NodeReference target)
    {
        if (this.FindNode(target.Node) is not { } root)
        {
            return SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The instance the node belongs to no longer exists.", target.Node);
        }

        if (this.Interaction is not { } interaction || this.Replaying)
        {
            return null;
        }

        var nodeLock = interaction.GetLock(this.Scene, target);
        if (nodeLock.UnreadInstance is not null)
        {
            return SceneDiagnostic.Error(
                SceneEditCodes.InstanceUnavailable,
                $"The contents of '{root.Metadata.Name}' are not available yet, so its nodes cannot be edited. Try again in a moment.",
                target.Node);
        }

        if (nodeLock.Owner is not { } owner)
        {
            return null;
        }

        var name = target.IsOwned ? $"'{root.Metadata.Name}'" : $"A node of '{root.Metadata.Name}'";
        var message = owner == target ? $"{name} is locked."
            : owner.IsOwned && this.FindNode(owner.Node) is { } locked ? $"{name} is locked by '{locked.Metadata.Name}'."
            : $"{name} is locked by a node of '{root.Metadata.Name}' above it.";
        return SceneDiagnostic.Error(SceneEditCodes.TargetLocked, message, target.Node);
    }
}
