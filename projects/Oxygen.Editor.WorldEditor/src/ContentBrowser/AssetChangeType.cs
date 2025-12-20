// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.ContentBrowser;

/// <summary>
/// Represents the type of change that occurred to an asset.
/// </summary>
public enum AssetChangeType
{
    /// <summary>Asset was added (discovered during indexing or created via file watch).</summary>
    Added,

    /// <summary>Asset was removed (deleted file detected via file watch).</summary>
    Removed,

    /// <summary>Asset was modified (file change detected via file watch).</summary>
    Modified,
}
