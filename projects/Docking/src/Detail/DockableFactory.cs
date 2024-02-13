// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections;
using System.Collections.Concurrent;
using System.Diagnostics;

/// <summary>
/// Extension of the <see cref="Dockable" /> class, implementing a Factory that
/// keeps track of all created instances and ensures uniqueness of the dockable
/// ID.
/// </summary>
public partial class Dockable
{
    public static IEnumerable<IDockable> All => new AllDockables();

    public static Dockable? FromId(string id)
        => Factory.TryGetDockable(id, out var dockable) ? dockable : null;

    public static Dockable? New(string id) => Factory.CreateDockable(id);

    public void Dispose()
    {
        Factory.ReleaseDockable(this.Id);
        GC.SuppressFinalize(this);
    }

    public class AllDockables : IEnumerable<IDockable>
    {
        public IEnumerator<IDockable> GetEnumerator() => new Factory.DockablesEnumerator();

        IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
    }

    private static class Factory
    {
        private static readonly ConcurrentDictionary<string, Dockable> Dockables = [];

        public static Dockable? CreateDockable(string id)
        {
            if (Dockables.ContainsKey(id))
            {
                throw new InvalidOperationException($"attempt to create a dockable with an already used ID: {id}");
            }

            try
            {
                var dockable = new Dockable(id);
                var added = Dockables.TryAdd(id, dockable);
                Debug.Assert(added, "adding a dockable with a new id should always succeed");
                return dockable;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to create a new dockable: {ex}");
                return null;
            }
        }

        public static bool TryGetDockable(string id, out Dockable? dockable)
            => Dockables.TryGetValue(id, out dockable);

        public static void ReleaseDockable(string id)
        {
            var found = Dockables.Remove(id, out var _);
            Debug.Assert(
                found,
                $"unexpected call to {nameof(ReleaseDockable)} with an id `{id}` that is not currently managed");
        }

        public class DockablesEnumerator : IEnumerator<IDockable>
        {
            private int position = -1;

            public IDockable Current
            {
                get
                {
                    try
                    {
                        return Dockables.Values.ElementAt(this.position);
                    }
                    catch (IndexOutOfRangeException)
                    {
                        throw new InvalidOperationException();
                    }
                }
            }

            object IEnumerator.Current => this.Current;

            public bool MoveNext()
            {
                this.position++;
                return this.position < Dockables.Count;
            }

            public void Reset() => this.position = -1;

            public void Dispose() => GC.SuppressFinalize(this);
        }
    }
}
