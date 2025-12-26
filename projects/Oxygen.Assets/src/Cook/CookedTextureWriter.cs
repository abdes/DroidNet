// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;

namespace Oxygen.Assets.Cook;

public static class CookedTextureWriter
{
    private static readonly byte[] Magic = [(byte)'O', (byte)'T', (byte)'E', (byte)'X'];
    private const uint Version = 1;

    public static void Write(Stream stream, uint tableIndex)
    {
        stream.Write(Magic);

        Span<byte> buffer = stackalloc byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(buffer, Version);
        stream.Write(buffer);

        BinaryPrimitives.WriteUInt32LittleEndian(buffer, tableIndex);
        stream.Write(buffer);

        // Padding/Reserved
        stream.Write(new byte[4]);
    }
}
