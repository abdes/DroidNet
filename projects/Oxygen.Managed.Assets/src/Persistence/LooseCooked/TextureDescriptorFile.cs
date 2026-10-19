// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

/// <summary>
/// Reader for a loose cooked root's texture descriptor (<c>.otex</c>): the texture-table index and record of one
/// texture, as the engine's <c>TextureResourceDescriptor</c> (<c>Oxygen/Data/TextureResourceDescriptor.h</c>) writes
/// them.
/// </summary>
public static class TextureDescriptorFile
{
    /// <summary>The size of the file.</summary>
    public const int Size = 12 + TextureResourceRecord.Size;

    private const ushort Version = 1;

    /// <summary>Reads a texture descriptor.</summary>
    /// <param name="bytes">The file's bytes.</param>
    /// <returns>The texture's record.</returns>
    /// <exception cref="InvalidDataException">The bytes are not a current texture descriptor.</exception>
    public static TextureResourceRecord Read(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != Size || !bytes[..4].SequenceEqual("OTEX"u8)
            || BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(4, 2)) != Version
            || BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(6, 2)) != 0
            || BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(8, 4)) == 0)
        {
            throw new InvalidDataException("The texture descriptor is not a current OTEX descriptor naming a texture.");
        }

        return TextureResourceRecord.Read(bytes[12..]);
    }
}
