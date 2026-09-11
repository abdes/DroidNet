// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;

namespace Oxygen.Managed.Assets.Cook;

/// <summary>Emits the default extension fields from the runtime MaterialAssetDesc contract.</summary>
public static partial class CookedMaterialWriter
{
    private static void WriteRuntimeDefaults(Span<byte> descriptor)
    {
        // The packed runtime header occupies 103 bytes. Extension offsets follow
        // Oxygen/Data/PakFormat_render.h; the complete descriptor is 357 bytes.
        WriteSingle(descriptor.Slice(0xC2, 4), 1.5f); // IOR
        WriteUnorm16(descriptor.Slice(0xC6, 2), 1f); // Specular factor
        for (var channel = 0; channel < 3; channel++)
        {
            BinaryPrimitives.WriteHalfLittleEndian(descriptor.Slice(0xD6 + (channel * 2), 2), (Half)1);
        }

        WriteSingles(descriptor, 0xE0, [1f, 1f]); // UV scale
        WriteSingles(descriptor, 0xF5, [1f, 1f]); // Grid spacing
        BinaryPrimitives.WriteUInt32LittleEndian(descriptor.Slice(0xFD, 4), 10); // Grid major interval
        WriteSingle(descriptor.Slice(0x101, 4), 1f); // Grid line thickness
        WriteSingle(descriptor.Slice(0x105, 4), 2f); // Grid major thickness
        WriteSingle(descriptor.Slice(0x109, 4), 2f); // Grid axis thickness
        WriteSingles(descriptor, 0x115, [0.35f, 0.35f, 0.35f, 1f]);
        WriteSingles(descriptor, 0x125, [0.55f, 0.55f, 0.55f, 1f]);
        WriteSingles(descriptor, 0x135, [0.90f, 0.20f, 0.20f, 1f]);
        WriteSingles(descriptor, 0x145, [0.20f, 0.60f, 0.90f, 1f]);
        WriteSingles(descriptor, 0x155, [1f, 1f, 1f, 1f]);
    }

    private static void WriteSingles(Span<byte> descriptor, int offset, ReadOnlySpan<float> values)
    {
        for (var index = 0; index < values.Length; index++)
        {
            WriteSingle(descriptor.Slice(offset + (index * 4), 4), values[index]);
        }
    }
}
