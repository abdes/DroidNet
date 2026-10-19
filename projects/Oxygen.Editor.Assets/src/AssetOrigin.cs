// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets;

/// <summary>Where an asset comes from, which pickers show as separate groups.</summary>
public enum AssetOrigin
{
    /// <summary>The engine content pak's assets, under <c>/Engine</c>.</summary>
    Engine,

    /// <summary>Assets of a user pak the project mounts.</summary>
    Package,

    /// <summary>The project's authored assets and the cooked assets of its libraries.</summary>
    Content,
}
