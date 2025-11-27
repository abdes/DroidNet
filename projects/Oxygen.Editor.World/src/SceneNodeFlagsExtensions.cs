// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World;

/// <summary>
/// Helpers for working with <see cref="SceneNodeFlags"/> such as bitmask conversions.
/// </summary>
public static class SceneNodeFlagsExtensions
{
    /// <summary>
    /// Returns the numeric bitmask that represents the provided <see cref="SceneNodeFlags"/>.
    /// </summary>
    /// <param name="flags">The flags to convert to a compact bitmask.</param>
    /// <returns>A <see cref="uint"/> containing the bitmask representing <paramref name="flags"/>.</returns>
    public static uint ToBitmask(this SceneNodeFlags flags) => (uint)flags;

    /// <summary>
    /// Creates a <see cref="SceneNodeFlags"/> instance from a raw numeric bitmask.
    /// </summary>
    /// <param name="mask">A <see cref="uint"/> bitmask where bits correspond to the <see cref="SceneNodeFlags"/> values.</param>
    /// <returns>A <see cref="SceneNodeFlags"/> with the bits set from <paramref name="mask"/>.</returns>
    public static SceneNodeFlags FromBitmask(uint mask) => (SceneNodeFlags)mask;
}
