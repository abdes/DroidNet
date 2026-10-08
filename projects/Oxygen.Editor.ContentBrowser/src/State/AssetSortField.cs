// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser;

/// <summary>The fields that order Content Browser results.</summary>
public enum AssetSortField
{
    /// <summary>The asset name.</summary>
    Name,

    /// <summary>The user-facing asset type.</summary>
    Type,

    /// <summary>The displayed source and cooking status.</summary>
    Status,

    /// <summary>The folder that holds the asset.</summary>
    Location,

    /// <summary>The size of the asset's file.</summary>
    Size,

    /// <summary>When the asset's file last changed.</summary>
    Modified,
}
