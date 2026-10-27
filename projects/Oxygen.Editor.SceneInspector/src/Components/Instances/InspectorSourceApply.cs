// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>
/// <i>Apply to source</i> from the Properties pane (scene instances 6.7, 8.5): the authored scenes a node's changes can go
/// to, the commands a changed row, a component and a node offer, and running them, with the result told to the user.
/// </summary>
/// <param name="session">The project session, whose scene files the apply reads.</param>
/// <param name="authoring">The authoring of the scene being inspected.</param>
/// <param name="operationResults">What tells the user why an apply did nothing, or which changes stayed.</param>
/// <param name="statusReducer">The reducer paired with <paramref name="operationResults"/>.</param>
internal sealed class InspectorSourceApply(
    IProjectSession session,
    Func<SceneAuthoring?> authoring,
    IOperationResultPublisher? operationResults = null,
    IStatusReducer? statusReducer = null)
{
    /// <summary>Lists the authored scenes a node's path passes through, which its changes can be applied to.</summary>
    /// <param name="node">The instance root for the whole instance, or a node it brings in.</param>
    /// <returns>The scenes, outermost first; none for a model instance.</returns>
    public async Task<IReadOnlyList<ApplyTarget>> TargetsAsync(NodeReference node)
    {
        ArgumentNullException.ThrowIfNull(node);
        return authoring() is { } current
            ? await new SourceApplication(session.Context).TargetsAsync(current.Scene, node, session.Lifetime).ConfigureAwait(true)
            : [];
    }

    /// <summary>Gets whether an instance makes any of some changes.</summary>
    /// <param name="node">The instance root, or a node it brings in.</param>
    /// <param name="selection">The changes.</param>
    /// <returns>Whether the instance root holds any of them.</returns>
    public bool HasChanges(NodeReference node, ChangeSelection selection)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(selection);
        return authoring()?.Scene.AllNodes.FirstOrDefault(candidate => candidate.Id == node.Node)?.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } component
            && component.Changes.Nodes.Any(entry => !selection.Select(entry.Key, entry.Value).IsEmpty);
    }

    /// <summary>Gets the command that applies some changes of an instance to an authored scene.</summary>
    /// <param name="instance">The instance root.</param>
    /// <param name="selection">The changes.</param>
    /// <param name="target">The scene.</param>
    /// <returns>The command, labelled such as <c>Apply to Watchtower</c>.</returns>
    public OverrideAction ActionOf(NodeId instance, ChangeSelection selection, ApplyTarget target)
        => new(target.Label, new AsyncRelayCommand(() => this.ApplyAsync(instance, selection, target)));

    /// <summary>Gets the commands that apply some changes of an instance to each authored scene they can go to.</summary>
    /// <param name="node">The instance root for the whole instance, or a node it brings in.</param>
    /// <param name="selection">The changes.</param>
    /// <returns>One command per scene, such as <c>Apply to Watchtower</c>; none when the instance makes none of the changes.</returns>
    public async Task<IReadOnlyList<OverrideAction>> ActionsAsync(NodeReference node, ChangeSelection selection)
        => this.HasChanges(node, selection)
            ? [.. (await this.TargetsAsync(node).ConfigureAwait(true)).Select(target => this.ActionOf(node.Node, selection, target))]
            : [];

    /// <summary>Applies changes of an instance to an authored scene, and tells the user when it applied nothing or kept some.</summary>
    /// <param name="instance">The instance root.</param>
    /// <param name="selection">The changes.</param>
    /// <param name="target">The scene.</param>
    /// <returns>The edit's result, or <see langword="null"/> when no scene is inspected.</returns>
    public async Task<SceneEditResult?> ApplyAsync(NodeId instance, ChangeSelection selection, ApplyTarget target)
    {
        if (authoring() is not { } current)
        {
            return null;
        }

        var result = await new SourceApplication(session.Context).ExecuteAsync(current, instance, selection, target, session.Lifetime).ConfigureAwait(true);
        if (operationResults is { } publisher && statusReducer is { } reducer)
        {
            SceneOperationResults.PublishEditResult(publisher, reducer, SceneOperationKinds.EditInstance, "Changes not applied", "Changes kept on the instance", result, new AffectedScope { SceneId = current.Scene.Id });
        }

        return result;
    }
}
