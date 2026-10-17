// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>The engine's preview of the open scene documents, as the scene's views read it.</summary>
/// <remarks>
/// The preview follows the scene documents in the workspace's open documents: the engine shows the scene document opened
/// last, follows its changes and reloads, and retires it when it closes. Views only read the preview; none of them
/// registers or synchronizes anything. Every event is raised on the document's owner thread.
/// </remarks>
public interface IScenePreview
{
    /// <summary>Occurs when a scene document becomes ready in the engine, or stops being ready.</summary>
    public event EventHandler<ScenePreviewEventArgs>? ReadyChanged;

    /// <summary>Occurs when the number of a scene document's fields still waiting for the engine changes.</summary>
    public event EventHandler<ScenePreviewEventArgs>? PendingFieldsChanged;

    /// <summary>Gets a value indicating whether the engine accepted the full projection of a document's current content.</summary>
    /// <param name="document">The scene document.</param>
    /// <returns><see langword="true"/> when the engine shows the document's current content.</returns>
    public bool IsReady(DocumentId document);

    /// <summary>Gets the number of a scene document's fields still waiting for the engine.</summary>
    /// <param name="document">The scene document.</param>
    /// <returns>The number of fields, zero when none waits or the document is not open.</returns>
    public int PendingFieldCount(DocumentId document);

    /// <summary>Reads the engine's last rendered sky-light state for a scene document.</summary>
    /// <param name="document">The scene document, which must be the one the engine shows.</param>
    /// <param name="cancellationToken">Cancels the observation.</param>
    /// <returns>The observed state, or <see langword="null"/> when the engine does not show the document.</returns>
    public Task<SkyLightObservation?> ObserveSkyLightAsync(DocumentId document, CancellationToken cancellationToken = default);
}
