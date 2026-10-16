// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Per-document context required by scene document commands.
/// </summary>
/// <param name="DocumentId">The identity of the scene's document.</param>
/// <param name="Scene">The scene the commands change.</param>
/// <param name="History">The document's history, into which the commands record their changes.</param>
public sealed record SceneDocumentCommandContext(
    DocumentId DocumentId,
    Scene Scene,
    HistoryKeeper History);
