// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Input;

/// <summary>
/// The handlers of an event that is raised on each subscriber's own context: the
/// <see cref="SynchronizationContext"/> that was current when it subscribed, or the thread pool when there was none.
/// </summary>
/// <typeparam name="THandler">The event's delegate type.</typeparam>
internal sealed class SubscriberEvent<THandler>
    where THandler : Delegate
{
    private ImmutableList<Subscription> subscriptions = [];

    /// <summary>Adds a handler, with the context it is raised on.</summary>
    /// <param name="handler">The handler; nothing happens when it is <see langword="null"/>.</param>
    public void Add(THandler? handler)
    {
        if (handler is not null)
        {
            ImmutableInterlocked.Update(ref this.subscriptions, static (list, added) => list.Add(added), new Subscription(handler, SynchronizationContext.Current));
        }
    }

    /// <summary>Removes the last subscription of a handler.</summary>
    /// <param name="handler">The handler.</param>
    public void Remove(THandler? handler)
    {
        if (handler is not null)
        {
            ImmutableInterlocked.Update(
                ref this.subscriptions,
                static (list, removed) => list.FindLastIndex(subscription => subscription.Handler.Equals(removed)) is var index and >= 0 ? list.RemoveAt(index) : list,
                handler);
        }
    }

    /// <summary>Raises the event: posts the call to each handler's context.</summary>
    /// <param name="invoke">Calls one handler with the event's arguments.</param>
    public void Raise(Action<THandler> invoke)
    {
        ArgumentNullException.ThrowIfNull(invoke);
        foreach (var subscription in this.subscriptions)
        {
            var call = () => invoke(subscription.Handler);
            if (subscription.Context is { } context)
            {
                context.Post(static state => ((Action)state!).Invoke(), call);
            }
            else
            {
                _ = ThreadPool.QueueUserWorkItem(static state => state.Invoke(), call, preferLocal: false);
            }
        }
    }

    private sealed record Subscription(THandler Handler, SynchronizationContext? Context);
}
