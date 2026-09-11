// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Validates native observations against run and scene lifetime.</summary>
internal sealed partial class RuntimeCommandDispatcher
{
    /// <inheritdoc/>
    public async Task<RuntimeBackgroundObservation> ObserveBackgroundAsync(
        Guid operationId, RuntimeSceneTarget target, CancellationToken cancellationToken = default)
    {
        var (outcome, state) = await this.ObserveAsync(operationId, target, static transport => transport.ObserveBackgroundAsync(), cancellationToken).ConfigureAwait(false);
        return new(outcome, state);
    }

    /// <inheritdoc/>
    public async Task<RuntimeEnvironmentObservation> ObserveEnvironmentAsync(
        Guid operationId, RuntimeSceneTarget target, CancellationToken cancellationToken = default)
    {
        var (outcome, state) = await this.ObserveAsync(operationId, target, static transport => transport.ObserveEnvironmentAsync(), cancellationToken).ConfigureAwait(false);
        return new(outcome, state);
    }

    private async Task<(RuntimeCommandResult outcome, TState? state)> ObserveAsync<TState>(
        Guid operationId,
        RuntimeSceneTarget target,
        Func<IRuntimeCommandTransport, Task<TState>> read,
        CancellationToken cancellationToken)
        where TState : class
    {
        Task<TState> observation;
        Task runEnded;
        lock (this.gate)
        {
            var rejection = this.Check(operationId, target, cancellationToken);
            if (rejection is not null)
            {
                return Result(rejection);
            }

            try
            {
                observation = read(this.transport!);
                runEnded = Task.WhenAny(this.loop!, this.ended.Task, this.sceneEnded.Task);
            }
            catch (Exception exception) when (IsRecoverable(exception))
            {
                return Result(Failure(operationId, target.RunId, exception));
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
                    return Result(interrupted);
                }
            }

            var state = await observation.ConfigureAwait(false);
            lock (this.gate)
            {
                var outcome = this.Check(operationId, target, cancellationToken) ?? Accepted(operationId, target.RunId);
                return Result(outcome, outcome.Succeeded ? state : null);
            }
        }
        catch (Exception exception) when (IsRecoverable(exception))
        {
            return Result(Failure(operationId, target.RunId, exception));
        }

        (RuntimeCommandResult outcome, TState? state) Result(RuntimeCommandResult outcome, TState? state = null)
            => (outcome with { SceneTarget = target }, state);
    }
}
