// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Managed.Core;

/// <summary>
/// The handler storage behind an event's <c>add</c> and <c>remove</c> accessors, for an event raised on each
/// subscriber's own context: the <see cref="SynchronizationContext"/> that was current when it subscribed, or the
/// thread pool when there was none.
/// </summary>
/// <remarks>
/// The owner declares the event with custom accessors that call <see cref="Add"/> and <see cref="Remove"/>, and
/// raises it with <see cref="Invoke"/>, as it would invoke the delegate of a field-like event. It captures each
/// subscriber's context as <see cref="Progress{T}"/> captures its creator's.
/// </remarks>
/// <typeparam name="THandler">The event's delegate type.</typeparam>
public sealed class SubscriberEvent<THandler>
    where THandler : Delegate
{
    private ImmutableList<Subscription> subscriptions = [];

    /// <summary>Adds a handler, with the context it is raised on.</summary>
    /// <param name="handler">The handler; nothing happens when it is <see langword="null"/>.</param>
    public void Add(THandler? handler)
    {
        if (handler is not null)
        {
            _ = ImmutableInterlocked.Update(ref this.subscriptions, static (list, added) => list.Add(added), new Subscription(handler, SynchronizationContext.Current));
        }
    }

    /// <summary>Removes the last subscription of a handler.</summary>
    /// <param name="handler">The handler.</param>
    public void Remove(THandler? handler)
    {
        if (handler is not null)
        {
            _ = ImmutableInterlocked.Update(
                ref this.subscriptions,
                static (list, removed) => list.FindLastIndex(subscription => subscription.Handler.Equals(removed)) is var index and >= 0 ? list.RemoveAt(index) : list,
                handler);
        }
    }

    /// <summary>Invokes the handlers: posts the call to each handler's context.</summary>
    /// <param name="invoke">Calls one handler with the event's arguments.</param>
    public void Invoke(Action<THandler> invoke)
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
