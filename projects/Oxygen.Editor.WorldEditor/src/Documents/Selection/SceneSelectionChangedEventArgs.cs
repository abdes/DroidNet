// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Documents.Selection;

/// <summary>
/// Carries one <see cref="ISceneSelectionService.SelectionChanged"/> notification:
/// which document changed, the context that is now stored, and the writer's
/// source tag so subscribers can tell foreign changes from their own echoes.
/// </summary>
/// <param name="documentId">The document whose selection changed.</param>
/// <param name="context">The selection context stored by the write.</param>
/// <param name="source">The selection source that performed the write.</param>
public sealed class SceneSelectionChangedEventArgs(
    Guid documentId,
    SceneSelectionContext context,
    string source) : EventArgs
{
    /// <summary>Gets the document whose selection changed.</summary>
    public Guid DocumentId { get; } = documentId;

    /// <summary>Gets the selection context stored by the write.</summary>
    public SceneSelectionContext Context { get; } = context;

    /// <summary>Gets the selection source that performed the write.</summary>
    public string Source { get; } = source;
}
