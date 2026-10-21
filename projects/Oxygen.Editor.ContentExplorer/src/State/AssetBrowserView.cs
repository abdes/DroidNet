// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser;

/// <summary>The ways the Content Browser shows its results.</summary>
public enum AssetBrowserView
{
    /// <summary>Preview tiles with name, type and status.</summary>
    Tiles,

    /// <summary>A compact list for scanning.</summary>
    List,

    /// <summary>A table with sortable columns.</summary>
    Details,
}
