// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Serializes dispatch with lifetime invalidation; never blocks on native frame progress.</summary>
internal sealed partial class RuntimeCommandDispatcher : IRuntimeWorldCommands, IRuntimeInputCommands
{
    private readonly Lock gate = new();
    private readonly Dictionary<ulong, RuntimeViewTarget> views = [];
    private readonly Dictionary<(Guid nodeId, int slot), Guid> assetOperations = [];
    private IRuntimeCommandTransport? transport;
    private Task? loop;
    private TaskCompletionSource ended = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private RuntimeSceneTarget? scene;
    private Guid runId;
    private bool sceneReady;

    /// <inheritdoc/>
    public event EventHandler<RuntimeAssetLoadFailedEventArgs>? AssetLoadFailed;

    /// <inheritdoc/>
    public Guid RunId
    {
        get
        {
            lock (this.gate)
            {
                return this.IsRunning ? this.runId : Guid.Empty;
            }
        }
    }

    private bool IsRunning => this.transport is not null && this.loop is { IsCompleted: false };

    /// <summary>Installs one native transport and its run identity.</summary>
    /// <param name="commandTransport">The native adapter.</param>
    /// <param name="loopTask">The runtime lifetime task.</param>
    public void BeginRun(IRuntimeCommandTransport commandTransport, Task loopTask)
    {
        lock (this.gate)
        {
            this.EndRun();
            this.runId = Guid.NewGuid();
            this.transport = commandTransport;
            this.transport.AssetLoadFailed += this.OnAssetLoadFailed;
            this.loop = loopTask;
            this.ended = new(TaskCreationOptions.RunContinuationsAsynchronously);
        }
    }

    /// <summary>Invalidates dispatch before native ownership is destroyed.</summary>
    public void EndRun()
    {
        lock (this.gate)
        {
            if (this.transport is { } transport)
            {
                transport.AssetLoadFailed -= this.OnAssetLoadFailed;
            }

            this.transport = null;
            this.scene = null;
            this.sceneReady = false;
            this.views.Clear();
            this.assetOperations.Clear();
            _ = this.ended.TrySetResult();
        }
    }

    /// <summary>Registers a fresh view generation.</summary>
    /// <param name="viewId">The native view identifier.</param>
    /// <param name="documentId">The owning document.</param>
    /// <param name="viewportId">The owning viewport.</param>
    public void RegisterView(ulong viewId, Guid documentId, Guid viewportId)
    {
        lock (this.gate)
        {
            if (this.IsRunning)
            {
                this.views[viewId] = new(this.runId, documentId, viewportId, Guid.NewGuid(), viewId);
            }
        }
    }

    /// <summary>Invalidates a view before destruction.</summary>
    /// <param name="viewId">The native view identifier.</param>
    public void UnregisterView(ulong viewId)
    {
        lock (this.gate)
        {
            _ = this.views.Remove(viewId);
        }
    }

    /// <inheritdoc/>
    public RuntimeViewTarget? GetViewTarget(ulong viewId)
    {
        lock (this.gate)
        {
            return this.IsRunning && this.views.TryGetValue(viewId, out var target) ? target : null;
        }
    }

    /// <inheritdoc/>
    public RuntimeCommandResult Execute(RuntimeWorldRequest request, CancellationToken cancellationToken = default)
        => this.ExecuteWorldCore(request, cancellationToken) with { SceneTarget = request.Target };

    /// <inheritdoc/>
    public RuntimeCommandResult Execute(RuntimeInputRequest request, CancellationToken cancellationToken = default)
        => this.ExecuteInputCore(request, cancellationToken) with { ViewTarget = request.Target };

    /// <inheritdoc/>
    public async Task<RuntimeCommandResult> ActivateSceneAsync(Guid operationId, RuntimeSceneTarget target, string name, CancellationToken cancellationToken = default)
        => (await this.ActivateSceneCoreAsync(operationId, target, name, cancellationToken).ConfigureAwait(false)) with { SceneTarget = target };

    /// <inheritdoc/>
    public async Task<RuntimeCommandResult> CreateNodeAsync(RuntimeWorldRequest request, CancellationToken cancellationToken = default)
        => (await this.CreateNodeCoreAsync(request, cancellationToken).ConfigureAwait(false)) with { SceneTarget = request.Target };

