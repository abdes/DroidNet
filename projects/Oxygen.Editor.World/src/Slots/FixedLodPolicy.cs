// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// LOD policy that always uses a fixed LOD level.
/// </summary>
/// <remarks>
/// This policy is useful for forcing a specific detail level regardless of distance or screen space error.
/// Common use cases include previews, cinematic cameras, or objects that should always render at a specific quality.
/// </remarks>
public class FixedLodPolicy : LodPolicy
{
    /// <summary>
    /// Gets or sets the LOD index to use.
    /// </summary>
    /// <value>
    /// The zero-based LOD index. Lower values represent higher detail. Default is 0 (highest detail).
    /// </value>
    public int LodIndex { get; set; }
}
