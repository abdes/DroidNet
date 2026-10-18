// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>Reads the asset references that scene data stores.</summary>
internal static class AssetPathData
{
    /// <summary>Returns a stored asset reference, or rejects data that leaves it out.</summary>
    /// <param name="asset">The stored reference; the converter has already refused anything but an asset URI.</param>
    /// <param name="property">The name of the property that stores it.</param>
    /// <returns><paramref name="asset"/>.</returns>
    /// <exception cref="JsonException"><paramref name="asset"/> is missing.</exception>
    public static AssetPath Require(AssetPath? asset, string property)
        => asset ?? throw new JsonException($"{property} is missing.");
}