    /// <inheritdoc/>
    public bool IsCurrentAssetRequest(RuntimeWorldRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        lock (this.gate)
        {
            return this.IsRunning && this.sceneReady && this.scene == request.Target
                && AssetTarget(request.Command) is { } target
                && this.assetOperations.TryGetValue(target, out var current)
                && current == request.OperationId;
        }
    }

    private static (Guid nodeId, int slot)? AssetTarget(RuntimeWorldCommand command)
        => command switch
        {
            RuntimeSetGeometry geometry => (geometry.NodeId, -1),
            RuntimeSetMaterialOverride material when material.SlotIndex >= 0 => (material.NodeId, material.SlotIndex),
            _ => null,
        };

    private static RuntimeCommandResult Accepted(Guid operationId, Guid runId)
        => new(operationId, runId, RuntimeCommandStatus.Accepted);

    private static bool IsRecoverable(Exception exception)
        => exception is ArgumentException or InvalidOperationException or NotSupportedException
            or NotImplementedException or TimeoutException or OperationCanceledException
            or System.Runtime.InteropServices.COMException;

    private static RuntimeCommandResult Failure(Guid operationId, Guid runId, Exception exception)
        => new(operationId, runId, Classify(exception), exception.Message, exception);

    private static RuntimeCommandStatus Classify(Exception exception)
        => exception switch
        {
            OperationCanceledException => RuntimeCommandStatus.Cancelled,
            ArgumentException or InvalidOperationException => RuntimeCommandStatus.Rejected,
            NotSupportedException or NotImplementedException => RuntimeCommandStatus.Unavailable,
            _ => RuntimeCommandStatus.Failed,
        };

    private RuntimeCommandResult ExecuteWorldCore(RuntimeWorldRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        lock (this.gate)
        {
            var rejection = this.Check(request.OperationId, request.Target, cancellationToken);
            if (rejection is not null)
            {
                return rejection;
            }

            try
            {
                this.TrackAssetOperation(request);
                this.transport!.Execute(request);
                return Accepted(request.OperationId, request.Target.RunId);
            }
            catch (Exception exception) when (IsRecoverable(exception))
            {
                return Failure(request.OperationId, request.Target.RunId, exception);
            }
        }
    }

    private RuntimeCommandResult ExecuteInputCore(RuntimeInputRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        lock (this.gate)
        {
            var rejection = this.CheckRun(request.OperationId, request.Target.RunId, cancellationToken);
            if (rejection is not null)
            {
                return rejection;
            }

            if (!this.views.TryGetValue(request.Target.ViewId, out var current) || current != request.Target)
            {
                return new(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Rejected, "The viewport generation is no longer current.");
            }

            try
            {
                this.transport!.ExecuteInput(request.Target.ViewId, request.Input);
                return Accepted(request.OperationId, request.Target.RunId);
            }
            catch (Exception exception) when (IsRecoverable(exception))
            {
                return Failure(request.OperationId, request.Target.RunId, exception);
            }
        }
    }

    private async Task<RuntimeCommandResult> ActivateSceneCoreAsync(
        Guid operationId, RuntimeSceneTarget target, string name, CancellationToken cancellationToken)
    {
        Task<bool> creation;
        Task runEnded;
        lock (this.gate)
        {
            var rejection = this.CheckRun(operationId, target.RunId, cancellationToken);
            if (rejection is not null)
            {
                return rejection;
            }

            if (target.SceneId == Guid.Empty || target.DocumentLifetime == Guid.Empty || target.ActivationId == Guid.Empty)
            {
                return new(operationId, target.RunId, RuntimeCommandStatus.Rejected, "A scene activation requires scene, document and activation identities.");
            }

            this.scene = target;
            this.sceneReady = false;
            this.assetOperations.Clear();
            try
            {
                creation = this.transport!.ActivateSceneAsync(name);
                runEnded = Task.WhenAny(this.loop!, this.ended.Task);
            }
            catch (Exception exception) when (IsRecoverable(exception))
            {
                return Failure(operationId, target.RunId, exception);
            }
        }

        try
        {
            var completed = await Task.WhenAny(creation, runEnded).WaitAsync(cancellationToken).ConfigureAwait(false);
            if (completed != creation)
            {
                return new(operationId, target.RunId, RuntimeCommandStatus.Unavailable, "The runtime ended before scene creation completed.");
            }

            var created = await creation.ConfigureAwait(false);
            lock (this.gate)
            {
                var rejection = this.CheckRun(operationId, target.RunId, cancellationToken);
                if (rejection is not null)
                {
                    return rejection;
                }

                if (this.scene != target)
                {
                    return new(operationId, target.RunId, RuntimeCommandStatus.Rejected, "Scene creation was superseded by another activation.");
                }

                this.sceneReady = created;
                return created ? Accepted(operationId, target.RunId) : new(operationId, target.RunId, RuntimeCommandStatus.Rejected, "Native scene creation was rejected.");
            }
        }
        catch (Exception exception) when (IsRecoverable(exception))
        {
            return Failure(operationId, target.RunId, exception);
        }
    }

