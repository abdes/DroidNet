// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Assets.Import.Textures;

public static class TextureSourceWriter
{
    public static void Write(Stream output, TextureSourceData data)
    {
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(data);

        JsonSerializer.Serialize(output, data, Serialization.Context.TextureSourceData);
    }
}
