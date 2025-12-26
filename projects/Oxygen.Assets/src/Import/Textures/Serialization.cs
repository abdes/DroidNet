// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Assets.Import.Textures;

/// <summary>
/// Shared JSON serialization configuration for texture authoring JSON.
/// </summary>
internal static class Serialization
{
    internal static readonly JsonSerializerOptions Options = new()
    {
        AllowTrailingCommas = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        PropertyNameCaseInsensitive = false,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
    };

    internal static readonly TextureSerializationContext Context = new(Options);
}
