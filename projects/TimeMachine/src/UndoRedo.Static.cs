// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine;

/// <summary>
/// Provides static helpers for the global undo/redo history cache.
/// </summary>
public sealed partial class UndoRedo
{
    /// <summary>
    /// Gets (or creates) a <see cref="HistoryKeeper"/> for the specified identifier.
    /// </summary>
    /// <param name="id">
    /// A <see cref="Guid"/> that uniquely identifies the document or entity for which this
    /// <see cref="HistoryKeeper"/> will track Undo/Redo changes.
    /// </param>
    /// <returns>A <see cref="HistoryKeeper"/> instance.</returns>
    public static HistoryKeeper GetHistory(Guid id) => GuidHistories.Value.GetOrAdd(id, guid => new HistoryKeeper(guid));

    /// <summary>
    /// Clears the cached undo roots.
    /// </summary>
    public static void Clear()
    {
        Histories.Value.Clear();
        GuidHistories.Value.Clear();
    }
}
