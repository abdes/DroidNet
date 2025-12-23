// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace Oxygen.Assets.Persistence.LooseCooked.V1;

/// <summary>
/// Runtime-facing 128-bit asset key as stored in the index (raw bytes).
/// </summary>
/// <remarks>
/// We intentionally do not use <see cref="Guid"/> here because its byte ordering semantics are not
/// guaranteed to match the runtime <c>data::AssetKey</c> layout.
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
public readonly record struct AssetKey(ulong Part0, ulong Part1)
{
    /// <summary>
    /// Reads an asset key from exactly 16 bytes (little-endian parts).
    /// </summary>
    /// <param name="bytes">The bytes containing the key.</param>
    /// <returns>The parsed key.</returns>
    public static AssetKey FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 16)
        {
            throw new ArgumentException("AssetKey must be exactly 16 bytes.", nameof(bytes));
        }

        var p0 = BinaryPrimitives.ReadUInt64LittleEndian(bytes[..8]);
        var p1 = BinaryPrimitives.ReadUInt64LittleEndian(bytes.Slice(8, 8));
        return new AssetKey(p0, p1);
    }

    /// <summary>
    /// Writes the asset key to the destination span.
    /// </summary>
    /// <param name="destination">The destination span (at least 16 bytes).</param>
    public void WriteBytes(Span<byte> destination)
    {
        if (destination.Length < 16)
        {
            throw new ArgumentException("Destination must be at least 16 bytes.", nameof(destination));
        }

        BinaryPrimitives.WriteUInt64LittleEndian(destination[..8], this.Part0);
        BinaryPrimitives.WriteUInt64LittleEndian(destination.Slice(8, 8), this.Part1);
    }

    public override string ToString() => $"{this.Part0:x16}{this.Part1:x16}";
}
