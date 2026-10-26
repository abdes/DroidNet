// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.TestSupport;

/// <summary>
/// The engine's world commands, recording every request: each one is accepted unless a test decides otherwise, and a
/// test raises the world's asset events.
/// </summary>
internal sealed class FakeWorld : IRuntimeWorldCommands
{
    /// <inheritdoc/>
    public event EventHandler<RuntimeAssetLoadFailedEventArgs>? AssetLoadFailed;

    /// <inheritdoc/>
    public event EventHandler? AssetStatusChanged;

    /// <inheritdoc/>
    public Guid RunId { get; set; } = Guid.NewGuid();

    /// <inheritdoc/>
    public IReadOnlyList<RuntimeAssetRequestStatus> AssetRequests { get; set; } = [];

    /// <summary>Gets every request executed or used to create a node, in order.</summary>
    public List<RuntimeWorldRequest> Requests { get; } = [];

    /// <summary>Gets the targets of every scene activation, in order.</summary>
    public List<RuntimeSceneTarget> Activations { get; } = [];

    /// <summary>Gets the targets of every scene invalidation, in order.</summary>
    public List<RuntimeSceneTarget> Invalidations { get; } = [];

    /// <summary>Gets the commands of every request, in order.</summary>
    public IEnumerable<RuntimeWorldCommand> Commands => this.Requests.Select(static request => request.Command);

    /// <summary>Gets or sets the outcome of an executed request; null accepts it.</summary>
    public Func<RuntimeWorldRequest, RuntimeCommandResult?>? OnExecute { get; set; }

    /// <summary>Gets or sets the outcome of a node creation; null accepts it at once.</summary>
    public Func<RuntimeWorldRequest, Task<RuntimeCommandResult>?>? OnCreateNode { get; set; }

    /// <summary>Gets or sets what an invalidation does besides recording its target.</summary>
    public Action<RuntimeSceneTarget>? OnInvalidateScene { get; set; }

    /// <summary>Gets or sets whether an asset failure is current.</summary>
    public Func<RuntimeAssetLoadFailedEventArgs, bool> IsCurrentFailure { get; set; } = static _ => true;

    /// <summary>Gets or sets the environment the engine reports.</summary>
    public RuntimeEnvironmentObservation? Environment { get; set; }

    /// <summary>Accepts a request.</summary>
    /// <param name="request">The request.</param>
    /// <returns>The accepted result.</returns>
    public static RuntimeCommandResult Accept(RuntimeWorldRequest request)
        => new(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted);

    /// <summary>Gives a request an outcome.</summary>
    /// <param name="request">The request.</param>
    /// <param name="status">The outcome.</param>
    /// <param name="message">The outcome's message.</param>
    /// <returns>The result.</returns>
    public static RuntimeCommandResult Result(RuntimeWorldRequest request, RuntimeCommandStatus status, string? message = null)
        => new(request.OperationId, request.Target.RunId, status, message);

    /// <summary>Gets the property values sent to one component of a node, in order.</summary>
    /// <param name="node">The node.</param>
    /// <param name="component">The engine component.</param>
    /// <returns>The values.</returns>
    public IEnumerable<RuntimePropertyValue> PropertiesOf(NodeId node, RuntimeComponentId component)
        => this.Commands.OfType<RuntimeSetProperties>()
            .Where(command => command.NodeId == node)
            .SelectMany(static command => command.Entries)
            .Where(entry => entry.ComponentId == component);

    /// <summary>Raises an asset load failure.</summary>
    /// <param name="failure">The failure.</param>
    public void RaiseAssetLoadFailed(RuntimeAssetLoadFailedEventArgs failure) => this.AssetLoadFailed?.Invoke(this, failure);

    /// <summary>Raises an asset status change.</summary>
    public void RaiseAssetStatusChanged() => this.AssetStatusChanged?.Invoke(this, EventArgs.Empty);

    /// <inheritdoc/>
    public bool IsCurrentAssetFailure(RuntimeAssetLoadFailedEventArgs failure) => this.IsCurrentFailure(failure);

    /// <inheritdoc/>
    public void InvalidateScene(RuntimeSceneTarget target)
    {
        this.Invalidations.Add(target);
        this.OnInvalidateScene?.Invoke(target);
    }

    /// <inheritdoc/>
    public bool IsCurrentAssetRequest(RuntimeWorldRequest request) => true;

    /// <inheritdoc/>
    public Task<RuntimeCommandResult> ActivateSceneAsync(Guid operationId, RuntimeSceneTarget target, string name, CancellationToken cancellationToken = default)
    {
        this.Activations.Add(target);
        return Task.FromResult(new RuntimeCommandResult(operationId, target.RunId, RuntimeCommandStatus.Accepted));
    }

    /// <inheritdoc/>
    public RuntimeCommandResult Execute(RuntimeWorldRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        this.Requests.Add(request);
        return this.OnExecute?.Invoke(request) ?? Accept(request);
    }

    /// <inheritdoc/>
    public Task<RuntimeCommandResult> CreateNodeAsync(RuntimeWorldRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        this.Requests.Add(request);
        return this.OnCreateNode?.Invoke(request) ?? Task.FromResult(Accept(request));
    }

    /// <inheritdoc/>
    public Task<RuntimeEnvironmentObservation> ObserveEnvironmentAsync(Guid operationId, RuntimeSceneTarget target, CancellationToken cancellationToken = default)
        => this.Environment is { } environment
            ? Task.FromResult(environment)
            : throw new InvalidOperationException("The test gave the world no environment to observe.");

    /// <inheritdoc/>
    public Task<RuntimeNodeObservation> ObserveNodeAsync(Guid operationId, RuntimeSceneTarget target, NodeId nodeId, CancellationToken cancellationToken = default)
        => throw new NotSupportedException("Scene preview tests do not observe nodes.");

    /// <inheritdoc/>
    public Task<RuntimeSceneHierarchyObservation> ReadSceneHierarchyAsync(Guid operationId, RuntimeSceneTarget target, string virtualPath, CancellationToken cancellationToken = default)
        => throw new NotSupportedException("Scene preview tests do not read scene hierarchies.");

    /// <inheritdoc/>
    public Task<RuntimeSceneNodeObservation> ReadSceneNodeAsync(Guid operationId, RuntimeSceneTarget target, string virtualPath, NodePath path, CancellationToken cancellationToken = default)
        => throw new NotSupportedException("Scene preview tests do not read scene nodes.");
}
