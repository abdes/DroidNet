// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed scene projection operations; implementations own native conversion and lifetime checks.</summary>
public interface IRuntimeWorldCommands
{
    /// <summary>Occurs on the runtime thread when a current asynchronous asset request fails.</summary>
    public event EventHandler<RuntimeAssetLoadFailedEventArgs>? AssetLoadFailed;

    /// <summary>Gets the current run identity, or empty when unavailable.</summary>
    public Guid RunId { get; }

    /// <summary>Invalidates commands and notifications for the specified scene activation.</summary>
    /// <param name="target">The activation being closed or superseded.</param>
    public void InvalidateScene(RuntimeSceneTarget target);

    /// <summary>Checks request correlation again after a consumer marshals a failure to its UI thread.</summary>
    /// <param name="request">The original asset operation.</param>
    /// <returns>Whether this is still the current asset request for its live target.</returns>
    public bool IsCurrentAssetRequest(RuntimeWorldRequest request);

    /// <summary>Replaces the live scene and invalidates earlier projection targets.</summary>
    /// <param name="operationId">The request identity.</param>
    /// <param name="target">The new scene activation.</param>
    /// <param name="name">The scene name.</param>
    /// <param name="cancellationToken">Cancels the wait without claiming native rollback.</param>
    /// <returns>The native scene-creation outcome.</returns>
    public Task<RuntimeCommandResult> ActivateSceneAsync(Guid operationId, RuntimeSceneTarget target, string name, CancellationToken cancellationToken = default);

    /// <summary>Dispatches a synchronous enqueue operation against the current scene activation.</summary>
    /// <param name="request">The immutable request.</param>
    /// <param name="cancellationToken">Cancels before dispatch.</param>
    /// <returns>The enqueue outcome; accepted does not mean presented or asset-resolved.</returns>
    public RuntimeCommandResult Execute(RuntimeWorldRequest request, CancellationToken cancellationToken = default);

    /// <summary>Creates a node and waits asynchronously for its mutation-phase acknowledgment.</summary>
    /// <param name="request">A request containing a RuntimeCreateNode command.</param>
    /// <param name="cancellationToken">Cancels the wait.</param>
    /// <returns>The correlated acknowledgment, rejected if the activation is no longer current.</returns>
    public Task<RuntimeCommandResult> CreateNodeAsync(RuntimeWorldRequest request, CancellationToken cancellationToken = default);

    /// <summary>Observes stored background values without treating them as presented pixels.</summary>
    /// <param name="operationId">The observation identity.</param>
    /// <param name="target">The scene activation to observe.</param>
    /// <param name="cancellationToken">Cancels the observation wait.</param>
    /// <returns>The native state only if the requested activation remains current.</returns>
    public Task<RuntimeBackgroundObservation> ObserveBackgroundAsync(Guid operationId, RuntimeSceneTarget target, CancellationToken cancellationToken = default);

    /// <summary>Reads stored atmosphere and post-process values for the current scene activation.</summary>
    /// <param name="operationId">The observation identity.</param>
    /// <param name="target">The scene activation to observe.</param>
    /// <param name="cancellationToken">Cancels the observation wait.</param>
    /// <returns>The native state only if the requested activation remains current.</returns>
    public Task<RuntimeEnvironmentObservation> ObserveEnvironmentAsync(Guid operationId, RuntimeSceneTarget target, CancellationToken cancellationToken = default);

    /// <summary>Reads stored node properties and the currently resolved assets.</summary>
    /// <param name="operationId">The observation identity.</param>
    /// <param name="target">The scene activation to observe.</param>
    /// <param name="nodeId">The authored node identity.</param>
    /// <param name="cancellationToken">Cancels the observation wait.</param>
    /// <returns>The native state only if the requested activation remains current.</returns>
    public Task<RuntimeNodeObservation> ObserveNodeAsync(Guid operationId, RuntimeSceneTarget target, Guid nodeId, CancellationToken cancellationToken = default);
}
