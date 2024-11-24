// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.Projects;

/// <summary>
/// A custom JSON converter for <see cref="Category"/>.
/// </summary>
public sealed class CategoryJsonConverter : JsonConverter<Category>
{
    /// <inheritdoc/>
    public override Category Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        if (reader.TokenType != JsonTokenType.String)
        {
            throw new JsonException($"Expecting a string token for the category ID, but got `{reader.TokenType}`");
        }

        var categoryId = reader.GetString();
        var category = Category.ById(categoryId!);
        return category ?? throw new JsonException($"Unknown category ID '{categoryId}'.");
    }

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, Category value, JsonSerializerOptions options)
        => writer.WriteStringValue(value.Id);
}
