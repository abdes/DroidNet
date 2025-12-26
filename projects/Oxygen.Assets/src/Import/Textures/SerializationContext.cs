// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Assets.Import.Textures;

/// <summary>
/// Source-generated JSON serialization context for authoring texture JSON (<c>*.otex.json</c>).
/// </summary>
[JsonSourceGenerationOptions(
    WriteIndented = true,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    PropertyNameCaseInsensitive = false)]
[JsonSerializable(typeof(TextureSourceData))]
[JsonSerializable(typeof(TextureMipPolicyData))]
[JsonSerializable(typeof(TextureRuntimeFormatData))]
[JsonSerializable(typeof(TextureImportedData))]
internal partial class TextureSerializationContext : JsonSerializerContext;
