// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// A simple thread-safe implementation of <see cref="IObservable{T}"/> for broadcasting values to observers.
/// </summary>
/// <typeparam name="T">The type of value to observe.</typeparam>
internal sealed class SimpleSubject<T> : IObservable<T>
{
    private readonly ConcurrentDictionary<Guid, IObserver<T>> observers = new();

    /// <summary>
    /// Subscribes an observer to receive notifications.
    /// </summary>
    /// <param name="observer">The observer to subscribe.</param>
    /// <returns>An <see cref="IDisposable"/> that can be used to unsubscribe.</returns>
    public IDisposable Subscribe(IObserver<T> observer)
    {
        var id = Guid.NewGuid();
        _ = this.observers.TryAdd(id, observer);
        return new Unsubscriber(this.observers, id);
    }

    /// <summary>
    /// Publishes a value to all subscribed observers.
    /// </summary>
    /// <param name="value">The value to publish.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Observers are external; ensure one failing observer does not prevent others from receiving notifications.")]
    public void OnNext(T value)
    {
        foreach (var observer in this.observers.Values)
        {
            try
            {
                observer.OnNext(value);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"SimpleSubject observer OnNext threw: {ex}");
            }
        }
    }

    /// <summary>
    /// Notifies observers of an error condition.
    /// </summary>
    /// <param name="error">The exception representing the error.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Observers are external; ensure one failing observer does not prevent others from receiving notifications.")]
    public void OnError(Exception error)
    {
        foreach (var observer in this.observers.Values)
        {
            try
            {
                observer.OnError(error);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"SimpleSubject observer OnError threw: {ex}");
            }
        }
    }

    /// <summary>
    /// Notifies observers that the sequence has completed.
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Observers are external; ensure one failing observer does not prevent others from receiving notifications.")]
    public void OnCompleted()
    {
        foreach (var observer in this.observers.Values)
        {
            try
            {
                observer.OnCompleted();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"SimpleSubject observer OnCompleted threw: {ex}");
            }
        }
    }

    /// <summary>
    /// Handles unsubscription of observers from the subject.
    /// </summary>
    /// <param name="observers">The collection of observers.</param>
    /// <param name="id">The unique identifier for the observer.</param>
    private sealed class Unsubscriber(ConcurrentDictionary<Guid, IObserver<T>> observers, Guid id) : IDisposable
    {
        private readonly ConcurrentDictionary<Guid, IObserver<T>> observers = observers;
        private readonly Guid id = id;

        /// <summary>
        /// Removes the observer from the collection.
        /// </summary>
        public void Dispose() => _ = this.observers.TryRemove(this.id, out _);
    }
}
