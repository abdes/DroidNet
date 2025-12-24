// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Assets.Import.Geometry;

namespace Oxygen.Assets.Import.Gltf;

/// <summary>
/// Handles serialization of <see cref="ImportedGeometry"/> to/from the .imported cache.
/// </summary>
public static class ImportedGeometrySerializer
{
    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        IncludeFields = true,
    };

    public static void Write(Stream stream, ImportedGeometry geometry)
    {
        JsonSerializer.Serialize(stream, geometry, Options);
    }

    public static ImportedGeometry? Read(Stream stream)
    {
        return JsonSerializer.Deserialize<ImportedGeometry>(stream, Options);
    }

    public static async Task WriteAsync(Stream stream, ImportedGeometry geometry, CancellationToken cancellationToken = default)
    {
        await JsonSerializer.SerializeAsync(stream, geometry, Options, cancellationToken).ConfigureAwait(false);
    }

    public static async Task<ImportedGeometry?> ReadAsync(Stream stream, CancellationToken cancellationToken = default)
    {
        return await JsonSerializer.DeserializeAsync<ImportedGeometry>(stream, Options, cancellationToken).ConfigureAwait(false);
    }
}
