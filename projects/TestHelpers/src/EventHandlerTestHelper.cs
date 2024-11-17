// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;

namespace DroidNet.TestHelpers;

/// <summary>
/// A helper class for unit tests that need to check if a specific event handler is registered/unregistered on a specific
/// event of an object.
/// </summary>
[ExcludeFromCodeCoverage]
public static class EventHandlerTestHelper
{
    /// <summary>
    /// Return a list of all delegates registered for the named event of the given <paramref name="eventEmitter" />.
    /// </summary>
    /// <param name="eventEmitter">An object with an event handler named as <paramref name="eventName" />.</param>
    /// <param name="eventName">The event name.</param>
    /// <returns>
    /// A list of all delegates registered as event handler on the named event specified by <paramref name="eventName" /> of
    /// the <paramref name="eventEmitter" /> object.
    /// </returns>
    public static IList<Delegate> FindAllRegisteredDelegates(object eventEmitter, string eventName)
    {
        var b = eventEmitter;

        const BindingFlags filter = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

        var fieldTheEvent = b.GetType().GetField(eventName, filter);

        // The field that we get has a type that can be cast to a Delegate
        var eventDelegate = (Delegate?)fieldTheEvent?.GetValue(b);

        return eventDelegate == null ? [] : new List<Delegate>(eventDelegate.GetInvocationList());
    }

    /// <summary>
    /// Return a list of all delegates registered for the named event of the given <paramref name="eventEmitter" /> that
    /// belong to the given <paramref name="target" />.
    /// </summary>
    /// <param name="eventEmitter">An object with an event handler named as <paramref name="eventName" />.</param>
    /// <param name="eventName">The event name.</param>
    /// <param name="target">The object which must be the target of the event handler's invocation.</param>
    /// <returns>
    /// A list of all delegates registered for the named event of the given <paramref name="eventEmitter" /> that belong to
    /// the given <paramref name="target" />.
    /// </returns>
    public static IList<Delegate> FindRegisteredDelegates(
        object eventEmitter,
        string eventName,
        object target)
        => FindAllRegisteredDelegates(eventEmitter, eventName)
            .ToList()
            .FindAll(del => ReferenceEquals(del.Target, target));
}
