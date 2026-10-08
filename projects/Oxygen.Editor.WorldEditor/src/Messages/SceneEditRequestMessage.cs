// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Messages;

/// <summary>A history or deletion command the viewport asks the Scene Explorer to run.</summary>
internal enum SceneEditRequest
{
    /// <summary>Undo the last change.</summary>
    Undo,

    /// <summary>Redo the last undone change.</summary>
    Redo,

    /// <summary>Delete the selection.</summary>
    Delete,
}

/// <summary>
/// Asks the Scene Explorer to run a history or deletion command for a scene, as its own keyboard
/// shortcuts do, so the viewport and the Explorer share one command path.
/// </summary>
/// <param name="DocumentId">The scene document.</param>
/// <param name="Request">The command.</param>
internal sealed record SceneEditRequestMessage(Guid DocumentId, SceneEditRequest Request);
