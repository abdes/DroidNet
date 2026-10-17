// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.Scenes.Changes;

/// <summary>The changes one edit, replay, preview or relocation made to a scene, in the order it made them.</summary>
/// <param name="document">The scene's document.</param>
/// <param name="changes">The changes.</param>
public sealed class SceneChangedEventArgs(DocumentId document, IReadOnlyList<SceneChange> changes) : EventArgs
{
    /// <summary>Gets the scene's document.</summary>
    public DocumentId Document { get; } = document;

    /// <summary>Gets the changes, in the order they were made.</summary>
    public IReadOnlyList<SceneChange> Changes { get; } = changes;
}
