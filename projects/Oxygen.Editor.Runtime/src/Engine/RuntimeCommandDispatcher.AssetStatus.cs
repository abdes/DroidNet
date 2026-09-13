// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Keeps current native outcomes with their owning scene, node, slot and generation.</summary>
internal sealed partial class RuntimeCommandDispatcher
{
    /// <inheritdoc />
    public event EventHandler? AssetStatusChanged;

    /// <inheritdoc />
    public IReadOnlyList<RuntimeAssetRequestStatus> AssetRequests
    {
        get
        {
            lock (this.gate)
            {
                return this.IsRunning && this.sceneReady ? this.assetOperations.Values.ToArray() : [];
            }
        }
    }

    /// <inheritdoc />
    public bool IsCurrentAssetFailure(RuntimeAssetLoadFailedEventArgs failure)
    {
        lock (this.gate)
        {
            return this.IsCurrentAssetRequest(failure.Request) && AssetTarget(failure.Request.Command) is { } target
                && this.assetOperations[target] is { Succeeded: false } state && state.Generation == failure.Generation;
        }
    }

    private void OnAssetLoadSucceeded(object? sender, RuntimeAssetLoadSucceededEventArgs args)
        => this.RecordAssetOutcome(args.Request, args.Generation, succeeded: true, reason: null);

    private bool RecordAssetOutcome(RuntimeWorldRequest request, ulong generation, bool succeeded, string? reason)
    {
        lock (this.gate)
        {
            if (!this.IsCurrentAssetRequest(request) || AssetTarget(request.Command) is not { } target
                || this.assetOperations[target].Generation > generation
                || (generation != 0 && this.assetOperations[target] is { Succeeded: not null } completed && completed.Generation == generation))
            {
                return false;
            }

            this.assetOperations[target] = new(request, generation, succeeded, reason);
        }

        this.NotifyAssetStatusChanged();
        return true;
    }

    private void NotifyAssetStatusChanged()
        => _ = Task.Run(this.PublishAssetStatusChanged, CancellationToken.None);

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A status observer cannot interrupt native dispatch or suppress other observers.")]
    private void PublishAssetStatusChanged()
    {
        if (this.AssetStatusChanged is not { } handlers)
        {
            return;
        }

        foreach (EventHandler handler in handlers.GetInvocationList())
        {
            try
            {
                handler(this, EventArgs.Empty);
            }
            catch (Exception exception)
            {
                System.Diagnostics.Trace.TraceError("Runtime asset status observer failed: {0}", exception.Message);
            }
        }
    }
}
