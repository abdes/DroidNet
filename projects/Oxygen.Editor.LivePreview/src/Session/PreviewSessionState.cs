// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>Where the preview of the open project stands.</summary>
public enum PreviewSessionState
{
    /// <summary>The session has not started.</summary>
    NotStarted,

    /// <summary>The engine is starting or the project's content is being mounted.</summary>
    Starting,

    /// <summary>The engine runs and shows the project's cooked content.</summary>
    Ready,

    /// <summary>The engine could not start; authoring still works and the start can be retried.</summary>
    EngineUnavailable,

    /// <summary>The project's cooked content could not be mounted; authoring still works and mounting can be retried.</summary>
    ContentUnavailable,
}
