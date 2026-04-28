// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// User-facing authoring/cooked identity state for browser rows and typed pickers.
/// </summary>
public enum AssetState
{
    /// <summary>
    /// Generated editor/engine asset.
    /// </summary>
    Generated,

    /// <summary>
    /// Source asset.
    /// </summary>
    Source,

    /// <summary>
    /// Oxygen descriptor asset.
    /// </summary>
    Descriptor,

    /// <summary>
    /// Cooked asset.
    /// </summary>
    Cooked,

    /// <summary>
    /// Cooked asset is older than its descriptor/source.
    /// </summary>
    Stale,

    /// <summary>
    /// Referenced asset is missing.
    /// </summary>
    Missing,

    /// <summary>
    /// Asset exists but cannot be read or validated.
    /// </summary>
    Broken,
}
