// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>Reads the asset URIs that scene data stores.</summary>
internal static class AssetUriData
{
    /// <summary>Returns a stored URI that names an asset, or rejects the data.</summary>
    /// <param name="uri">The stored URI.</param>
    /// <param name="property">The name of the property that stores it.</param>
    /// <returns><paramref name="uri"/>.</returns>
    /// <exception cref="JsonException"><paramref name="uri"/> is missing or does not name an asset.</exception>
    public static Uri Read(Uri? uri, string property)
        => AssetUris.IsAssetUri(uri)
            ? uri
            : throw new JsonException($"{property} '{uri}' is not an absolute '{AssetUris.Scheme}' URI naming an asset.");
}
