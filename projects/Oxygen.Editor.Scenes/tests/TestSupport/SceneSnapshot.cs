// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// What an edit that did nothing leaves the same. Two snapshots are equal when the document is.
/// </summary>
/// <param name="Document">The dehydrated scene as JSON, explorer layout included.</param>
/// <param name="Hidden">The hidden nodes, sorted.</param>
/// <param name="UndoCount">The number of undoable entries.</param>
/// <param name="RedoCount">The number of redoable entries.</param>
/// <param name="ChangedCount">The number of <c>Changed</c> events raised so far.</param>
public sealed record SceneSnapshot(string Document, string Hidden, int UndoCount, int RedoCount, int ChangedCount);
