// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.IO.Abstractions;

namespace Oxygen.Editor.Models;

/// <summary>A model import record as its file holds it: the record and the exact bytes it was read from or written as.</summary>
/// <remarks>
/// A replacement of the record states the bytes it expects the file to hold, and the record's writer compares them with
/// the file's; the record serialized again is not those bytes when another writer, such as the ImportTool, wrote it.
/// </remarks>
/// <param name="Record">The record.</param>
/// <param name="Content">The file's bytes.</param>
public sealed record ModelImportRecordSnapshot(ModelImportSettings Record, ImmutableArray<byte> Content)
{
    /// <summary>Reads the record at <paramref name="recordPath"/>.</summary>
    /// <param name="fileSystem">The project's file system.</param>
    /// <param name="recordPath">The record's full path.</param>
    /// <returns>The record and its bytes.</returns>
    /// <exception cref="InvalidDataException">The record cannot be read.</exception>
    public static ModelImportRecordSnapshot Read(IFileSystem fileSystem, string recordPath)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        var bytes = fileSystem.File.ReadAllBytes(recordPath);
        return new(ModelImportSettings.Parse(recordPath, bytes), [.. bytes]);
    }

    /// <summary>Gets the snapshot of a record as its writer writes it.</summary>
    /// <param name="record">The record.</param>
    /// <returns>The record and the bytes it is written as.</returns>
    public static ModelImportRecordSnapshot Of(ModelImportSettings record)
    {
        ArgumentNullException.ThrowIfNull(record);
        return new(record, [.. record.ToBytes()]);
    }

    /// <summary>Gets whether the file holds the same bytes as <paramref name="other"/>.</summary>
    /// <param name="other">Another snapshot of the record.</param>
    /// <returns><see langword="true"/> when the bytes are equal.</returns>
    public bool HoldsSameBytes(ModelImportRecordSnapshot other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return this.Content.AsSpan().SequenceEqual(other.Content.AsSpan());
    }
}
