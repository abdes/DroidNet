// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine;

/// <summary>Names the state a history's root is in, so a caller can tell when it returns to a state it saw.</summary>
/// <remarks>
/// A new entry moves the history to a new position. Undoing an entry returns it to the position before the entry, and
/// redoing it returns it to the position after, even though undo and redo record a new inverse change each time.
/// Clearing the history moves it to a new position.
/// </remarks>
public readonly record struct HistoryPosition
{
    /// <summary>Initializes a new instance of the <see cref="HistoryPosition"/> struct.</summary>
    /// <param name="value">The position's identity within its history.</param>
    internal HistoryPosition(long value) => this.Value = value;

    /// <summary>Gets the position's identity within its history.</summary>
    internal long Value { get; }
}
