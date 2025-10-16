// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Extension methods for <see cref="DispatcherQueue"/>.
/// </summary>
internal static partial class DispatcherQueueExtensions
{
    /// <summary>
    /// Enqueues an action on the dispatcher queue and returns a task that completes when the action finishes.
    /// </summary>
    /// <param name="dispatcher">The dispatcher queue.</param>
    /// <param name="action">The action to execute on the dispatcher thread.</param>
    /// <param name="logger">Optional logger to receive exceptions from the dispatcher callback.</param>
    /// <returns>A task that completes when the action has executed.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the action could not be enqueued.</exception>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Exceptions are captured and forwarded to the returned Task; they must not escape the dispatcher callback.")]
    public static Task EnqueueAsync(this DispatcherQueue dispatcher, Action action, ILogger? logger = null)
    {
        ArgumentNullException.ThrowIfNull(dispatcher);
        ArgumentNullException.ThrowIfNull(action);

        var tcs = new TaskCompletionSource();

        var enqueued = dispatcher.TryEnqueue(() =>
        {
            try
            {
                action();
                tcs.SetResult();
            }
            catch (Exception ex)
            {
                LogEnqueueActionFailed(logger ?? NullLoggerFactory.Instance.CreateLogger("DispatcherQueueExtensions"), ex);
                tcs.SetException(ex);
            }
        });

        if (!enqueued)
        {
            throw new InvalidOperationException("Failed to enqueue action on dispatcher queue");
        }

        return tcs.Task;
    }

    /// <summary>
    /// Enqueues a function on the dispatcher queue and returns a task that completes with the result.
    /// </summary>
    /// <typeparam name="T">The return type of the function.</typeparam>
    /// <param name="dispatcher">The dispatcher queue.</param>
    /// <param name="func">The function to execute on the dispatcher thread.</param>
    /// <param name="logger">Optional logger to receive exceptions from the dispatcher callback.</param>
    /// <returns>A task that completes with the function result.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the function could not be enqueued.</exception>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Exceptions are captured and forwarded to the returned Task; they must not escape the dispatcher callback.")]
    public static Task<T> EnqueueAsync<T>(this DispatcherQueue dispatcher, Func<T> func, ILogger? logger = null)
    {
        ArgumentNullException.ThrowIfNull(dispatcher);
        ArgumentNullException.ThrowIfNull(func);

        var tcs = new TaskCompletionSource<T>();

        var enqueued = dispatcher.TryEnqueue(() =>
        {
            try
            {
                var result = func();
                tcs.SetResult(result);
            }
            catch (Exception ex)
            {
                LogEnqueueFuncFailed(logger ?? NullLoggerFactory.Instance.CreateLogger("DispatcherQueueExtensions"), ex);
                tcs.SetException(ex);
            }
        });

        if (!enqueued)
        {
            throw new InvalidOperationException("Failed to enqueue function on dispatcher queue");
        }

        return tcs.Task;
    }

    [LoggerMessage(
        EventId = 4300,
        Level = LogLevel.Error,
        Message = "[DispatcherQueueExtensions] enqueue action failed")]
    private static partial void LogEnqueueActionFailed(ILogger logger, Exception exception);

    [LoggerMessage(
        EventId = 4301,
        Level = LogLevel.Error,
        Message = "[DispatcherQueueExtensions] enqueue func failed")]
    private static partial void LogEnqueueFuncFailed(ILogger logger, Exception exception);
}
