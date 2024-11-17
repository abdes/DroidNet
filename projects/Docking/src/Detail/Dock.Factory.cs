// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;

namespace DroidNet.Docking.Detail;

/// <summary>
/// Extension of the <see cref="Dock" /> class, implementing a generic Factory
/// to create instances of a specific dock type.
/// </summary>
public partial class Dock
{
    /// <summary>
    /// Retrieves a dock instance by its ID.
    /// </summary>
    /// <param name="id">The ID of the dock to retrieve.</param>
    /// <returns>The dock instance if found; otherwise, <see langword="null"/>.</returns>
    public static Dock? FromId(DockId id)
        => Factory.TryGetDock(id, out var dock) ? dock : null;

    /// <summary>
    /// Factory class for creating instances of <see cref="Dock"/>.
    /// </summary>
    /// <remarks>
    /// The <see cref="Factory"/> class provides methods to create and manage instances of <see cref="Dock"/>.
    /// It ensures that each dock has a unique ID.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1034:Nested types should not be visible",
        Justification = "nested to be able to set the Id of a dock, which we want to keep private settable")]
    public static class Factory
    {
        /// <summary>The next value of DockId to use when creating a new dock.</summary>
        private static readonly ConcurrentDictionary<DockId, Dock> Docks = [];

        /// <summary>The next value of DockId to use when creating a new dock.</summary>
        private static readonly AtomicCounter NextId = new();

        /// <summary>
        /// Creates a new instance of the specified dock type.
        /// </summary>
        /// <param name="type">The type of dock to create.</param>
        /// <param name="args">The arguments to pass to the dock's constructor.</param>
        /// <returns>A new instance of the specified dock type.</returns>
        /// <exception cref="ObjectCreationException">Thrown when the dock cannot be created.</exception>
        /// <remarks>
        /// This method uses reflection to create an instance of the specified dock type. It ensures that the dock is managed and assigned a unique ID.
        /// <para>
        /// <strong>Example Usage:</strong>
        /// <code><![CDATA[
        /// var dock = Factory.CreateDock(typeof(ToolDock));
        /// ]]></code>
        /// </para>
        /// </remarks>
        public static Dock CreateDock(Type type, params object[] args)
        {
            Debug.Assert(
                type.IsAssignableTo(typeof(Dock)),
                "can only create instances of classes derived from me");

            Dock? dock;
            try
            {
                dock = Activator.CreateInstance(
                    type,
                    BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                    binder: null,
                    args,
                    culture: null) as Dock;
            }
            catch (Exception ex)
            {
                throw new ObjectCreationException(message: null, ex) { ObjectType = type };
            }

            return Manage(dock ?? throw new ObjectCreationException { ObjectType = type });
        }

        /// <summary>
        /// Tries to get a dock by its ID.
        /// </summary>
        /// <param name="id">The ID of the dock to retrieve.</param>
        /// <param name="dock">When this method returns, contains the dock associated with the specified ID, if the ID is found;
        /// otherwise, <see langword="null"/>.</param>
        /// <returns><see langword="true"/> if the dock is found; otherwise, <see langword="false"/>.</returns>
        /// <remarks>
        /// This method attempts to retrieve a dock by its ID from the internal dictionary of managed docks.
        /// </remarks>
        public static bool TryGetDock(DockId id, out Dock? dock)
            => Docks.TryGetValue(id, out dock);

        private static Dock Manage(Dock dock)
        {
            dock.Id = new DockId(NextId.Increment());
            var added = Docks.TryAdd(dock.Id, dock);
            Debug.Assert(added, "the dock id is always incremented, the key cannot already exist");
            return dock;
        }

        private sealed class AtomicCounter
        {
            private uint value;

            public uint Increment()
            {
                uint snapshot;
                uint newValue;

                do
                {
                    snapshot = this.value;
                    newValue = snapshot == uint.MaxValue ? 0 : snapshot + 1;
                }
                while (Interlocked.CompareExchange(ref this.value, newValue, snapshot) != snapshot);

                return newValue;
            }
        }
    }
}
