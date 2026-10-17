// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>One change to a scene: a record of ids and values that validates against the scene and applies itself.</summary>
/// <remarks>
/// <para>
/// An edit holds only typed ids, primitives, URIs, vectors, enums, immutable property edits and serialization records it
/// owns; never a live model object. The scene's <see cref="SceneAuthoring"/> executes it and owns the thread, the
/// lifetime, the target checks, the history and the change notification.
/// </para>
/// <para>
/// <see cref="Apply"/> is called only after <see cref="Validate"/> passed on the current scene. It applies completely and
/// returns its exact inverse, captured while it applied, or leaves the scene as it found it and returns
/// <see cref="SceneEditApplication.Rejected"/>; only a composite rejects. An exception from it is a defect. An edit that
/// finds the scene already as it asks applies with no changes, and nothing is recorded.
/// </para>
/// </remarks>
public interface ISceneEdit
{
    /// <summary>Gets the edit's name in the history, as people read it.</summary>
    public string Label { get; }

    /// <summary>Gets the nodes that must exist and be unlocked for the edit to apply.</summary>
    public IReadOnlyCollection<NodeId> Targets { get; }

    /// <summary>
    /// Gets a value indicating whether the edit changes the document, as opposed to only how the editor shows it; an edit
    /// that does not keeps the history's position, so it never makes the document dirty.
    /// </summary>
    public bool ChangesDocument => true;

    /// <summary>Checks whether the edit fits the scene as it is.</summary>
    /// <param name="scope">The scene and the ports the edit may use.</param>
    /// <returns>The validation.</returns>
    public SceneEditValidation Validate(SceneEditScope scope);

    /// <summary>Applies the edit.</summary>
    /// <param name="scope">The scene and the ports the edit may use.</param>
    /// <returns>The inverse and the changes made, or why a composite applied nothing.</returns>
    public SceneEditApplication Apply(SceneEditScope scope);
}
