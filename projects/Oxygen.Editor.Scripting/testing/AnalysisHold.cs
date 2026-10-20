// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scripting.Testing;

/// <summary>
/// One analysis a <see cref="FakeScriptAnalyzer"/> holds back: a test learns when the analysis has reached the wait,
/// and decides when it goes on.
/// </summary>
/// <remarks>Made by <see cref="FakeScriptAnalyzer.HoldNext"/>; holds are given to analyses in the order they are made.</remarks>
public sealed class AnalysisHold
{
    private readonly TaskCompletionSource arrived = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TaskCompletionSource released = new(TaskCreationOptions.RunContinuationsAsynchronously);

    /// <summary>Initializes a new instance of the <see cref="AnalysisHold"/> class.</summary>
    /// <remarks>
    /// Only <see cref="FakeScriptAnalyzer.HoldNext"/> makes holds: it queues each one for the next analysis that waits.
    /// A hold made anywhere else would be given to no analysis, so its <see cref="Arrived"/> would never complete.
    /// </remarks>
    internal AnalysisHold()
    {
    }

    /// <summary>Gets a task that completes when an analysis has reached the wait this hold imposes.</summary>
    public Task Arrived => this.arrived.Task;

    /// <summary>Lets the held analysis go on.</summary>
    public void Release() => this.released.TrySetResult();

    /// <summary>Holds back the analysis given this hold: reports it arrived, then waits until the test releases it.</summary>
    /// <param name="cancellationToken">A token that cancels the wait, as it cancels the analysis.</param>
    /// <returns>A task that completes when the hold is released.</returns>
    internal Task WaitAsync(CancellationToken cancellationToken)
    {
        _ = this.arrived.TrySetResult();
        return this.released.Task.WaitAsync(cancellationToken);
    }
}
