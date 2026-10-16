// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
namespace Oxygen.Editor.World.Services;

/// <summary>Identifies an accepted scene projection and its open-document owner.</summary>
/// <param name="scene">The authoring scene that was projected.</param>
/// <param name="documentId">The identity of the document that owns the scene.</param>
public sealed class SceneSynchronizationCompletedEventArgs(Scene scene, DocumentId documentId) : EventArgs
{
    /// <summary>Gets the authoring scene.</summary>
    public Scene Scene { get; } = scene;

    /// <summary>Gets the identity of the document that owns the scene.</summary>
    public DocumentId DocumentId { get; } = documentId;
}
