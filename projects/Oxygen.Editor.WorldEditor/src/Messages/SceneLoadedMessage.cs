// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Message indicating that a scene has been fully created/synchronized in the engine.
/// </summary>
/// <param name="scene">The <see cref="Scene"/> that was loaded into the engine.</param>
/// <param name="documentId">The identity of the document that edits the scene.</param>
internal sealed class SceneLoadedMessage(Scene scene, DocumentId documentId)
{
    /// <summary>
    /// Gets the loaded <see cref="Scene"/>.
    /// </summary>
    public Scene Scene { get; } = scene;

    /// <summary>Gets the identity of the document that edits the scene.</summary>
    public DocumentId DocumentId { get; } = documentId;
}
