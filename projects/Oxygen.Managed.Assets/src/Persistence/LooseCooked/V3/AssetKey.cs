// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace Oxygen.Managed.Assets.Persistence.LooseCooked.V3;

/// <summary>Runtime-facing 128-bit asset key as stored in the index.</summary>
/// <remarks>Its raw-byte ordering is distinct from <see cref="Guid"/> ordering.</remarks>
[StructLayout(LayoutKind.Sequential)]
public readonly record struct AssetKey(ulong Part0, ulong Part1)
{
    /// <summary>Reads an asset key from exactly 16 bytes.</summary>
    public static AssetKey FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 16)
        {
            throw new ArgumentException("AssetKey must be exactly 16 bytes.", nameof(bytes));
        }

        return new(BinaryPrimitives.ReadUInt64LittleEndian(bytes[..8]), BinaryPrimitives.ReadUInt64LittleEndian(bytes.Slice(8, 8)));
    }

    /// <summary>Writes the asset key to a destination of at least 16 bytes.</summary>
    public void WriteBytes(Span<byte> destination)
    {
        if (destination.Length < 16)
        {
            throw new ArgumentException("Destination must be at least 16 bytes.", nameof(destination));
        }

        BinaryPrimitives.WriteUInt64LittleEndian(destination[..8], this.Part0);
        BinaryPrimitives.WriteUInt64LittleEndian(destination.Slice(8, 8), this.Part1);
    }

    /// <inheritdoc />
    public override string ToString()
    {
        Span<byte> bytes = stackalloc byte[16];
        this.WriteBytes(bytes);
        return new Guid(bytes, bigEndian: true).ToString("D");
    }
}
