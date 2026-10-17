// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The domain rules of one component kind: what its properties write, what they couple, what they allow.</summary>
/// <remarks>
/// Per-field validation stays on each descriptor. The rules hold what spans fields or nodes: the properties whose state
/// must be captured with an edited one, the validation of the candidate state an edit would produce, and the edits
/// that adding the component brings with it.
/// </remarks>
public interface IComponentRules
{
    /// <summary>Gets the component kind, as property ids name it.</summary>
    public string Kind { get; }

    /// <summary>Gets the kind's property descriptors, by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors { get; }

    /// <summary>Gets a value indicating whether the kind's properties belong to the scene rather than to a node.</summary>
    public bool IsSceneKind => false;

    /// <summary>Gets the object the descriptors read and write.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node, or <see langword="null"/> for a scene kind.</param>
    /// <returns>The object, or <see langword="null"/> when the node has no component of this kind.</returns>
    public object? TargetOf(Scene scene, SceneNode? node);

    /// <summary>Makes what the descriptors wrote to a target part of the scene.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="target">A target from <see cref="TargetOf"/>, after the descriptors wrote to it.</param>
    /// <remarks>Components are written in place, so the default does nothing.</remarks>
    public void Commit(Scene scene, object target)
    {
    }

    /// <summary>Gets the properties whose state an edit of the given ones can change.</summary>
    /// <param name="edited">The edited properties.</param>
    /// <returns>The edited properties and those coupled to them.</returns>
    public IReadOnlySet<PropertyId> Coupled(IEnumerable<PropertyId> edited) => edited.ToHashSet();

    /// <summary>Validates the candidate state the edits would produce, across fields and nodes.</summary>
    /// <param name="scene">The scene as it is.</param>
    /// <param name="edits">The edits, each with its node, or without one for a scene kind.</param>
    /// <returns>Why the edits are invalid, or <see langword="null"/> when they are valid.</returns>
    public SceneDiagnostic? Validate(Scene scene, IReadOnlyList<PropertyTargetEdit> edits) => null;

    /// <summary>Gets the edits that adding a component of this kind to a node brings with it.</summary>
    /// <param name="node">The node, which already has the component.</param>
    /// <returns>The edits; empty when adding the component has no consequence.</returns>
    public IReadOnlyList<ISceneEdit> OnAdd(SceneNode node) => [];
}
