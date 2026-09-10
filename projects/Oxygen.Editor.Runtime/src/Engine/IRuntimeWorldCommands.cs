// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed scene projection operations; implementations own native conversion and lifetime checks.</summary>
public interface IRuntimeWorldCommands
{
    /// <summary>Gets the current run identity, or empty when unavailable.</summary>
    public Guid RunId { get; }

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
}
