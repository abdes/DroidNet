// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents a change that must be applied asynchronously.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "multiple constructors")]
public sealed class AsyncSimpleAction : Change
{
    private readonly Func<CancellationToken, ValueTask> applyAsync;

    /// <summary>
    /// Initializes a new instance of the <see cref="AsyncSimpleAction"/> class.
    /// </summary>
    /// <param name="action">The asynchronous action to execute when the change is applied.</param>
    public AsyncSimpleAction(Func<CancellationToken, ValueTask> action)
    {
        this.applyAsync = action;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="AsyncSimpleAction"/> class.
    /// </summary>
    /// <param name="action">The asynchronous action to execute when the change is applied.</param>
    public AsyncSimpleAction(Func<ValueTask> action)
        : this(_ => action())
    {
    }

    /// <inheritdoc />
    public override void Apply() => throw new InvalidOperationException("This change must be applied asynchronously. Use ApplyAsync() or HistoryKeeper.UndoAsync/RedoAsync.");

    /// <inheritdoc />
    public override ValueTask ApplyAsync(CancellationToken cancellationToken = default) => this.applyAsync(cancellationToken);
}
