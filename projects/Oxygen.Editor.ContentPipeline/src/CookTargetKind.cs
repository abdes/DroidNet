// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// User-visible cook target kinds.
/// </summary>
public enum CookTargetKind
{
    /// <summary>
    /// Current scene and its dependencies.
    /// </summary>
    CurrentScene,

    /// <summary>
    /// A single asset.
    /// </summary>
    Asset,

    /// <summary>
    /// All cookable assets under a folder.
    /// </summary>
    Folder,

    /// <summary>
    /// All cookable project assets.
    /// </summary>
    Project,
}
