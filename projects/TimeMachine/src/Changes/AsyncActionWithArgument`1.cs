// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents a change that must be applied asynchronously and executes an action with an argument.
/// </summary>
/// <typeparam name="TArgument">The type of the argument passed to the action.</typeparam>
public sealed class AsyncActionWithArgument<TArgument> : Change
{
    private readonly Func<TArgument?, CancellationToken, ValueTask> applyAsync;
    private readonly TArgument? argument;

    /// <summary>
    /// Initializes a new instance of the <see cref="AsyncActionWithArgument{TArgument}"/> class.
    /// </summary>
    /// <param name="action">The asynchronous action to execute when the change is applied.</param>
    /// <param name="argument">The argument to pass to the action.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "multiple constructors")]
    public AsyncActionWithArgument(Func<TArgument?, CancellationToken, ValueTask> action, TArgument? argument)
    {
        this.applyAsync = action;
        this.argument = argument;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="AsyncActionWithArgument{TArgument}"/> class.
    /// </summary>
    /// <param name="action">The asynchronous action to execute when the change is applied.</param>
    /// <param name="argument">The argument to pass to the action.</param>
    public AsyncActionWithArgument(Func<TArgument?, ValueTask> action, TArgument? argument)
        : this((a, _) => action(a), argument)
    {
    }

    /// <inheritdoc />
    public override void Apply() => throw new InvalidOperationException("This change must be applied asynchronously. Use ApplyAsync() or HistoryKeeper.UndoAsync/RedoAsync.");

    /// <inheritdoc />
    public override ValueTask ApplyAsync(CancellationToken cancellationToken = default) => this.applyAsync(this.argument, cancellationToken);
}
