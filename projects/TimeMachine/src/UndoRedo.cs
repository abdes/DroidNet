// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Runtime.CompilerServices;

namespace DroidNet.TimeMachine;

/// <summary>
/// Provides a singleton instance to manage undo/redo operations for different root objects.
/// </summary>
/// <remarks>
/// The <see cref="UndoRedo"/> class provides a singleton instance to manage undo/redo operations for different root objects.
/// It uses a <see cref="ConditionalWeakTable{TKey, TValue}"/> to store <see cref="HistoryKeeper"/> instances for each root object,
/// ensuring that undo/redo histories are tracked separately for each root.
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// var rootObject = new object();
/// var historyKeeper = UndoRedo.Default[rootObject];
///
/// using (var transaction = historyKeeper.BeginTransaction("exampleTransaction"))
/// {
///     historyKeeper.AddChange("ChangeLabel1", targetObject, t => t.SomeMethod());
///     historyKeeper.AddChange("ChangeLabel2", arg => Console.WriteLine(arg), "Hello, World!");
///     historyKeeper.AddChange("ChangeLabel3", () => Console.WriteLine("Action executed"));
///     transaction.Commit();
/// }
///
/// historyKeeper.Undo();
/// historyKeeper.Redo();
/// ]]></code>
/// </example>
public sealed class UndoRedo
{
    private static readonly Lazy<UndoRedo> DefaultInstance = new(() => new UndoRedo());

    private static readonly Lazy<ConditionalWeakTable<object, HistoryKeeper>> Histories = new(() => []);

    private static readonly Lazy<ConcurrentDictionary<Guid, HistoryKeeper>> GuidHistories = new(() => []);

    /// <summary>
    /// Initializes a new instance of the <see cref="UndoRedo"/> class.
    /// </summary>
    internal UndoRedo()
    {
    }

    /// <summary>
    /// Gets the singleton instance of the <see cref="UndoRedo"/> class.
    /// </summary>
    public static UndoRedo Default => DefaultInstance.Value;

    /// <summary>
    /// Gets (or creates) a <see cref="HistoryKeeper"/> for the specified object or document instance.
    /// </summary>
    /// <param name="root">
    /// An object that represents the root in the hierarchy for which this <see cref="HistoryKeeper"/> will
    /// track Undo/Redo changes.
    /// </param>
    /// <returns>A <see cref="HistoryKeeper"/> instance.</returns>
    public HistoryKeeper this[object root] => Histories.Value.GetValue(root, r => new HistoryKeeper(r));

    /// <summary>
    /// Gets (or creates) a <see cref="HistoryKeeper"/> for the specified identifier.
    /// </summary>
    /// <param name="id">
    /// A <see cref="Guid"/> that uniquely identifies the document or entity for which this
    /// <see cref="HistoryKeeper"/> will track Undo/Redo changes.
    /// </param>
    /// <returns>A <see cref="HistoryKeeper"/> instance.</returns>
    public HistoryKeeper this[Guid id] => GuidHistories.Value.GetOrAdd(id, guid => new HistoryKeeper(guid));

    /// <summary>
    /// Clears the cached undo roots.
    /// </summary>
    public static void Clear()
    {
        Histories.Value.Clear();
        GuidHistories.Value.Clear();
    }
}
