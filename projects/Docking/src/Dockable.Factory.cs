// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Collections;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;

/// <summary>
/// Extension of the <see cref="Dockable" /> class, implementing a Factory that
/// keeps track of all created instances and ensures uniqueness of the dockable
/// ID.
/// </summary>
public partial class Dockable
{
    private bool disposed;

    public event EventHandler<EventArgs>? OnDisposed;

    public static IEnumerable<IDockable> All => new AllDockables();

    public static Dockable? FromId(string id)
        => Factory.TryGetDockable(id, out var dockable) ? dockable : null;

    public static Dockable New(string id) => Factory.CreateDockable(typeof(Dockable), id);

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="Dockable" /> and optionally releases
    /// the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true" /> to release both managed and unmanaged resources; <see langword="false" />
    /// to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            /* Dispose of managed resources */

            // First invoke any subscribers to the OnDisposed event, because they
            // may still need to use the dockable before it is disposed.
            this.OnDisposed?.Invoke(this, EventArgs.Empty);

            Factory.ReleaseDockable(this.Id);
        }

        /* Dispose of unmanaged resources */

        this.disposed = true;
    }

    /// <summary>
    /// A factory class for instances of the <see cref="Dockable" /> class. Manages all the existing instances and guarantees
    /// uniqueness of their id.
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1034:Nested types should not be visible",
        Justification = "nested to be able to set the Id of a dockable, which we want to keep private settable")]
    public static partial class Factory
    {
        private static readonly ConcurrentDictionary<string, Dockable> Dockables = [];

        public static Dockable CreateDockable(string id, params object[] args)
            => CreateDockable(typeof(Dockable), id, args);

        public static Dockable CreateDockable(Type type, string id, params object[] args)
        {
            Debug.Assert(
                type.IsAssignableTo(typeof(Dockable)),
                "can only create instances of classes derived from me");

            if (Dockables.ContainsKey(id))
            {
                throw new InvalidOperationException($"attempt to create a dockable with an already used ID: {id}");
            }

            Dockable? dockable;
            try
            {
                args = [id, .. args];
                dockable = Activator.CreateInstance(
                    type,
                    BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                    binder: null,
                    args,
                    culture: null) as Dockable;
            }
            catch (Exception ex)
            {
                throw new ObjectCreationException(message: null, ex) { ObjectType = type };
            }

            return Manage(dockable ?? throw new ObjectCreationException { ObjectType = type });
        }

        public static bool TryGetDockable(string id, out Dockable? dockable)
            => Dockables.TryGetValue(id, out dockable);

        public static void ReleaseDockable(string id)
        {
            var found = Dockables.Remove(id, out _);
            Debug.Assert(
                found,
                $"unexpected call to {nameof(ReleaseDockable)} with an id `{id}` that is not currently managed");
        }

        private static Dockable Manage(Dockable dockable)
        {
            var added = Dockables.TryAdd(dockable.Id, dockable);
            Debug.Assert(added, "adding a dockable with a new id should always succeed");
            return dockable;
        }

        /// <summary>
        /// Provides an implementation of <see cref="IEnumerator" /> for iteration over the collection of all dockables.
        /// </summary>
        internal sealed partial class DockablesEnumerator : IEnumerator<IDockable>
        {
            private int position = -1;
            private bool isDisposed;

            public IDockable Current => Dockables.Values.ElementAt(this.position);

            object IEnumerator.Current => this.Current;

            public bool MoveNext()
            {
                this.position++;
                return this.position < Dockables.Count;
            }

            public void Reset() => this.position = -1;

            /// <inheritdoc />
            public void Dispose() => this.Dispose(disposing: true);

            private void Dispose(bool disposing)
            {
                if (this.isDisposed)
                {
                    return;
                }

                if (disposing)
                {
                    /* Dispose of managed resources */
                }

                /* Dispose of unmanaged resources */

                this.isDisposed = true;
            }
        }
    }

    /// <summary>
    /// Represents an <see cref="IEnumerable">enumerable</see> collection of all the instances of <see cref="Dockable" />.
    /// </summary>
    private sealed partial class AllDockables : IEnumerable<IDockable>
    {
        public IEnumerator<IDockable> GetEnumerator() => new Factory.DockablesEnumerator();

        IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
    }
}
