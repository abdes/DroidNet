// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Linq.Expressions;

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents a change that must be applied asynchronously and executes a delegate on a target.
/// </summary>
/// <typeparam name="TTarget">The type of the target object.</typeparam>
public sealed class AsyncLambdaExpressionOnTarget<TTarget> : TargetedChange<TTarget>
{
    private readonly Func<TTarget, CancellationToken, ValueTask> applyAsync;

    /// <summary>
    /// Initializes a new instance of the <see cref="AsyncLambdaExpressionOnTarget{TTarget}"/> class.
    /// </summary>
    /// <param name="target">The target object on which the delegate will be executed.</param>
    /// <param name="expression">The expression to compile and execute asynchronously.</param>
    public AsyncLambdaExpressionOnTarget(TTarget target, Expression<Func<TTarget, ValueTask>> expression)
        : base(target)
    {
        var compiled = expression.Compile();
        this.applyAsync = (t, _) => compiled(t);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="AsyncLambdaExpressionOnTarget{TTarget}"/> class.
    /// </summary>
    /// <param name="target">The target object on which the delegate will be executed.</param>
    /// <param name="expression">The expression to compile and execute asynchronously.</param>
    public AsyncLambdaExpressionOnTarget(TTarget target, Expression<Func<TTarget, Task>> expression)
        : base(target)
    {
        var compiled = expression.Compile();
        this.applyAsync = (t, cancellationToken) =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            return new ValueTask(compiled(t));
        };
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="AsyncLambdaExpressionOnTarget{TTarget}"/> class.
    /// </summary>
    /// <param name="target">The target object on which the delegate will be executed.</param>
    /// <param name="selector">The asynchronous delegate to execute.</param>
    public AsyncLambdaExpressionOnTarget(TTarget target, Func<TTarget, CancellationToken, ValueTask> selector)
        : base(target)
    {
        this.applyAsync = selector;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="AsyncLambdaExpressionOnTarget{TTarget}"/> class.
    /// </summary>
    /// <param name="target">The target object on which the delegate will be executed.</param>
    /// <param name="selector">The asynchronous delegate to execute.</param>
    public AsyncLambdaExpressionOnTarget(TTarget target, Func<TTarget, ValueTask> selector)
        : base(target)
    {
        this.applyAsync = (t, _) => selector(t);
    }

    /// <inheritdoc />
    public override void Apply() => throw new InvalidOperationException("This change must be applied asynchronously. Use ApplyAsync() or HistoryKeeper.UndoAsync/RedoAsync.");

    /// <inheritdoc />
    public override ValueTask ApplyAsync(CancellationToken cancellationToken = default) => this.applyAsync(this.Target, cancellationToken);
}
