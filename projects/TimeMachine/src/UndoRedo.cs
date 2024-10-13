// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine;

using System.Runtime.CompilerServices;

public sealed class UndoRedo
{
    private static readonly Lazy<UndoRedo> DefaultInstance = new(() => new UndoRedo());

    private static readonly Lazy<ConditionalWeakTable<object, HistoryKeeper>> Histories = new(() => []);

    internal UndoRedo()
    {
    }

    public static UndoRedo Default => DefaultInstance.Value;

    /// <summary>
    /// Get (or create) a <see cref="HistoryKeeper" /> for the specified object or document instance.
    /// </summary>
    /// <param name="root">An object that represents the root in the hierarchy for which this <see cref="HistoryKeeper" /> will
    /// track Undo/Redo changes.</param>
    /// <returns>A <see cref="HistoryKeeper" /> instance.</returns>
    public HistoryKeeper this[object root] => Histories.Value.GetValue(root, (r) => new HistoryKeeper(r));

    /// <summary>
    /// Clear the cached UndoRoots.
    /// </summary>
    public static void Clear() => Histories.Value.Clear();
}
