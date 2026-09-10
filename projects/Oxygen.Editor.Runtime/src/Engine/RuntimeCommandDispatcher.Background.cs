// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Validates native background observations against run and scene lifetime.</summary>
internal sealed partial class RuntimeCommandDispatcher
{
    /// <inheritdoc/>
    public async Task<RuntimeBackgroundObservation> ObserveBackgroundAsync(
        Guid operationId, RuntimeSceneTarget target, CancellationToken cancellationToken = default)
    {
        Task<RuntimeBackgroundState> observation;
        Task runEnded;
        lock (this.gate)
        {
            var rejection = this.Check(operationId, target, cancellationToken);
            if (rejection is not null)
            {
                return BackgroundResult(rejection, target);
            }

            try
            {
                observation = this.transport!.ObserveBackgroundAsync();
                runEnded = Task.WhenAny(this.loop!, this.ended.Task, this.sceneEnded.Task);
            }
            catch (Exception exception) when (IsRecoverable(exception))
            {
                return BackgroundResult(Failure(operationId, target.RunId, exception), target);
            }
        }

        try
        {
            var completed = await Task.WhenAny(observation, runEnded).WaitAsync(cancellationToken).ConfigureAwait(false);
            if (completed != observation)
            {
                lock (this.gate)
                {
                    var interrupted = this.Check(operationId, target, cancellationToken)
                        ?? new(operationId, target.RunId, RuntimeCommandStatus.Rejected, "The scene activation was invalidated before observation completed.");
                    return BackgroundResult(interrupted, target);
                }
            }

            var state = await observation.ConfigureAwait(false);
            lock (this.gate)
            {
                var outcome = this.Check(operationId, target, cancellationToken) ?? Accepted(operationId, target.RunId);
                return BackgroundResult(outcome, target, outcome.Succeeded ? state : null);
            }
        }
        catch (Exception exception) when (IsRecoverable(exception))
        {
            return BackgroundResult(Failure(operationId, target.RunId, exception), target);
        }
    }

    private static RuntimeBackgroundObservation BackgroundResult(
        RuntimeCommandResult outcome, RuntimeSceneTarget target, RuntimeBackgroundState? state = null)
        => new(outcome with { SceneTarget = target }, state);
}
