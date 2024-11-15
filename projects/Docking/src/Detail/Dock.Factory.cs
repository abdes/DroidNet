// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;

/// <summary>
/// Extension of the <see cref="Dock" /> class, implementing a generic Factory
/// to create instances of a specific dock type.
/// </summary>
public partial class Dock
{
    public static Dock? FromId(DockId id)
        => Factory.TryGetDock(id, out var dock) ? dock : null;

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1034:Nested types should not be visible",
        Justification = "nested to be able to set the Id of a dock, which we want to keep private settable")]
    public static class Factory
    {
        private static readonly ConcurrentDictionary<DockId, Dock> Docks = [];

        /// <summary>The next value of DockId to use when creating a new dock.</summary>
        private static readonly AtomicCounter NextId = new();

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
