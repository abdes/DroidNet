// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Shared JSON serialization configuration for material authoring JSON.
/// </summary>
internal static class Serialization
{
    /// <summary>
    /// Gets the JSON serializer options used for material authoring JSON.
    /// </summary>
    internal static readonly JsonSerializerOptions Options = new()
    {
        AllowTrailingCommas = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        PropertyNameCaseInsensitive = false,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
    };

    /// <summary>
    /// Gets the source-generated serialization context configured with <see cref="Options"/>.
    /// </summary>
    internal static readonly SerializationContext Context = new(Options);
}
