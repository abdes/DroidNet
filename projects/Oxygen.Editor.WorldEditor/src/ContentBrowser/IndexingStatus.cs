// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Represents the current status of the asset indexing service.
/// </summary>
public enum IndexingStatus
{
    /// <summary>Indexing has not yet started.</summary>
    NotStarted,

    /// <summary>Initial indexing is in progress.</summary>
    Indexing,

    /// <summary>Initial indexing completed, file watching is active.</summary>
    Completed,

    /// <summary>Indexing stopped or encountered an error.</summary>
    Stopped,
}
