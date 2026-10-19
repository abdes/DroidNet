// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;

namespace Oxygen.Managed.Assets.Persistence;

/// <summary>
/// The facts of a cooked texture that its resource-table record states, as the engine's
/// <c>TextureResourceDesc</c> (<c>Oxygen/Data/PakFormat_core.h</c>) lays them out.
/// </summary>
/// <param name="TextureType">The engine <c>TextureType</c> value.</param>
/// <param name="Format">The engine <c>Format</c> value.</param>
/// <param name="Width">The width in texels.</param>
/// <param name="Height">The height in texels.</param>
/// <param name="ArrayLayers">The array layers; six for a cube.</param>
public sealed record TextureResourceRecord(byte TextureType, byte Format, uint Width, uint Height, ushort ArrayLayers)
{
    /// <summary>The size of the native record.</summary>
    public const int Size = 39;

    // Engine TextureType (Oxygen/Core/Types/TextureType.h) and Format (Oxygen/Core/Types/Format.h) values.
    private const byte Texture2D = 3;
    private const byte TextureCube = 5;
    private const byte Rgba16Float = 39;
    private const byte Rgba32Float = 42;

    /// <summary>Gets a value indicating whether the texture is a single 2D image.</summary>
    public bool Is2D => this.TextureType == Texture2D;

    /// <summary>Gets a value indicating whether the texture is a cube.</summary>
    public bool IsCube => this.TextureType == TextureCube;

    /// <summary>Gets a value indicating whether the texels are floating point, as lighting radiance requires.</summary>
    public bool StoresRadiance => this.Format is Rgba16Float or Rgba32Float;

    /// <summary>Reads the native record.</summary>
    /// <param name="bytes">Exactly <see cref="Size"/> bytes.</param>
    /// <returns>The record.</returns>
    /// <exception cref="InvalidDataException">The bytes are not one record.</exception>
    public static TextureResourceRecord Read(ReadOnlySpan<byte> bytes)
        => bytes.Length != Size
            ? throw new InvalidDataException($"A texture resource record is {Size} bytes, not {bytes.Length}.")
            : new(
                bytes[12],
                bytes[28],
                BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(14, 4)),
                BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(18, 4)),
                BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(24, 2)));
}
