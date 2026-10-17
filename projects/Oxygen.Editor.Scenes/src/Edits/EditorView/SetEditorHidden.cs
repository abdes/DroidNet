// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.EditorView;

/// <summary>Hides nodes in the editor viewports, or shows them again ("Show in Editor").</summary>
/// <remarks>
/// The state belongs to the workspace, not to the scene: the edit is undoable but does not change the document, so it
/// never makes it dirty, never touches the saved file and never asks for a cook. It never writes authored visibility.
/// Hiding is not locking, so a hidden node's lock does not refuse it.
/// </remarks>
public sealed record SetEditorHidden : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetEditorHidden"/> class.</summary>
    /// <param name="nodes">The nodes to hide or show.</param>
    /// <param name="hidden">Whether to hide the nodes; otherwise they are shown.</param>
    public SetEditorHidden(IReadOnlyList<NodeId> nodes, bool hidden)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        this.Nodes = [.. nodes.Where(static id => !id.IsEmpty).Distinct()];
        this.Hidden = hidden;
    }

    /// <summary>Gets the nodes to hide or show.</summary>
    public IReadOnlyList<NodeId> Nodes { get; }

    /// <summary>Gets a value indicating whether the nodes are hidden; otherwise they are shown.</summary>
    public bool Hidden { get; }

    /// <inheritdoc/>
    public string Label => string.Create(CultureInfo.InvariantCulture, $"{(this.Hidden ? "Hide" : "Show")} {this.Nodes.Count} node(s) in editor");

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    public bool ChangesDocument => false;

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return scope.Interaction is null
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.InteractionPortMissing, "No workspace keeps editor visibility, so nodes cannot be hidden or shown."))
            : this.Nodes.Count == 0
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No node to hide or show."))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var interaction = scope.Interaction!;

        // A node no longer in the scene is skipped: a stale id must not fail the whole selection.
        NodeId[] affected = [.. this.Nodes.Where(id => scope.FindNode(id) is not null && interaction.IsHidden(id) != this.Hidden)];
        foreach (var node in affected)
        {
            interaction.SetHidden(node, this.Hidden);
        }

        return affected.Length == 0
            ? new SceneEditApplication.Applied(this, [])
            : new SceneEditApplication.Applied(new SetEditorHidden(affected, !this.Hidden), [new SceneChange.EditorVisibilityChanged(affected)]);
    }
}
