// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Engine;

/// <summary>The engine's view of the open scenes: it syncs each registered scene and then follows its changes.</summary>
/// <remarks>
/// The engine projector consumes each scene's <see cref="SceneAuthoring.Changed"/> stream; nothing in Scenes calls the
/// engine directly.
/// </remarks>
public interface ISceneEngineSync
{
    /// <summary>
    ///     Occurs when the number of buffered property sync requests changes for a scene.
    /// </summary>
    public event EventHandler<PendingPropertySyncCountChangedEventArgs>? PendingPropertySyncCountChanged;

    /// <summary>Occurs after a current scene snapshot and its later pending work are accepted.</summary>
    /// <remarks>Delivered on the configured UI dispatcher; acceptance does not prove presentation.</remarks>
    public event EventHandler<SceneSynchronizationCompletedEventArgs>? SceneSynchronized;

    /// <summary>Registers the scene of an open document, replacing the scene the document showed before.</summary>
    /// <remarks>
    /// Call it on the scene thread. From then on the engine follows the scene's changes: every change raised after the
    /// call is projected after the next full sync, and none raised before it, since the sync's snapshot holds those.
    /// </remarks>
    /// <param name="authoring">The authoring of the loaded scene.</param>
    /// <returns>False if the document closed before its asynchronous load completed.</returns>
    public bool RegisterDocument(SceneAuthoring authoring);

    /// <summary>Gets authoring data already loaded for a document, independently of runtime readiness.</summary>
    /// <param name="documentId">The identity of the scene's document.</param>
    /// <returns>The loaded scene, or null if it is still loading or has closed.</returns>
    public Scene? GetDocumentScene(DocumentId documentId);

    /// <summary>Gets a value indicating whether a scene's current snapshot is already synchronized into the engine.</summary>
    /// <param name="scene">The registered authoring scene.</param>
    /// <returns>
    ///     <see langword="true"/> when <see cref="SceneSynchronized"/> was already raised for the scene's current
    ///     projection, so a consumer created afterwards can act on it without waiting for another notification.
    /// </returns>
    public bool IsSceneSynchronized(Scene scene);

    /// <summary>Invalidates queued work and runtime delivery for one document.</summary>
    /// <param name="documentId">The identity of the closing document.</param>
    public void CloseDocument(DocumentId documentId);

    /// <summary>
    ///     Synchronizes an entire scene with the engine, creating all nodes and establishing the hierarchy.
    /// </summary>
    /// <param name="scene">The scene to synchronize.</param>
    /// <param name="cancellationToken">Cancellation token to abort stale scene synchronization.</param>
    /// <returns><see langword="true"/> when the scene was synchronized into the engine; otherwise <see langword="false"/>.</returns>
    public Task<bool> SyncSceneAsync(Scene scene, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Synchronizes an entire scene with the engine once the engine is running.
    /// </summary>
    /// <param name="scene">The scene to synchronize.</param>
    /// <param name="cancellationToken">
    ///     Cancellation token to abort waiting (e.g., when a newer scene load supersedes this one).
    /// </param>
    /// <returns><see langword="true"/> when the scene was synchronized into the engine; otherwise <see langword="false"/>.</returns>
    public Task<bool> SyncSceneWhenReadyAsync(Scene scene, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Gets the number of buffered property sync requests waiting for the next full scene sync.
    /// </summary>
    /// <param name="sceneId">The scene id.</param>
    /// <returns>The pending property sync request count.</returns>
    public int GetPendingPropertySyncCount(SceneId sceneId);

    /// <summary>Reads the engine's last rendered sky-light state.</summary>
    /// <param name="scene">The authoring scene, which must be the one the runtime shows.</param>
    /// <param name="cancellationToken">Cancels the observation.</param>
    /// <returns>The observed state, or <see langword="null"/> when the runtime does not show this scene.</returns>
    public Task<SceneSkyLightObservation?> ObserveSkyLightAsync(Scene scene, CancellationToken cancellationToken = default);
}
