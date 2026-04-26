// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Per-document context required by scene document commands.
/// </summary>
/// <param name="DocumentId">The document identity.</param>
/// <param name="Metadata">The mutable document metadata.</param>
/// <param name="Scene">The active scene model.</param>
/// <param name="History">The document-scoped undo/redo history.</param>
public sealed record SceneDocumentCommandContext(
    Guid DocumentId,
    SceneDocumentMetadata Metadata,
    Scene Scene,
    HistoryKeeper History);
