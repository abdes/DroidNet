// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;

namespace DroidNet.Docking;

/// <summary>
/// Represents content (a <see cref="ViewModel"/>) that can be docked in a <see cref="IDock"/>.
/// </summary>
/// <remarks>
/// The <see cref="Dockable"/> class provides the base implementation for dockable content within a docking framework. It includes properties for identification, display titles, preferred dimensions, and ownership. It also includes an event for disposal notification.
/// </remarks>
public partial class Dockable
{
    private bool disposed;

    /// <inheritdoc/>
    public event EventHandler<EventArgs>? OnDisposed;

    /// <summary>
    /// Gets an enumerable collection of all dockable instances.
    /// </summary>
    /// <value>
    /// An <see cref="IEnumerable{IDockable}"/> representing all dockable instances.
    /// </value>
    public static IEnumerable<IDockable> All => new AllDockables();

    /// <summary>
    /// Retrieves a dockable instance by its unique identifier.
    /// </summary>
    /// <param name="id">The unique identifier of the dockable instance to retrieve.</param>
    /// <returns>
    /// The <see cref="Dockable"/> instance with the specified identifier, or <see langword="null"/> if no such instance exists.
    /// </returns>
    public static Dockable? FromId(string id)
        => Factory.TryGetDockable(id, out var dockable) ? dockable : null;

    /// <summary>
    /// Creates a new instance of the <see cref="Dockable"/> class with the specified unique identifier.
    /// </summary>
    /// <param name="id">The unique identifier for the new dockable instance.</param>
    /// <returns>A new instance of the <see cref="Dockable"/> class.</returns>
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
    /// A factory class for instances of the <see cref="Dockable"/> class. Manages all the existing instances and guarantees
    /// uniqueness of their id.
    /// </summary>
    /// <remarks>
    /// The <see cref="Factory"/> class provides methods to create and manage instances of <see cref="Dockable"/>. It ensures that each dockable has a unique ID.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1034:Nested types should not be visible",
        Justification = "nested to be able to set the Id of a dockable, which we want to keep private settable")]
    public static partial class Factory
    {
        private static readonly ConcurrentDictionary<string, Dockable> Dockables = [];

        /// <summary>
        /// Creates a new instance of the specified dockable type.
        /// </summary>
        /// <param name="id">The unique identifier for the dockable.</param>
        /// <param name="args">The arguments to pass to the dockable's constructor.</param>
        /// <returns>A new instance of the specified dockable type.</returns>
        /// <exception cref="InvalidOperationException">Thrown when a dockable with the specified ID already exists.</exception>
        /// <exception cref="ObjectCreationException">Thrown when the dockable cannot be created.</exception>
        /// <remarks>
        /// This method uses reflection to create an instance of the specified dockable type. It ensures that the dockable is managed and assigned a unique ID.
        /// <para>
        /// <strong>Example Usage:</strong>
        /// <code><![CDATA[
        /// var dockable = Factory.CreateDockable(typeof(CustomDockable), "unique-id");
        /// ]]></code>
        /// </para>
        /// </remarks>
        public static Dockable CreateDockable(string id, params object[] args)
            => CreateDockable(typeof(Dockable), id, args);

        /// <summary>
        /// Creates a new instance of the specified dockable type.
        /// </summary>
        /// <param name="type">The type of dockable to create.</param>
        /// <param name="id">The unique identifier for the dockable.</param>
        /// <param name="args">The arguments to pass to the dockable's constructor.</param>
        /// <returns>A new instance of the specified dockable type.</returns>
        /// <exception cref="InvalidOperationException">Thrown when a dockable with the specified ID already exists.</exception>
        /// <exception cref="ObjectCreationException">Thrown when the dockable cannot be created.</exception>
        /// <remarks>
        /// This method uses reflection to create an instance of the specified dockable type. It ensures that the dockable is managed and assigned a unique ID.
        /// <para>
        /// <strong>Example Usage:</strong>
        /// <code><![CDATA[
        /// var dockable = Factory.CreateDockable(typeof(CustomDockable), "unique-id");
        /// ]]></code>
        /// </para>
        /// </remarks>
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

        /// <summary>
        /// Tries to get a dockable by its ID.
        /// </summary>
        /// <param name="id">The ID of the dockable to retrieve.</param>
        /// <param name="dockable">When this method returns, contains the dockable associated with the specified ID, if the ID is found; otherwise, <see langword="null"/>.</param>
        /// <returns><see langword="true"/> if the dockable is found; otherwise, <see langword="false"/>.</returns>
        /// <remarks>
        /// This method attempts to retrieve a dockable by its ID from the internal dictionary of managed dockables.
        /// </remarks>
        public static bool TryGetDockable(string id, out Dockable? dockable)
            => Dockables.TryGetValue(id, out dockable);

        /// <summary>
        /// Releases a dockable by its ID.
        /// </summary>
        /// <param name="id">The ID of the dockable to release.</param>
        /// <remarks>
        /// This method removes the dockable from the internal dictionary of managed dockables.
        /// </remarks>
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
        /// Provides an implementation of <see cref="IEnumerator"/> for iteration over the collection of all dockables.
        /// </summary>
        /// <remarks>
        /// This enumerator is used to iterate over all managed dockables.
        /// </remarks>
        internal sealed partial class DockablesEnumerator : IEnumerator<IDockable>
        {
            private int position = -1;
            private bool isDisposed;

            /// <inheritdoc/>
            public IDockable Current => Dockables.Values.ElementAt(this.position);

            /// <inheritdoc/>
            object IEnumerator.Current => this.Current;

            /// <inheritdoc/>
            public bool MoveNext()
            {
                this.position++;
                return this.position < Dockables.Count;
            }

            /// <inheritdoc/>
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
    /// Represents an <see cref="IEnumerable"/> collection of all the instances of <see cref="Dockable"/>.
    /// </summary>
    /// <remarks>
    /// This class provides an enumerable collection of all managed dockables.
    /// </remarks>
    private sealed partial class AllDockables : IEnumerable<IDockable>
    {
        /// <inheritdoc/>
        public IEnumerator<IDockable> GetEnumerator() => new Factory.DockablesEnumerator();

        /// <inheritdoc/>
        IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
    }
}