    private async Task<RuntimeCommandResult> CreateNodeCoreAsync(RuntimeWorldRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        Task creation;
        Task runEnded;
        lock (this.gate)
        {
            var rejection = this.Check(request.OperationId, request.Target, cancellationToken);
            if (rejection is not null)
            {
                return rejection;
            }

            if (request.Command is not RuntimeCreateNode command)
            {
                return new(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Rejected, "Expected a node-creation command.");
            }

            try
            {
                creation = this.transport!.CreateNodeAsync(command);
                runEnded = Task.WhenAny(this.loop!, this.ended.Task);
            }
            catch (Exception exception) when (IsRecoverable(exception))
            {
                return Failure(request.OperationId, request.Target.RunId, exception);
            }
        }

        try
        {
            var completed = await Task.WhenAny(creation, runEnded).WaitAsync(cancellationToken).ConfigureAwait(false);
            if (completed != creation)
            {
                return new(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Unavailable, "The runtime ended before node creation completed.");
            }

            await creation.ConfigureAwait(false);
            lock (this.gate)
            {
                return this.Check(request.OperationId, request.Target, cancellationToken) ?? Accepted(request.OperationId, request.Target.RunId);
            }
        }
        catch (Exception exception) when (IsRecoverable(exception))
        {
            return Failure(request.OperationId, request.Target.RunId, exception);
        }
    }

    private RuntimeCommandResult? Check(Guid operationId, RuntimeSceneTarget target, CancellationToken cancellationToken)
        => this.CheckRun(operationId, target.RunId, cancellationToken)
            ?? (this.scene != target ? new(operationId, target.RunId, RuntimeCommandStatus.Rejected, "The scene activation is no longer current.")
                : !this.sceneReady ? new(operationId, target.RunId, RuntimeCommandStatus.Unavailable, "The scene is not ready.") : null);

    private void TrackAssetOperation(RuntimeWorldRequest request)
    {
        // This only correlates delivery after a UI hop. Native SceneAssetRequests
        // remains the sole authority that generates and accepts load generations.
        if (AssetTarget(request.Command) is { } target)
        {
            this.assetOperations[target] = request.OperationId;
        }
        else if (request.Command is RuntimeDetachGeometry detach)
        {
            this.ForgetAssetOperations(detach.NodeId);
        }
        else if (request.Command is RuntimeRemoveSceneNode remove)
        {
            this.ForgetAssetOperations(remove.NodeId);
        }
        else if (request.Command is RuntimeRemoveSceneNodes removeMany)
        {
            foreach (var nodeId in removeMany.Nodes)
            {
                this.ForgetAssetOperations(nodeId);
            }
        }
    }

    private void ForgetAssetOperations(Guid nodeId)
    {
        foreach (var target in this.assetOperations.Keys.Where(value => value.nodeId == nodeId).ToArray())
        {
            _ = this.assetOperations.Remove(target);
        }
    }

    private void OnAssetLoadFailed(object? sender, RuntimeAssetLoadFailedEventArgs args)
    {
        if (this.IsCurrentAssetRequest(args.Request))
        {
            this.AssetLoadFailed?.Invoke(this, args);
        }
    }

    private RuntimeCommandResult? CheckRun(Guid operationId, Guid requestedRun, CancellationToken cancellationToken)
        => cancellationToken.IsCancellationRequested
            ? new(operationId, requestedRun, RuntimeCommandStatus.Cancelled, "Runtime dispatch was cancelled.")
            : !this.IsRunning
                ? new(operationId, requestedRun, RuntimeCommandStatus.Unavailable, "The runtime is not running.")
                : requestedRun != this.runId
                    ? new(operationId, requestedRun, RuntimeCommandStatus.Rejected, "The runtime run is no longer current.") : null;
}
