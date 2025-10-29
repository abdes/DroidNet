// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.Specialized;

namespace DroidNet.Controls;

/// <summary>
///     A minimal, dispatcher-aware proxy over an <see cref="IEnumerable{T}"/> that forwards
///     <see cref="INotifyCollectionChanged.CollectionChanged"/> events from a source notifier onto
///     a provided enqueue function (typically a UI dispatcher).
/// </summary>
/// <typeparam name="T">The type of items in the collection.</typeparam>
/// <remarks>
///     This proxy does not maintain its own copy of the data. It delegates enumeration, indexing
///     and Count to the provided <c>source</c>. When the source raises collection-changed events
///     the proxy invokes <c>tryEnqueue</c> to marshal the re-raise of the same event on the
///     dispatcher.
///     <para>
///     Use this when you want a lightweight wrapper that ensures collection change events are
///     observed on the UI thread and you're happy to rely on the source for enumeration. If you
///     need an actual UI-thread-owned collection instance (ObservableCollection) prefer a proxy
///     that maintains an inner collection and applies diffs.
///     </para>
/// </remarks>
public sealed class DispatcherCollectionProxy<T> : IReadOnlyList<T>, INotifyCollectionChanged, IDisposable
{
    private readonly IEnumerable<T> source;
    private readonly INotifyCollectionChanged? sourceNotifier;
    private readonly Func<Action, bool>? tryEnqueue;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DispatcherCollectionProxy{T}"/> class as a
    ///     proxy over <paramref name="source"/> that listens to <paramref name="sourceNotifier"/>
    ///     and re-raises events on the dispatcher via <paramref name="tryEnqueue"/>.
    /// </summary>
    /// <param name="source">The source collection to proxy. Cannot be <see langword="null"/>.</param>
    /// <param name="sourceNotifier">An optional source notifier to listen for collection changes.</param>
    /// <param name="tryEnqueue">An optional function to enqueue actions on the dispatcher.</param>
    public DispatcherCollectionProxy(IEnumerable<T> source, INotifyCollectionChanged? sourceNotifier = null, Func<Action, bool>? tryEnqueue = null)
    {
        this.source = source ?? throw new ArgumentNullException(nameof(source));
        this.sourceNotifier = sourceNotifier;
        this.tryEnqueue = tryEnqueue;

        if (this.sourceNotifier is not null)
        {
            this.sourceNotifier.CollectionChanged += this.OnSourceCollectionChanged;
        }
    }

    /// <inheritdoc />
    public event NotifyCollectionChangedEventHandler? CollectionChanged;

    /// <inheritdoc />
    public int Count
    {
        get
        {
            if (this.source is ICollection<T> collT)
            {
                return collT.Count;
            }

            if (this.source is IReadOnlyCollection<T> roColl)
            {
                return roColl.Count;
            }

            // fallback: enumerate
            return this.source.Count();
        }
    }

    /// <inheritdoc />
    public T this[int index]
    {
        get
        {
            if (this.source is IList<T> listT)
            {
                return listT[index];
            }

            if (this.source is IReadOnlyList<T> roList)
            {
                return roList[index];
            }

            // fallback: enumerate to index
            using var e = this.source.GetEnumerator();
            for (var i = 0; i <= index; i++)
            {
                if (!e.MoveNext())
                {
                    throw new ArgumentOutOfRangeException(nameof(index));
                }
            }

            return e.Current!;
        }
    }

    /// <inheritdoc />
    public IEnumerator<T> GetEnumerator() => this.source.GetEnumerator();

    /// <inheritdoc/>
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();

    /// <summary>
    /// Stop listening to the source notifier.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        if (this.sourceNotifier is not null)
        {
            this.sourceNotifier.CollectionChanged -= this.OnSourceCollectionChanged;
        }

        this.disposed = true;
    }

    private void OnSourceCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (this.disposed)
        {
            return;
        }

        void Raise() => this.CollectionChanged?.Invoke(this, e);

        if (this.tryEnqueue is null)
        {
            Raise();
        }
        else
        {
            if (!this.tryEnqueue(Raise))
            {
                // fallback: raise inline
                Raise();
            }
        }
    }
}
