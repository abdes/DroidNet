// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Command contexts over a history of their own, which tell whether the scene changed since it was saved.</summary>
/// <remarks>A scene document is dirty when its history is at another position than the one it was saved at.</remarks>
internal static class SceneContexts
{
    private static readonly ConditionalWeakTable<HistoryKeeper, StrongBox<HistoryPosition>> SavedPositions = [];

    /// <summary>Creates the context of a saved scene document.</summary>
    /// <param name="scene">The scene.</param>
    /// <returns>The context.</returns>
    public static SceneDocumentCommandContext Create(Scene scene)
    {
        var history = new HistoryKeeper(scene);
        SavedPositions.AddOrUpdate(history, new(history.Position));
        return new(DocumentId.New(), scene, history);
    }

    /// <summary>Tells whether the scene changed since it was saved.</summary>
    /// <param name="context">The context.</param>
    /// <returns>Whether the history left the saved position.</returns>
    public static bool IsDirty(this SceneDocumentCommandContext context)
        => !SavedPositions.TryGetValue(context.History, out var saved) || saved.Value != context.History.Position;

    /// <summary>Marks the scene as saved at the history's current position.</summary>
    /// <param name="context">The context.</param>
    public static void MarkSaved(this SceneDocumentCommandContext context)
        => SavedPositions.AddOrUpdate(context.History, new(context.History.Position));
}
