// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Source-generated JSON serialization context for authoring material JSON (<c>*.omat.json</c>).
/// </summary>
[JsonSourceGenerationOptions(
    WriteIndented = true,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    PropertyNameCaseInsensitive = false)]
[JsonSerializable(typeof(MaterialSourceData))]
[JsonSerializable(typeof(PbrMetallicRoughnessData))]
[JsonSerializable(typeof(TextureRefData))]
internal partial class SerializationContext : JsonSerializerContext;
