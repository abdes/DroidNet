// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

/// <summary>
/// Reader for a loose cooked root's texture descriptor (<c>.otex</c>): the texture-table index and record of one
/// texture, as the engine's <c>TextureResourceDescriptorRecord</c> (<c>Oxygen/Data/ResourceDescriptor.h</c>) writes them.
/// </summary>
public static class TextureDescriptorFile
{
    /// <summary>The size of the header before the record: the magic, the index and the version.</summary>
    public const int HeaderSize = 10;

    /// <summary>The size of the file.</summary>
    public const int Size = HeaderSize + TextureResourceRecord.Size;

    /// <summary>The format version the reader accepts.</summary>
    public const ushort Version = 3;

    /// <summary>Reads a texture descriptor.</summary>
    /// <param name="bytes">The file's bytes.</param>
    /// <returns>The texture's record.</returns>
    /// <exception cref="InvalidDataException">The bytes are not a current texture descriptor.</exception>
    public static TextureResourceRecord Read(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != Size || !bytes[..4].SequenceEqual("OTEX"u8)
            || BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(4, 4)) == 0
            || BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(8, 2)) != Version)
        {
            throw new InvalidDataException("The texture descriptor is not a current OTEX descriptor naming a texture.");
        }

        return TextureResourceRecord.Read(bytes[HeaderSize..]);
    }
}
