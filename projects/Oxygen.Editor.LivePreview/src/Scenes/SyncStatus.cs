// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>How the engine took an edit the preview projected into it.</summary>
/// <remarks>
/// The values are ordered from best to worst; an edit made of several commands reports the worst of their statuses.
/// Whatever the status, the authored edit stands: the document, not the engine, is the source of truth.
/// </remarks>
internal enum SyncStatus
{
    /// <summary>The engine applied the edit.</summary>
    Accepted = 0,

    /// <summary>
    /// The engine does not run, is faulted, or has no world; the edit is projected again with the scene when the engine
    /// next runs.
    /// </summary>
    SkippedNotRunning,

    /// <summary>The engine has no support for the edit.</summary>
    Unsupported,

    /// <summary>The engine refused the edit's values.</summary>
    Rejected,

    /// <summary>The edit stopped before the engine took it, because its document closed or its caller cancelled it.</summary>
    Cancelled,

    /// <summary>The engine failed while applying the edit.</summary>
    Failed,
}
