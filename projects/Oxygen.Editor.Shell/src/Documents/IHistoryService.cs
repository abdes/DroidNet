// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>The histories of an editing session, which ends when its project closes.</summary>
/// <remarks>
/// The global history records the actions that are not inside one document, such as relocating, renaming or deleting
/// assets. Code that changes something records its change into the history it is given; it never creates, finds or
/// clears one. Undo and Redo run the history's entries in order.
/// </remarks>
public interface IHistoryService
{
    /// <summary>Gets the session's global history.</summary>
    public HistoryKeeper GlobalHistory { get; }
}
